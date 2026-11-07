#include "windows/browser_dialogs.h"
#include "workspace/controller.h"
#include "app/updates.h"
#include "windows/pointer.h"
#include "app/session.h"
#include "app/browsing_profiles.h"
#include "app/auto_archive.h"
#include "windows/boosts.h"
#include "navigation/shortcut_settings.h"
#include "windows/tab_drag_preview.h"
#include "windows/devtools_dock.h"
#include <windows.h>
#include <dwmapi.h>
#include <algorithm>
#include <cmath>
#include <chrono>
#include <tuple>
#include "include/base/cef_callback.h"
#include "include/cef_image.h"
#include "include/cef_navigation_entry.h"
#include "include/cef_parser.h"
#include "include/wrapper/cef_closure_task.h"
#include "windows/embedded_content.h"
#include "windows/page_corners.h"
#include "windows/window_corners.h"
#include "windows/workspace_layout.h"
#include "windows/sidebar_peek.h"
#include "workspace/tab_order.h"
#include "windows/focus.h"
#include "windows/command_bar.h"
#include "trust_policy.h"
#include "downloads/download_manager.h"
#include "windows/panel_popup.h"
#include "history/favicon_cache.h"
#include "history/history_service.h"

namespace red {
namespace {
bool UrlAllowed(const std::string& url) {
  if (url.size() > 4096 || url.find_first_of("\r\n") != std::string::npos || url.find('\0') != std::string::npos) return false;
  CefURLParts parts;
  return CefParseURL(url, parts) && parts.host.length && !parts.username.length && !parts.password.length &&
      (CefString(&parts.scheme) == "https" || CefString(&parts.scheme) == "http");
}
// What the person may type or create in the trusted UI: websites and local
// files. Website-initiated links (OpenLink) stay limited to UrlAllowed.
bool UserUrlAllowed(const std::string& url) { return UrlAllowed(url) || IsLocalFileUrl(url); }
// Saved and live page addresses may also be extension pages that an extension
// opened itself, and local files the person opened.
// Our history page is a saved tab too (created in the trusted UI profile).
bool PageUrlAllowed(const std::string& url) { return UserUrlAllowed(url) || IsExtensionPageUrl(url) || IsInternalPageUrl(url); }
std::string IconHost(const CefString& url) {
  CefURLParts parts;
  return CefParseURL(url, parts) ? FaviconHost(CefString(&parts.host).ToString()) : "";
}
bool SamePageUrl(const std::string& live, const std::string& saved) {
  if (live == saved) return true;
  CefURLParts parts;
  return CefParseURL(saved, parts) && CefString(&parts.spec) == live;
}
// Tab title for a local file: its decoded file name.
std::string FileTitle(const std::string& url) {
  const auto name = url.substr(url.find_last_of('/') + 1);
  const auto decoded = CefURIDecode(name, true, static_cast<cef_uri_unescape_rule_t>(UU_SPACES | UU_PATH_SEPARATORS | UU_URL_SPECIAL_CHARS_EXCEPT_PATH_SEPARATORS)).ToString();
  return (decoded.empty() ? "Local file" : decoded).substr(0, 240);
}
// Saved themes only (not every frame of a drag in the theme editor).
void RememberTheme(const std::shared_ptr<State>& state) {
  if (!state->workspace_mode || state->standalone_window) return;
  const auto file = state->profile / "BrowserData" / "theme-cache";
  if (ReadThemeCache(file) != state->theme) WriteThemeCache(file, state->theme);
}
void KeepWindowSelection(TreeSnapshot& incoming, const TreeSnapshot& local) {
  if (std::any_of(incoming.spaces.begin(), incoming.spaces.end(), [&](const auto& s) { return s.id == local.active_space; }))
    incoming.active_space = local.active_space;
  incoming.active_id = local.active_id;
  for (auto& space : incoming.spaces) {
    space.active_id.clear();
    for (const auto& old : local.spaces) if (old.id == space.id) space.active_id = old.active_id;
  }
  const auto visible = OrderedTabsForSpace(incoming, incoming.active_space);
  if (std::find(visible.begin(), visible.end(), incoming.active_id) == visible.end()) incoming.active_id.clear();
}
}  // namespace
void WorkspaceController::Start() {
  const auto state = state_.lock();
  if (!state) return;
  session_ = state->session;
  if (!session_) {
    session_ = state->session = std::make_shared<AppSession>();
    session_->Register(state);
  }
  data_ = state->workspace_data ? state->workspace_data : (state->workspace_data = session_->workspace);
  if (!session_->preferences) session_->preferences = std::make_shared<BrowserPreferences>(state->profile / "BrowserData" / "settings.sqlite3");
  if (!session_->auto_archive) session_->auto_archive=AutoArchive::Start(session_);
  LoadBoosts(*session_, state->profile / "BrowserData" / "boosts.json");
  if (!session_->pinned_extensions) {
    session_->pinned_extensions = std::make_shared<PinnedExtensions>();
    session_->pinned_extensions->Load(state->profile / "BrowserData" / "pinned_extensions.json");
  }
  if (data_->worker) {
    worker_ = data_->worker;
    if (data_->ready) ReceiveSharedTree(data_->tree);
    return;
  }
  worker_ = CefThread::CreateThread("Workspace database");
  if (!worker_) { error_ = "Cannot start database worker"; Publish(); return; }
  data_->worker = worker_;
  if (data_->path.empty()) data_->path = state->profile / "BrowserData" / "workspace.sqlite3";
  const auto path = data_->path;
  worker_->GetTaskRunner()->PostTask(CefCreateClosureTask(base::BindOnce(
      [](std::shared_ptr<WorkspaceController> self, std::filesystem::path path, std::string initial_url, bool seed_default,
          std::string title, bool custom_title, bool standalone, Theme theme) {
        std::error_code error;
        if (path != ":memory:") std::filesystem::create_directories(path.parent_path(), error);  // A private window's sidebar is in memory.
        self->data_->store = std::make_unique<TreeStore>();
        auto result = error ? TreeResult{500, "Cannot create workspace directory", {}, {}} : self->data_->store->Open(path);
        if (result && result.snapshot.nodes.empty() && seed_default) {
          TreeCommand seed{TreeAction::CreateTab}; seed.title = title; seed.url = initial_url; seed.value = true;
          seed.expected_revision = result.snapshot.revision; result = self->data_->store->Apply(seed);
          if (result && custom_title) {
            TreeCommand rename{TreeAction::Rename}; rename.id = result.created_id; rename.title = title;
            rename.expected_revision = result.snapshot.revision; result = self->data_->store->Apply(rename);
          }
          if (result && standalone) result = self->data_->store->SetTheme(theme.hue, theme.intensity, static_cast<int>(theme.mode), theme.noise,"",theme.palette);
        }
        // A private window starts empty (its first tab is a new tab) in its own colours.
        if (result && path == ":memory:") result = self->data_->store->SetTheme(theme.hue, theme.intensity, static_cast<int>(theme.mode), theme.noise,"",theme.palette);
        CefPostTask(TID_UI, base::BindOnce([](std::weak_ptr<WorkspaceController> weak, TreeResult result) {
          if (auto owner = weak.lock()) owner->Complete(std::move(result), {});
        }, std::weak_ptr<WorkspaceController>(self), std::move(result)));
      }, shared_from_this(), path, state->initial_url, external_pending_.empty() && !state->private_window &&
          (state->probe || state->secondary_window || session_->preferences->Get().startup == "restore"),
      state->standalone_window ? state->initial_title : "Start", state->initial_custom_title, state->standalone_window, state->theme)));
}
void WorkspaceController::Stop() {
  stopped_ = true;
  CancelFolderRemoval();
  observer_ = {};
  // AppSession closes all data scopes on their worker threads after the last
  // window, including scopes whose standalone window was closed earlier.
  worker_ = nullptr;
  sessions_.clear(); browser_nodes_.clear();
}
void WorkspaceController::SetObserver(std::function<void(CefRefPtr<CefDictionaryValue>)> observer) {
  observer_ = std::move(observer);
  if (observer_) observer_(Snapshot());
}
void WorkspaceController::Publish() {
  ++revision_;
  UpdateMedia();
  const auto snapshot = Snapshot();
  if (auto state = state_.lock(); state && !state->protocols_probe.empty()) {
    auto data = CefDictionaryValue::Create(); data->SetBool("can_go_back", snapshot->GetBool("canGoBack"));
    state->Record("snapshot_published", data);
  }
  if (observer_) observer_(snapshot);
  if (auto state = state_.lock(); state && state->panel_popup) state->panel_popup->Publish();
  if (auto state = state_.lock(); state && state->sidebar_peek) state->sidebar_peek->Publish();
}
void WorkspaceController::PublishTheme() {
  ++revision_;
  const auto theme=ThemeSnapshot();
  if(observer_)observer_(theme->Copy(false));
  if(auto state=state_.lock()) {
    if(state->panel_popup)state->panel_popup->Publish(theme);
    if(state->sidebar_peek)state->sidebar_peek->Publish(theme);
  }
}
void WorkspaceController::PublishLayout() {
  ++revision_;
  const auto layout=LayoutSnapshot();
  if(observer_)observer_(layout->Copy(false));
  if(auto state=state_.lock(); state && state->sidebar_peek)state->sidebar_peek->Publish(layout);
}
CefRefPtr<CefDictionaryValue> WorkspaceController::LayoutSnapshot() const {
  const auto state=state_.lock();
  auto result=CefDictionaryValue::Create();
  result->SetBool("layoutOnly",true);result->SetDouble("revision",revision_);
  if (state && state->window) {
    const auto client = state->window->GetClientAreaBoundsInScreen();
    result->SetInt("sidebarWidth", SidebarWidth(state->sidebar_width, client.width));
    const auto area = WorkspaceContentArea(client.width, client.height, state->sidebar_visible, state->sidebar_width);
    auto bounds = CefDictionaryValue::Create();
    bounds->SetInt("x", area.x); bounds->SetInt("y", area.y); bounds->SetInt("width", area.width); bounds->SetInt("height", area.height);
    result->SetDictionary("contentBounds", bounds);
  }
  result->SetBool("sidebarResizing", state && state->sidebar_peek && state->sidebar_peek->is_resizing());
  if (state && state->devtools_dock) if (auto dock = state->devtools_dock->Describe()) result->SetDictionary("devTools", dock);
  if (state && state->devtools_dock) if (auto device = state->devtools_dock->DescribeDevice()) result->SetDictionary("devicePreview", device);
  return result;
}
CefRefPtr<CefDictionaryValue> WorkspaceController::ThemeSnapshot() const {
  const auto state=state_.lock();
  auto result=CefDictionaryValue::Create();
  result->SetBool("themeOnly",true);result->SetDouble("revision",revision_);
  result->SetString("activeSpace",tree_.active_space);
  if (state) {
    auto theme = CefDictionaryValue::Create();
    theme->SetInt("hue", state->theme.hue); theme->SetInt("intensity", state->theme.intensity);
    theme->SetString("mode", state->theme.mode == ThemeMode::Dark ? "dark" : state->theme.mode == ThemeMode::System ? "system" : "light");
    theme->SetInt("noise", state->theme.noise); theme->SetBool("dark", state->theme_dark);
    if (state->theme.palette) {
      const auto& p=*state->theme.palette;auto palette=CefDictionaryValue::Create();
      palette->SetInt("version",p.version);palette->SetInt("count",p.count);palette->SetInt("radius",p.radius);
      palette->SetInt("lightness",p.lightness);palette->SetInt("harmony",p.harmony);palette->SetInt("kind",p.kind);palette->SetInt("fill",p.fill);
      if(p.version==2){if(p.rotation!=0)palette->SetInt("rotation",p.rotation);auto circles=CefListValue::Create();for(size_t i=0;i<3;++i){auto c=CefDictionaryValue::Create();c->SetInt("hue",p.circles[i].hue);c->SetInt("distance",p.circles[i].distance);c->SetInt("size",p.circles[i].size);circles->SetDictionary(i,c);}palette->SetList("circles",circles);}
      theme->SetDictionary("palette",palette);
    }
    result->SetDictionary("theme", theme);
  }
  return result;
}
CefRefPtr<CefDictionaryValue> WorkspaceController::Snapshot() const {
  auto state = state_.lock();
  auto result = CefDictionaryValue::Create();
  result->SetInt("schemaVersion", 2); result->SetDouble("revision", revision_);
  result->SetDouble("treeRevision", static_cast<double>(tree_.revision));
  result->SetBool("workspaceEnabled", true); result->SetBool("ready", ready_);
  if (state && state->session && state->session->preferences) {
    result->SetString("searchEngine", state->session->preferences->Get().search_engine);
    result->SetBool("searchSuggestions", state->session->preferences->Get().search_suggestions);
    result->SetDictionary("shortcutLabels", ShortcutLabels(state->session->preferences->Get()));
  }
  result->SetString("windowKind", state && state->standalone_window ? "standalone" : "workspace");
  if (const auto update = ReadyUpdate(); !update.empty() && update.size() <= 40) result->SetString("updateReady", update);
  if (state && state->private_window) result->SetBool("privateWindow", true);
  if (state && state->translucent && state->backdrop != "none") result->SetString("backdrop", state->backdrop);
  // Pinned extensions for the top bar (none in a private window: no extensions run there).
  const auto active_profile = ProfileForNode("");
  const auto pins = active_profile ? active_profile->pins : session_ ? session_->pinned_extensions : nullptr;
  if (state && !state->private_window && state->extensions && pins)
    result->SetList("pinnedExtensions", pins->Items(*state->extensions, state->WebContext()));
  result->SetBool("windowClosing", shutting_down_);
  result->SetBool("sidebarVisible", !state || state->sidebar_visible);
  result->SetBool("sidebarResizing", state && state->sidebar_peek && state->sidebar_peek->is_resizing());
  result->SetInt("sidebarWidth", state && state->window ? SidebarWidth(state->sidebar_width,
      state->window->GetClientAreaBoundsInScreen().width) : kSidebarWidth);
  result->SetBool("windowMaximized", state && state->window && state->window->IsMaximized());
  result->SetBool("browserFullscreen",state && state->embedded && state->embedded->IsBrowserFullscreen());
  if (state && state->devtools_dock) if (auto dock = state->devtools_dock->Describe()) result->SetDictionary("devTools", dock);
  if (state && state->devtools_dock) if (auto device = state->devtools_dock->DescribeDevice()) result->SetDictionary("devicePreview", device);
  auto spaces = CefListValue::Create();
  if (!state || !state->standalone_window) for (const auto& space : tree_.spaces) {
    auto row = CefDictionaryValue::Create();
    row->SetString("id", space.id); row->SetString("name", space.name); row->SetString("icon", space.icon);
    row->SetString("profileId", space.profile);
    row->SetInt("position", space.position); row->SetInt("hue", space.theme_hue); row->SetInt("intensity", space.theme_intensity);
    spaces->SetDictionary(spaces->GetSize(), row);
  }
  result->SetList("spaces", spaces); result->SetString("activeSpace", tree_.active_space);
  result->SetString("activeProfile", state && state->standalone_window ? state->active_profile : BrowsingProfileId(tree_));
  auto profiles = CefListValue::Create();
  if (!state || !state->standalone_window) for (const auto& profile : tree_.profiles) {
    auto row = CefDictionaryValue::Create(); row->SetString("id",profile.id); row->SetString("name",profile.name);
    profiles->SetDictionary(profiles->GetSize(),row);
  }
  if (!state || !state->standalone_window) result->SetList("profiles",profiles);
  result->SetDictionary("theme", ThemeSnapshot()->GetDictionary("theme")->Copy(false));
  if (state && state->window) {
    const auto client = state->window->GetClientAreaBoundsInScreen();
    const auto area = WorkspaceContentArea(client.width, client.height, state->sidebar_visible, state->sidebar_width);
    auto bounds = CefDictionaryValue::Create();
    bounds->SetInt("x", area.x); bounds->SetInt("y", area.y);
    bounds->SetInt("width", area.width); bounds->SetInt("height", area.height);
    result->SetDictionary("contentBounds", bounds);
  }
  result->SetString("error", error_); result->SetString("activeId", tree_.active_id);
  result->SetString("notice", notice_); result->SetInt("noticeId", notice_id_);
  if (state && state->downloads) {
    result->SetList("downloads", state->downloads->List());
    result->SetInt("downloadsStarted", state->downloads->started());
  }
  if (state) result->SetInt("shellExtensions", state->shell_extensions);
  result->SetBool("probeMode", state && state->probe);
  result->SetBool("workspaceProbe", state && state->workspace_probe);
  result->SetBool("organizationProbe", state && state->organization_probe);
  result->SetBool("paletteProbe",state&&state->palette_probe);
  result->SetBool("browsingProbe", state && state->browsing_probe);
  result->SetBool("downloadsProbe", state && state->downloads_probe);
  if (state && state->lifecycle_probe) {
    result->SetString("lifecycleMode", state->lifecycle_mode);
    result->SetInt("lifecycleArmedCount", static_cast<int>(state->lifecycle_armed.size()));
    result->SetInt("lifecycleInspections", state->lifecycle_inspections);
    result->SetInt("windowCloseCancelled", state->window_close_cancelled);
  }
  result->SetBool("browsingPreserved", state && state->browsing_preserved);
  result->SetInt("popupCount", state ? static_cast<int>(state->popup_browsers.size()) : 0);
  result->SetBool("organizationArmed", state && state->organization_armed);
  result->SetInt("organizationUnloadDecisions", state ? state->organization_unload_decisions : 0);
  result->SetInt("newTabRequest", new_tab_request_);
  result->SetInt("addressRequest", address_request_);
  if (sidebar_request_) {
    auto request = CefDictionaryValue::Create(); request->SetInt("id", sidebar_request_); request->SetString("action", sidebar_action_);
    result->SetDictionary("sidebarRequest", request);
  }
  auto content = state ? state->content : nullptr;
  if (auto peek=DescribeLinkPeek()) result->SetDictionary("linkPeek",peek);
  if (auto media=DescribeMedia()) result->SetList("media",media);
  result->SetBool("canGoBack", content && content->CanGoBack());
  result->SetBool("canGoForward", content && content->CanGoForward());
  const bool crashed = content && crashed_.contains(content->GetIdentifier());
  result->SetBool("activeCrashed", crashed);
  result->SetBool("isLoading", content && !crashed && (content->IsLoading() || !loaded_.contains(content->GetIdentifier())));
  result->SetString("url", crashed ? last_urls_.at(content->GetIdentifier()) : content ? content->GetMainFrame()->GetURL().ToString() : HasLinkPeek() ? link_peek_url_ : "");
  // The active page's zoom when it is not 100% (the address bar's badge).
  if (content && !crashed) {
    const int zoom = static_cast<int>(std::lround(100 * std::pow(1.2, content->GetHost()->GetZoomLevel())));
    if (zoom != 100 && zoom > 0 && zoom <= 1000) result->SetInt("zoom", zoom);
  }
  auto nodes = CefListValue::Create();
  std::map<std::string,const SplitGroup*> split_members;
  const auto split_visible = OrderedTabsForSpace(tree_,tree_.active_space);
  for (const auto& split : tree_.splits) if (std::find(split_visible.begin(),split_visible.end(),split.anchor) != split_visible.end()) {
    split_members[split.anchor] = &split; split_members[split.other] = &split;
  }
  for (const auto& split:tree_.archived_splits) { split_members[split.anchor]=&split; split_members[split.other]=&split; }
  if (auto split = DescribeSplitView()) result->SetDictionary("splitView",split);
  for (size_t i = 0; i < tree_.nodes.size(); ++i) {
    const auto& node = tree_.nodes[i]; auto row = CefDictionaryValue::Create();
    if (node.favorite && node.profile != BrowsingProfileId(tree_)) continue;
    row->SetString("id", node.id); row->SetString("parentId", node.parent); row->SetString("space", node.space);
    row->SetString("kind", node.folder ? "folder" : "tab"); row->SetString("title", node.title);
    if (const auto member = split_members.find(node.id); member != split_members.end()) {
      row->SetString("splitAnchor",member->second->anchor); row->SetString("splitOther",member->second->other);
      row->SetString("splitTitle",member->second->title); row->SetBool("splitHidden",node.id != member->second->anchor && !node.favorite);
      row->SetBool("splitReversed",member->second->reversed);
    }
    row->SetString("url", node.url); row->SetInt("position", node.position);
    row->SetBool("pinned", node.pinned); row->SetBool("expanded", node.expanded);
    row->SetBool("favorite", node.favorite); row->SetBool("archived", node.archived); row->SetDouble("archivedAt", static_cast<double>(node.archived_at));
    const auto live = sessions_.find(node.id);
    const bool failed = live != sessions_.end() && crashed_.contains(live->second->GetIdentifier());
    const auto current = failed ? last_urls_.at(live->second->GetIdentifier()) :
        live != sessions_.end() ? live->second->GetMainFrame()->GetURL().ToString() : node.url;
    row->SetBool("crashed", failed);
    bool camera = false, microphone = false;  // Live camera/microphone use.
    if (live != sessions_.end() && state)
      if (const auto media = state->media_access.find(live->second->GetIdentifier()); media != state->media_access.end())
        std::tie(camera, microphone) = media->second;
    row->SetBool("mediaCamera", camera); row->SetBool("mediaMicrophone", microphone);
    row->SetBool("muted", live != sessions_.end() && live->second->GetHost()->IsAudioMuted());
    row->SetBool("audible", live != sessions_.end() && state && state->audible_frames.contains(live->second->GetIdentifier()));
    row->SetBool("customTitle", node.custom_title);
    const auto title = page_titles_.find(node.id);
    if (live != sessions_.end() && title != page_titles_.end() && title->second.url == current)
      row->SetString("pageTitle", title->second.text);
    else if (current.empty() || current == "about:blank" || SamePageUrl(current, node.url))
      row->SetString("pageTitle", node.page_title);
    // Live documents have their own selection. Only saved or not-yet-committed
    // tabs use the host fallback directly.
    const auto profile = ProfileForNode(node.id);
    const auto favicons = profile ? profile->favicons : state ? state->favicons : nullptr;
    if (favicons) {
      std::optional<std::string> icon;
      if (live != sessions_.end() && !failed) {
        icon = favicons->PageUrl(live->second->GetIdentifier());
        if (!icon && (current.empty() || current == "about:blank")) icon = favicons->Url(IconHost(node.url));
      } else {
        icon = favicons->Url(IconHost(node.url));
      }
      if (icon) row->SetString("favicon", *icon);
    }
    row->SetString("currentUrl", current);
    // Still "closing" while its archive is being saved, so the row never
    // reappears between the page closing and the archived snapshot.
    row->SetBool("closing", closing_.contains(node.id) || archiving_.contains(node.id) || auto_closing_.contains(node.id) ||
      (session_ && session_->auto_archive && session_->auto_archive->Reserved(node.id)));
    // Arc: a tab with a Link Peek (shown or parked) carries the preview's icon.
    const auto parked = parked_peeks_.find(node.id);
    const std::string peek = node.id == link_peek_opener_ && HasLinkPeek() ? link_peek_id_ : parked != parked_peeks_.end() ? parked->second.id : "";
    if (const auto preview = peek.empty() ? nullptr : LiveBrowser(peek)) {
      const auto peek_title = page_titles_.find(peek); row->SetString("peekTitle", peek_title != page_titles_.end() ? peek_title->second.text : "");
      const auto peek_profile = ProfileForBrowser(state,preview);
      const auto peek_icons = peek_profile ? peek_profile->favicons : favicons;
      if (peek_icons) if (const auto url = peek_icons->PageUrl(preview->GetIdentifier())) row->SetString("peekFavicon", *url);
      row->SetBool("peek", true);
    }
    row->SetBool("live", sessions_.contains(node.id)); nodes->SetDictionary(nodes->GetSize(), row);
  }
  result->SetList("nodes", nodes);
  return result;
}
void WorkspaceController::ReceiveSharedTree(const TreeSnapshot& incoming) {
  if (stopped_ || shutting_down_) return;
  TreeResult result; result.snapshot = incoming;
  if (ready_ || (state_.lock() && state_.lock()->secondary_window)) {
    KeepWindowSelection(result.snapshot, tree_);
  }
  if (!ready_) if (auto state = state_.lock(); state && !state->initial_space.empty())
    if (std::any_of(incoming.spaces.begin(), incoming.spaces.end(), [&](const auto& s) { return s.id == state->initial_space; }))
      result.snapshot.active_space = state->initial_space;
  if (!ready_) if (auto state = state_.lock(); state && !state->initial_tab.empty()) {
    const auto visible = OrderedTabsForSpace(incoming, result.snapshot.active_space);
    if (std::find(visible.begin(), visible.end(), state->initial_tab) != visible.end()) result.snapshot.active_id = state->initial_tab;
    else
    for (const auto& space : incoming.spaces) {
      const auto tabs = OrderedTabsForSpace(incoming, space.id);
      if (std::find(tabs.begin(), tabs.end(), state->initial_tab) != tabs.end()) {
        result.snapshot.active_space = space.id; result.snapshot.active_id = state->initial_tab; break;
      }
    }
  }
  Complete(std::move(result), {}, true);
}
void WorkspaceController::CompleteMetadata(TreeResult result, Reply done) {
  if (result) {
    session_->PublishTree(data_, result.snapshot, this);
    KeepWindowSelection(result.snapshot, tree_);
  }
  Complete(std::move(result), std::move(done), true);
}
void WorkspaceController::Complete(TreeResult result, Reply done, bool shared) {
  if (stopped_) return;
  if (!result) {
    if (!done) { error_ = result.error; Publish(); }
    else done(result.code, result.error, nullptr);
    return;
  }
  const bool initial = !ready_;
  if (!shared && session_) session_->PublishTree(data_, result.snapshot, this);
  const auto previous_space = tree_.ActiveSpace() ? *tree_.ActiveSpace() : Space{};
  if (!ready_ || result.snapshot.revision >= tree_.revision) tree_ = std::move(result.snapshot);
  if (const auto state = state_.lock(); state && !state->standalone_window && !SelectBrowsingProfile(state, BrowsingProfileId(tree_))) {
    error_ = "Cannot open this browsing profile"; Publish();
    if (done) done(500,error_,nullptr);
    return;
  }
  if (initial) if (auto state = state_.lock()) state->sidebar_width = tree_.sidebar_width;
  // Each space has its own theme: apply it on start and when the space changes.
  if (const auto* space = tree_.ActiveSpace(); space && (!preserve_theme_preview_ || space->id!=themed_space_) && (initial || space->id != themed_space_ ||
      std::tie(space->theme_hue, space->theme_intensity, space->theme_mode, space->theme_noise,space->theme_palette) !=
      std::tie(previous_space.theme_hue, previous_space.theme_intensity, previous_space.theme_mode, previous_space.theme_noise,previous_space.theme_palette))) if (auto state = state_.lock()) {
    themed_space_ = space->id;
    state->theme = {space->theme_hue, space->theme_intensity, static_cast<ThemeMode>(space->theme_mode), space->theme_noise,space->theme_palette};
    ApplyTheme(state); RememberTheme(state);
  }
  // Pages whose entries are gone (a deleted space) close without saving.
  std::vector<CefRefPtr<CefBrowser>> orphaned;
  for (const auto& [id, browser] : sessions_)
    if (!IsPeekPage(id) && !closing_.contains(id) && std::none_of(tree_.nodes.begin(), tree_.nodes.end(), [&](const auto& n) { return n.id == id; }))
      orphaned.push_back(browser);
  for (auto& browser : orphaned) CloseBrowserWithDialogs(state_, browser, true);
  // A parked preview goes with its source tab (closed, archived, deleted).
  std::vector<std::string> abandoned;
  for (const auto& [source, parked] : parked_peeks_)
    if (std::none_of(tree_.nodes.begin(), tree_.nodes.end(), [&](const auto& n) { return n.id == source && !n.archived; })) abandoned.push_back(source);
  for (const auto& source : abandoned) CloseParkedPeek(source);
  // A cold start loads no saved page: every startup mode begins on the empty tab ("restore" keeps
  // the sidebar and the space on screen; pages load when selected).
  if (initial) if (const auto state = state_.lock(); state && !state->probe && !state->secondary_window) tree_.active_id.clear();
  ready_ = true; error_.clear(); ApplyStartup(); EnsureActive(); Publish();
  if (initial) if (auto state = state_.lock()) {
    RefreshShortcuts(state);
    SetSidebarVisible(state->sidebar_visible);
    if (state->sidebar_peek) state->sidebar_peek->Preload();
  }
  if (done) { auto snapshot = Snapshot(); snapshot->SetString("createdId", result.created_id); done(0, "", snapshot); }
  DrainExternalUrls();
}
void WorkspaceController::ApplyStartup() {
  const auto state = state_.lock();
  if (startup_applied_ || !ready_ || !state || !state->embedded || !state->window || !session_ || !session_->preferences) return;
  startup_applied_ = true;
  if (state->probe || state->secondary_window || state->standalone_window || !external_pending_.empty()) return;
  const auto& preferences = session_->preferences->Get();
  if (preferences.startup == "newTab") { tree_.active_id.clear(); OpenNewTab(); }
  else if (preferences.startup == "homepage" && UrlAllowed(preferences.homepage)) {
    tree_.active_id.clear();
    const auto visible = OrderedTabsForSpace(tree_,tree_.active_space);
    for (const auto& node : tree_.nodes) if (node.url == preferences.homepage &&
        std::find(visible.begin(),visible.end(),node.id) != visible.end()) {
      TreeCommand activate{TreeAction::Activate}; activate.id = node.id; Dispatch(activate,{},true); return;
    }
    OpenLink(preferences.homepage,true);
  }
}
void WorkspaceController::ImportZen(const TreeSnapshot& incoming, Reply done) {
  auto state = state_.lock();
  if (state && state->standalone_window) { done(403, "Import requires a workspace window", nullptr); return; }
  if (!ready_ || stopped_ || shutting_down_ || !state || !worker_) { done(409, "Workspace is not ready", nullptr); return; }
  const auto stamp = std::to_string(GetTickCount64());
  const auto backup = state->profile / "BrowserData" / "import-backups" / stamp / "workspace.sqlite3";
  ++pending_commands_;
  worker_->GetTaskRunner()->PostTask(CefCreateClosureTask(base::BindOnce([](std::shared_ptr<WorkspaceController> self, TreeSnapshot incoming, std::filesystem::path backup, Reply done) {
    auto result = self->data_->store->Import(incoming, backup);
    CefPostTask(TID_UI, base::BindOnce([](std::shared_ptr<WorkspaceController> self, TreeResult result, Reply done) {
      --self->pending_commands_; self->Complete(std::move(result), std::move(done));
    }, self, std::move(result), std::move(done)));
  }, shared_from_this(), incoming, backup, std::move(done))));
}
void WorkspaceController::Dispatch(TreeCommand command, Reply done, bool internal) {
  if (command.action == TreeAction::SwitchSpace || command.action == TreeAction::DeleteSpace) {
    // A space opens on a page that is already loaded, or on the empty tab: after a cold start
    // nothing is loaded from the saved sidebar until you select it.
    command.live_nodes.emplace();
    for (const auto& [id, browser] : sessions_) if (!closing_.contains(id)) command.live_nodes->push_back(id);
  }
  const bool leaving=command.action==TreeAction::Activate || command.action==TreeAction::SwitchSpace || command.action==TreeAction::CreateSpace ||
    command.action==TreeAction::Restore || command.action==TreeAction::DeleteSpace || (command.action==TreeAction::CreateTab && command.value);
  if (HasLinkPeek() && leaving && !(link_peek_promoting_ && command.action==TreeAction::CreateTab && command.id==link_peek_id_)) {
    // Selecting the preview's own source tab keeps everything as it is.
    if (command.action==TreeAction::Activate && command.id==link_peek_opener_) { if (done) done(0,"",Snapshot()); return; }
  }
  // Leaving the source tab parks its preview with it; it returns with the tab.
  if (HasLinkPeek() && leaving && !(link_peek_promoting_ && command.action==TreeAction::CreateTab && command.id==link_peek_id_) && !ParkLinkPeek()) {
    DismissLinkPeek([weak=weak_from_this(),command,done,internal](bool closed) {
      if (auto self=weak.lock()) { if (closed) self->Dispatch(command,done,internal); else if (done) done(409,"Close cancelled; preview kept",self->Snapshot()); }
    }); return;
  }
  if (command.action == TreeAction::Activate) for (const auto& split : tree_.splits) if (split.anchor == command.id) {
    const auto remembered = split_focus_.find(split.anchor);
    if (remembered != split_focus_.end() && (remembered->second == split.anchor || remembered->second == split.other)) command.id = remembered->second;
    break;
  }
  const auto selection_started = command.action == TreeAction::Activate ? GetTickCount64() : 0;
  if (selection_started) if (auto s = state_.lock()) s->Record("tab_switch_requested");
  if (!command.url.empty()) {
    const auto title = page_titles_.find(command.id);
    if (title != page_titles_.end() && SamePageUrl(title->second.url, command.url)) command.page_title = title->second.text;
  }
  ++pending_commands_;
  worker_->GetTaskRunner()->PostTask(CefCreateClosureTask(base::BindOnce(
      [](std::shared_ptr<WorkspaceController> self, TreeCommand command, Reply done, bool internal, unsigned long long selection_started, TreeSnapshot window) {
        if (internal) command.expected_revision = self->data_->store->Read().snapshot.revision;
        auto result = self->data_->store->Apply(command, &window);
        CefPostTask(TID_UI, base::BindOnce([](std::weak_ptr<WorkspaceController> weak, TreeResult result, Reply done,
            unsigned long long selection_started, std::string changed_destination) {
          if (auto owner = weak.lock()) {
            if (selection_started) if (auto s = owner->state_.lock()) {
              auto details = CefDictionaryValue::Create();
              details->SetDouble("commit_ms", static_cast<double>(GetTickCount64() - selection_started));
              details->SetBool("success", static_cast<bool>(result)); s->Record("tab_switch_committed", details);
            }
            const bool saved = static_cast<bool>(result);
            --owner->pending_commands_; owner->Complete(std::move(result), std::move(done));
            if (saved && !changed_destination.empty()) owner->SavePageTitle(changed_destination);
          }
        }, std::weak_ptr<WorkspaceController>(self), std::move(result), std::move(done), selection_started,
            command.url.empty() ? std::string{} : command.id));
      }, shared_from_this(), std::move(command), std::move(done), internal, selection_started, tree_)));
}
CefRefPtr<CefBrowser> WorkspaceController::LiveBrowser(const std::string& id) const {
  const auto found = sessions_.find(id); return found == sessions_.end() ? nullptr : found->second;
}
std::shared_ptr<BrowsingProfile> WorkspaceController::ProfileForNode(const std::string& id) const {
  const auto state = state_.lock();
  if (!state || state->private_window || !session_ || !session_->profiles) return nullptr;
  if (IsPeekPage(id)) {
    if (const auto opener = LiveBrowser(link_peek_opener_)) return ProfileForBrowser(state,opener);
  }
  return session_->profiles->Get(state, state->standalone_window ? state->active_profile : BrowsingProfileId(tree_,id));
}
bool WorkspaceController::MoveToNewWindow(const std::string& id) {
  if (std::any_of(tree_.splits.begin(),tree_.splits.end(),[&](const auto& split) { return split.anchor == id; })) return false;
  auto state = state_.lock();
  const auto node = std::find_if(tree_.nodes.begin(), tree_.nodes.end(), [&](const auto& n) { return n.id == id && !n.folder && !n.archived; });
  if (!state || !CanOpen() || node == tree_.nodes.end() || !session_ || !session_->new_window) return false;
  return session_->new_window(state, id);
}
std::optional<TreeNode> WorkspaceController::DetachedSeed(const std::string& id) const {
  const auto found = std::find_if(tree_.nodes.begin(), tree_.nodes.end(), [&](const auto& n) { return n.id == id && !n.folder && !n.archived; });
  if (found == tree_.nodes.end()) return std::nullopt;
  auto seed = *found;
  if (auto browser = LiveBrowser(id)) {
    const auto url = browser->GetMainFrame()->GetURL().ToString();
    if (PageUrlAllowed(url)) seed.url = url;
  }
  return seed;
}
void WorkspaceController::DetachedReady(const std::string& id) {
  const auto seed = DetachedSeed(id);
  if (!seed || seed->pinned || seed->favorite || shutting_down_) return;
  detached_removals_.insert(id);
  if (LiveBrowser(id) || creating_.contains(id)) UnloadForMove(id);
  else { detached_removals_.erase(id); RemoveDetachedToday(id); }
}
void WorkspaceController::RemoveDetachedToday(const std::string& id) {
  const auto seed = DetachedSeed(id);
  if (!seed || seed->pinned || seed->favorite || shutting_down_) return;
  TreeCommand remove{TreeAction::RemoveLeaf}; remove.id = id;
  Dispatch(std::move(remove), {}, true);
}
void WorkspaceController::OpenDetachedCopy(const TreeNode& seed, std::function<void()> ready) {
  if (!CanOpen() || !PageUrlAllowed(seed.url)) return;
  OpenPage(seed.url, seed.title, true, [weak = weak_from_this(), ready = std::move(ready), title = seed.title, custom = seed.custom_title](int code, const std::string&, CefRefPtr<CefDictionaryValue> snapshot) mutable {
    if (auto self = weak.lock(); self && !code && snapshot) {
      const auto id = snapshot->GetString("createdId").ToString();
      if (custom) { TreeCommand rename{TreeAction::Rename}; rename.id = id; rename.title = title; self->Dispatch(std::move(rename), {}, true); }
      if (self->LiveBrowser(id)) { if (ready) ready(); }
      else self->tab_ready_[id] = std::move(ready);
    }
  });
}
void WorkspaceController::UnloadForMove(const std::string& id) {
  const auto browser = LiveBrowser(id);
  if (!browser && !creating_.contains(id)) return;
  moving_unloads_.insert(id);
  if (tree_.active_id == id) {
    tree_.active_id.clear();
    for (const auto& candidate : OrderedTabsForSpace(tree_, tree_.active_space))
      if (candidate != id && sessions_.contains(candidate) && !moving_unloads_.contains(candidate)) { tree_.active_id = candidate; break; }
    for (auto& space : tree_.spaces) if (space.id == tree_.active_space) space.active_id = tree_.active_id;
  }
  EnsureActive(); Publish();
  if (browser) CloseBrowserWithDialogs(state_, browser, false);
}
void WorkspaceController::OpenMovedTab(const std::string& id, const std::string& url) {
  if (!CanOpen() || !PageUrlAllowed(url)) return;
  next_urls_[id] = url;
  UnloadForMove(id);
  TreeCommand activate{TreeAction::Activate}; activate.id = id;
  Dispatch(std::move(activate), {}, true);
}
bool WorkspaceController::CanTransfer(const std::string& id) const {
  if (profile_changing_) return false;
  if (removing_folder_nodes_.contains(id)) return false;
  const auto browser = LiveBrowser(id);
  return browser && !shutting_down_ && !creating_.contains(id) && !closing_.contains(id) && !moving_unloads_.contains(id) &&
      !archiving_.contains(id) && !auto_closing_.contains(id) && !IsPeekPage(id) &&
      !(HasLinkPeek() && link_peek_opener_ == id && (link_peek_closing_ || link_peek_promoting_)) && !crashed_.contains(browser->GetIdentifier()) &&
      std::none_of(tree_.splits.begin(), tree_.splits.end(), [&](const auto& split) { return split.anchor == id || split.other == id; });
}
CefRefPtr<CefBrowser> WorkspaceController::ReleaseForTransfer(const std::string& id, bool remove_entry) {
  if (!CanTransfer(id)) return nullptr;
  const auto browser = LiveBrowser(id);
  const int browser_id = browser->GetIdentifier();
  // The page leaves without unloading: forget it as OnClosed would, then show
  // another tab here.
  loaded_.erase(browser_id); last_urls_.erase(browser_id);
  sessions_.erase(id); browser_nodes_.erase(browser_id); std::erase(recent_, id);
  auto_archive_media_.erase(browser_id);
  page_titles_.erase(id);
  if (tree_.active_id == id) {
    tree_.active_id.clear();
    for (const auto& candidate : OrderedTabsForSpace(tree_, tree_.active_space))
      if (candidate != id && sessions_.contains(candidate) && !moving_unloads_.contains(candidate)) { tree_.active_id = candidate; break; }
    for (auto& space : tree_.spaces) if (space.id == tree_.active_space) space.active_id = tree_.active_id;
  }
  if (remove_entry) RemoveDetachedToday(id);
  EnsureActive(); Publish();
  return browser;
}
bool WorkspaceController::AdoptPage(CefRefPtr<CefBrowser> browser, const std::string& node_id) {
  const auto node = std::find_if(tree_.nodes.begin(), tree_.nodes.end(), [&](const auto& n) { return n.id == node_id && !n.folder && !n.archived; });
  if (!browser || shutting_down_ || node == tree_.nodes.end() || sessions_.contains(node_id)) return false;
  creating_.erase(node_id);
  sessions_[node_id] = browser; browser_nodes_[browser->GetIdentifier()] = node_id;
  if (!browser->IsLoading()) loaded_.insert(browser->GetIdentifier());
  OnTitleChanged(browser);
  TreeCommand activate{TreeAction::Activate}; activate.id = node_id;
  Dispatch(std::move(activate), {}, true);
  EnsureActive(); Publish();
  return true;
}
void WorkspaceController::HandleTabDrag(CefRefPtr<CefDictionaryValue> request, Reply done) {
  const auto state = state_.lock();
  const auto method = request->GetString("method");
  const auto start = method == "tab.dragStarted";
  const auto previous_type = request->GetType("previousId");
  if (!state || request->GetSize() != (start ? 3U : 2U) || request->GetType("id") != VTYPE_STRING ||
      request->GetString("id").length() != 36 ||
      (start && previous_type != VTYPE_NULL && (previous_type != VTYPE_STRING || request->GetString("previousId").length() != 36))) {
    done(400, "Invalid tab command", nullptr);
    return;
  }
  const auto id = request->GetString("id").ToString();
  if (start) {
    tab_drag_ = TabDrag{id, previous_type == VTYPE_STRING ? request->GetString("previousId").ToString() : "", tree_.active_space};
    state->tab_dragging = true;
    if (state->organization_probe) {
      auto details = CefDictionaryValue::Create();
      details->SetString("id", id);
      details->SetString("previous", tab_drag_->previous);
      details->SetString("active", tree_.active_id);
      state->Record("organization_tab_drag", details);
    }
    ShowTabDragPreview(state, id);
  } else if (method == "tab.dragEnded" || method == "tab.dragCancel") {
    FinishTabDrag(id, method == "tab.dragEnded");
  } else if (!MoveToNewWindow(id)) {
    done(409, "Tab cannot move right now", nullptr);
    return;
  }
  done(0, "", Snapshot());
}
void WorkspaceController::FinishTabDrag(const std::string& id, bool dropped) {
  const auto state = state_.lock();
  if (!state || !state->tab_dragging || !tab_drag_ || tab_drag_->id != id) return;
  POINT cursor{};
  const bool released = dropped && state->window && session_ && !(GetAsyncKeyState(VK_ESCAPE) & 0x8000) &&
    !PrimaryButtonDown() && GetCursorPos(&cursor);
  const auto split = released ? SplitDropAt(id, cursor) : std::nullopt;
  state->tab_dragging = false;
  tab_drag_.reset();
  HideTabDragPreview(state);
  if (!released) return;
  if (split) { Dispatch(split->command,{},true); return; }
  RECT source{}; if (!GetWindowRect(state->window->GetWindowHandle(), &source)) return;
  InflateRect(&source, 12, 12);
  if (PtInRect(&source, cursor)) return;
  // A split is a composite sidebar item. Never tear out just its anchor page.
  if (std::any_of(tree_.splits.begin(),tree_.splits.end(),[&](const auto& split) { return split.anchor == id; })) return;
  const HWND hovered = GetAncestor(WindowFromPoint(cursor), GA_ROOT);
  for (const auto& target : session_->Windows()) {
    if (target == state || !target->window || !target->workspace || target->workspace->WindowClosing()) continue;
    if (hovered == target->window->GetWindowHandle()) {
      const auto browser = LiveBrowser(id);
      const auto node = std::find_if(tree_.nodes.begin(), tree_.nodes.end(), [&](const auto& n) { return n.id == id; });
      if (node == tree_.nodes.end()) return;
      if (target->workspace_data == state->workspace_data) {
        // Same data: the entry is already in the target window; its live page
        // moves there as it is (Pinned/Favorites stay listed here, unloaded).
        if (browser && session_->transfer_page && session_->transfer_page(state, target, id, id, false)) return;
        target->workspace->OpenMovedTab(id, browser ? browser->GetMainFrame()->GetURL().ToString() : node->url);
        if (!node->pinned && !node->favorite) UnloadForMove(id);
      } else if (const auto seed = DetachedSeed(id)) {
        std::function<void()> ready;
        if (!seed->pinned && !seed->favorite) ready = [weak = weak_from_this(), id] { if (auto source = weak.lock()) source->DetachedReady(id); };
        target->workspace->OpenDetachedCopy(*seed, std::move(ready));
      }
      return;
    }
  }
  MoveToNewWindow(id);
}
void WorkspaceController::EnsureActive() {
  if (profile_changing_) return;
  auto state = state_.lock();
  if (!state || !state->embedded || shutting_down_) return;
  if (HasLinkPeek()) { if (state->embedded) state->embedded->RaiseActive(); return; }
  if (session_ && session_->auto_archive && !state->standalone_window && state->window && !state->window->IsMinimized() && IsWindowVisible(state->window->GetWindowHandle())) {
    session_->auto_archive->Activity(tree_.active_id);
    if (const auto* split=SelectedSplit()) { session_->auto_archive->Activity(split->anchor); session_->auto_archive->Activity(split->other); }
  }
  if (const auto* split = SelectedSplit()) {
    split_focus_[split->anchor] = tree_.active_id;
    if (expanded_split_pane_ != split->anchor && expanded_split_pane_ != split->other) expanded_split_pane_.clear();
    EnsurePage(split->anchor); EnsurePage(split->other);
    const auto first = LiveBrowser(split->anchor), second = LiveBrowser(split->other);
    const int a = first ? first->GetIdentifier() : -1, b = second ? second->GetIdentifier() : -1;
    const auto expanded = LiveBrowser(expanded_split_pane_);
    state->embedded->SetSplitPages(a,b,split->vertical,split_preview_ && split_preview_->first == split->anchor ? split_preview_->second : split->ratio,
      split->reversed,expanded_split_pane_.empty() ? -1 : expanded ? expanded->GetIdentifier() : -2,crashed_);
    auto focus = LiveBrowser(tree_.active_id); if (!focus) focus = first ? first : second;
    if (focus) state->embedded->Activate(focus);
    if (focus) { std::erase(recent_,tree_.active_id); recent_.push_back(tree_.active_id); }
    return;
  }
  expanded_split_pane_.clear(); split_preview_.reset();
  const bool was_split = state->embedded->HasSplit();
  state->embedded->SetSplitPages(-1,-1,false,5000,false);
  if (tree_.active_id.empty()) { state->embedded->HideAll(); return; }
  if (closing_.contains(tree_.active_id) || moving_unloads_.contains(tree_.active_id)) return;
  const auto live = sessions_.find(tree_.active_id);
  if (live != sessions_.end()) {
    if (crashed_.contains(live->second->GetIdentifier())) { state->embedded->PresentCrash(live->second); return; }
    // Separate can keep the focused pane selected. Apply the single-page layout
    // even if that browser is already visible.
    if (was_split || state->embedded->HasPendingColdTab() || state->content != live->second ||
        !IsWindowVisible(live->second->GetHost()->GetWindowHandle())) {
      state->embedded->Activate(live->second, !loaded_.contains(live->second->GetIdentifier()));
      std::erase(recent_, tree_.active_id); recent_.push_back(tree_.active_id);
    }
    ResumeLinkPeek();
    return;
  }
  EnsurePage(tree_.active_id);
  if (creating_.contains(tree_.active_id)) state->embedded->ShowPendingColdTab();
  else state->embedded->CancelPendingColdTab();
}
void WorkspaceController::EnsurePage(const std::string& id) {
  if (profile_changing_) return;
  if (removing_folder_nodes_.contains(id)) return;
  if (session_) for (const auto& window:session_->Windows())
    if (window->workspace && window->workspace->data_==data_ && window->workspace->removing_folder_nodes_.contains(id)) return;
  if (session_ && session_->auto_archive && session_->auto_archive->Reserved(id)) return;
  const auto state = state_.lock(); if (!state || !state->embedded || sessions_.contains(id) || creating_.contains(id) ||
      closing_.contains(id) || moving_unloads_.contains(id) || archiving_.contains(id)) return;
  for (const auto& node : tree_.nodes) if (node.id == id && !node.folder && !node.archived) {
    if (!PageUrlAllowed(node.url)) { error_ = "Saved URL is not allowed"; return; }
    creating_.insert(node.id);
    std::string url = node.url;
    if (auto pending = next_urls_.find(node.id); pending != next_urls_.end()) { url = std::move(pending->second); next_urls_.erase(pending); }
    if (state->initial_tab == node.id && !state->initial_page_url.empty()) { url = std::move(state->initial_page_url); state->initial_page_url.clear(); }
    // A window opened for a tab moved live takes that page instead of loading.
    if (state->adopt_page) {
      auto adopt = std::move(state->adopt_page); state->adopt_page = {};
      creating_.erase(node.id);
      if (adopt(node.id)) return;
      creating_.insert(node.id);
    }
    if (!CreateLive(url, node.id)) { creating_.erase(node.id); error_ = "Cannot create page"; }
    return;
  }
}
bool WorkspaceController::CreateLive(const std::string& url, const std::string& id) {
  const auto state = state_.lock(); if (!state || !state->embedded) return false;
#if CEF_API_ADDED(CEF_EXPERIMENTAL)
  if (auto by_url = link_initiator_by_url_.find(url); by_url != link_initiator_by_url_.end() && !link_initiators_.contains(id)) {
    link_initiators_[id] = {url, by_url->second}; link_initiator_by_url_.erase(by_url);
  }
  if (auto link = link_initiators_.find(id); link != link_initiators_.end()) {
    link->second.first = url;
    if (state->embedded->Create("", id)) return true;  // Empty: no navigation until OnCreated.
    link_initiators_.erase(link); return false;
  }
#endif
  return state->embedded->Create(url, id);
}
void WorkspaceController::OnCreated(CefRefPtr<CefBrowser> browser, const std::string& node_id) {
  if (!creating_.erase(node_id)) { link_initiators_.erase(node_id); browser->GetHost()->CloseBrowser(false); return; }
  sessions_[node_id] = browser; browser_nodes_[browser->GetIdentifier()] = node_id;
#if CEF_API_ADDED(CEF_EXPERIMENTAL)
  if (auto link = link_initiators_.find(node_id); link != link_initiators_.end()) {
    const auto [url, initiator] = link->second; link_initiators_.erase(link);
    browser->GetHost()->LoadURLFromInitiator(url, initiator);
  }
#endif
  if (node_id==link_peek_id_) {
    CefPostTask(TID_UI,base::BindOnce([](std::weak_ptr<WorkspaceController> weak,CefRefPtr<CefBrowser> browser,std::string id) {
      if (auto self=weak.lock(); self && self->link_peek_id_==id) {
        if (self->link_peek_closing_ || self->shutting_down_) { browser->GetHost()->CloseBrowser(false); return; }
        if (const auto state=self->state_.lock(); state && state->embedded) state->embedded->SetLinkPeek(browser->GetIdentifier());
#if CEF_API_ADDED(CEF_EXPERIMENTAL)
        browser->GetHost()->NotifyTabActivated();
#endif
        FocusBrowser(browser); self->Publish();
      }
    },weak_from_this(),browser,node_id)); return;
  }
  if (moving_unloads_.contains(node_id)) {
    CefPostTask(TID_UI, base::BindOnce([](CefRefPtr<CefBrowser> page) { page->GetHost()->CloseBrowser(false); }, browser));
  }
  // Chrome's child window shows itself after OnAfterCreated returns. Let that
  // finish at the parked bounds before positioning/stacking it for presentation.
  // Resolve the current selection in the task, not the one that began creation.
  CefPostTask(TID_UI, base::BindOnce([](std::weak_ptr<WorkspaceController> weak) {
    if (auto self = weak.lock(); self && !self->stopped_) {
      self->EnsureActive(); self->Publish();
      if (auto state = self->state_.lock(); state && state->window && !self->shutting_down_ && state->detached_ready) {
        auto ready = std::move(state->detached_ready); state->detached_ready = {}; ready();
      }
      for (auto it = self->tab_ready_.begin(); it != self->tab_ready_.end();) {
        if (self->LiveBrowser(it->first)) {
          auto ready = std::move(it->second); it = self->tab_ready_.erase(it); if (ready) ready();
        } else ++it;
      }
      if (self->session_) for (const auto& state : self->session_->Windows())
        if (state->workspace_data == self->data_ && state->workspace && state->workspace.get() != self.get()) state->workspace->AttachContent();
    }
  }, weak_from_this()));
}
void WorkspaceController::OnClosed(int browser_id, const std::string& last_url) {
  if (const auto state = state_.lock()) {
    const auto profile = state->browser_profiles.find(browser_id);
    const auto icons = profile == state->browser_profiles.end() ? state->favicons : profile->second->favicons;
    if (icons) icons->ForgetPage(browser_id);
  }
  const auto binding = browser_nodes_.find(browser_id);
  if (binding == browser_nodes_.end()) return;
  const auto id = binding->second;
  loaded_.erase(browser_id);
  crashed_.erase(browser_id); last_urls_.erase(browser_id);
  sessions_.erase(id); browser_nodes_.erase(binding); std::erase(recent_, id);
  auto_archive_media_.erase(browser_id);
  if (closing_peeks_.erase(id) || std::erase_if(parked_peeks_, [&](const auto& entry) { return entry.second.id == id; })) {
    page_titles_.erase(id); Publish(); return;
  }
  if (id==link_peek_id_) {
    const auto state=state_.lock(); const int delay=state && state->embedded && !shutting_down_ ? state->embedded->LinkPeekReturnDelay() : 0;
    if (delay) {
      link_peek_closing_=true; Publish();
      CefPostDelayedTask(TID_UI,base::BindOnce([](std::weak_ptr<WorkspaceController> weak,std::string id) {
        if (const auto self=weak.lock(); self && self->link_peek_id_==id) self->FinishLinkPeekClose();
      },weak_from_this(),id),delay);
    } else FinishLinkPeekClose();
    return;
  }
  auto& unload_callbacks=profile_closing_.contains(id)?profile_closing_:auto_closing_;
  if (const auto automatic=unload_callbacks.find(id); automatic!=unload_callbacks.end()) {
    auto done=std::move(automatic->second); unload_callbacks.erase(automatic);
    if (shutting_down_ || !PageUrlAllowed(last_url)) { page_titles_.erase(id); done(false); return; }
    // Pinned/Favorites keep their saved address (unloaded only while on it).
    if (const auto node=std::find_if(tree_.nodes.begin(),tree_.nodes.end(),[&](const auto& n) { return n.id==id; });
        node!=tree_.nodes.end() && (node->pinned || node->favorite)) { page_titles_.erase(id); done(true); Publish(); return; }
    TreeCommand update{TreeAction::SetUrl}; update.id=id; update.url=last_url;
    Dispatch(update,[done=std::move(done)](int code,const std::string&,CefRefPtr<CefDictionaryValue>) { done(code==0); },true);
    page_titles_.erase(id);
    Publish(); return;
  }
  if (moving_unloads_.erase(id)) {
    page_titles_.erase(id);
    if (detached_removals_.erase(id)) RemoveDetachedToday(id);
    EnsureActive(); Publish(); return;
  }
  if (shutting_down_) { page_titles_.erase(id); closing_.erase(id); Publish(); return; }
  auto pending = closing_.find(id);
  if (pending != closing_.end()) {
    auto action = std::move(pending->second); closing_.erase(pending);
    FinishClose(id, action.first, std::move(action.second), last_url);
  } else {
    const auto node = std::find_if(tree_.nodes.begin(), tree_.nodes.end(), [&](const auto& n) { return n.id == id; });
    if (node != tree_.nodes.end()) FinishClose(id, !node->pinned, {}, last_url);
  }
  page_titles_.erase(id);
  Publish();
}
void WorkspaceController::Close(const std::string& id, bool archive, Reply done) {
  if (HasLinkPeek()) {
    DismissLinkPeek([weak=weak_from_this(),id,archive,done](bool closed) {
      if (auto self=weak.lock()) { if (closed) self->Close(id,archive,done); else if (done) done(409,"Close cancelled; preview kept",self->Snapshot()); }
    }); return;
  }
  const auto node = std::find_if(tree_.nodes.begin(), tree_.nodes.end(), [&](const auto& n) { return n.id == id; });
  if (!ready_ || shutting_down_ || node == tree_.nodes.end() || node->folder || node->archived || creating_.contains(id) || !closing_.empty() || closing_commits_) {
    if (done) done(409, "Page cannot close in its current state", nullptr); return;
  }
  const auto live = sessions_.find(id);
  if (live == sessions_.end()) { FinishClose(id, archive || !node->pinned, std::move(done)); return; }
  closing_[id] = {archive || !node->pinned, std::move(done)}; Publish();
  CloseBrowserWithDialogs(state_, live->second, false);
}
void WorkspaceController::CancelClose(int browser_id) {
  const auto binding = browser_nodes_.find(browser_id);
  if (binding == browser_nodes_.end()) return;
  if (binding->second==link_peek_id_) {
    if (const auto state=state_.lock(); state && state->embedded) state->embedded->DiscardLinkPeekClose();
    link_peek_closing_=false; auto after=std::move(link_peek_after_); Publish();
    FocusBrowser(LiveBrowser(link_peek_id_)); if (after) after(false); return;
  }
  auto& unload_callbacks=profile_closing_.contains(binding->second)?profile_closing_:auto_closing_;
  if (const auto automatic=unload_callbacks.find(binding->second); automatic!=unload_callbacks.end()) {
    auto done=std::move(automatic->second); unload_callbacks.erase(automatic); Publish(); done(false); return;
  }
  if (moving_unloads_.erase(binding->second)) {
    detached_removals_.erase(binding->second);
    tree_.active_id = binding->second; EnsureActive(); Publish(); return;
  }
  auto pending = closing_.find(binding->second);
  if (pending != closing_.end()) {
    auto done = std::move(pending->second.second); closing_.erase(pending); Publish();
    if (done) done(409, "Close cancelled; page kept", nullptr);
  }
}
void WorkspaceController::FinishClose(const std::string& id, bool archive, Reply done, const std::string& last_url) {
  TreeCommand command{archive ? TreeAction::Archive : TreeAction::Unload}; command.id = id;
  if (PageUrlAllowed(last_url)) command.url = last_url;
  // Unloading the active peek of a collapsed folder dismisses that branch.
  // Another live descendant must not replace the page beneath the folder.
  const auto excluded_folder = !archive && tree_.active_id == id ? CollapsedFolderContaining(tree_.nodes, id) : std::string_view{};
  const auto ordered = OrderedTabsForSpace(tree_, tree_.active_space, excluded_folder);
  for (auto i = recent_.rbegin(); i != recent_.rend(); ++i)
    if (*i != id && !removing_folder_nodes_.contains(*i) && sessions_.contains(*i) && std::find(ordered.begin(), ordered.end(), *i) != ordered.end()) { command.next_active = *i; break; }
  // A live page that was never shown (opened in the background) still beats the empty page,
  // but an unloaded entry is never reloaded just to fill the view: with no live page left the
  // window shows its empty new-tab state (active_id empty) instead.
  if (command.next_active.empty()) for (const auto& candidate : ordered)
    if (candidate != id && !removing_folder_nodes_.contains(candidate) && sessions_.contains(candidate)) { command.next_active = candidate; break; }
  ++closing_commits_;
  if (archive) archiving_.insert(id);
  Dispatch(command, [weak = weak_from_this(), id, done = std::move(done)](int code, const std::string& error, CefRefPtr<CefDictionaryValue> snapshot) {
    if (auto self = weak.lock()) {
      --self->closing_commits_;
      self->archiving_.erase(id);
      if (code) { self->error_ = "Page closed; its saved entry was kept because saving failed"; self->Publish(); }
    }
    if (done) done(code, error, snapshot);
  }, true);
}
void WorkspaceController::OnPageChanged(CefRefPtr<CefBrowser> browser) {
  const auto url = browser->GetMainFrame()->GetURL().ToString();
  if (!crashed_.contains(browser->GetIdentifier()) && PageUrlAllowed(url)) last_urls_[browser->GetIdentifier()] = url;
  if (IsLinkPeekBrowser(browser->GetIdentifier())) { link_peek_url_=url; Publish(); return; }
  Publish();
  if (stopped_ || shutting_down_ || !closing_.empty() || closing_commits_ || browser->IsLoading() || !loaded_.contains(browser->GetIdentifier())) return;
  const auto binding = browser_nodes_.find(browser->GetIdentifier());
  if (binding == browser_nodes_.end()) return;
  const auto id = binding->second;
  const auto node = std::find_if(tree_.nodes.begin(), tree_.nodes.end(), [&](const auto& n) { return n.id == id; });
  if (node == tree_.nodes.end() || node->pinned || node->archived || node->url == url || !PageUrlAllowed(url) || saving_urls_.contains(id)) return;
  saving_urls_[id] = url;
  TreeCommand command{TreeAction::SetUrl}; command.id = id; command.url = url;
  Dispatch(command, [weak = weak_from_this(), id](int code, const std::string&, CefRefPtr<CefDictionaryValue>) {
    if (auto self = weak.lock()) {
      self->saving_urls_.erase(id);
      const auto live = self->sessions_.find(id);
      // A pin/archive queued first may legitimately reject this write. Never
      // overwrite its home URL or retry a failed storage operation in a loop.
      if (!code && live != self->sessions_.end()) self->OnPageChanged(live->second);
    }
  }, true);
}
void WorkspaceController::OnLoadFinished(CefRefPtr<CefBrowser> browser) {
  loaded_.insert(browser->GetIdentifier());
  if (const auto state = state_.lock(); state && state->embedded) {
    if (IsLinkPeekBrowser(browser->GetIdentifier())) state->embedded->LinkPeekPageReady(browser);
    else state->embedded->OnPresentationLoadFinished(browser);
  }
  OnPageChanged(browser);
}
void WorkspaceController::OnTitleChanged(CefRefPtr<CefBrowser> browser) {
  const auto binding = browser_nodes_.find(browser->GetIdentifier());
  if (binding == browser_nodes_.end()) return;
  const auto entry = browser->GetHost()->GetVisibleNavigationEntry();
  if (!entry || !entry->IsValid()) return;
  const auto url = entry->GetURL().ToString();
  if (url != browser->GetMainFrame()->GetURL().ToString() || !PageUrlAllowed(url)) return;
  const auto raw = entry->GetTitle().ToString();
  // Page titles are untrusted text: no control characters, bounded length.
  size_t end = std::min<size_t>(raw.size(), 240);
  while (end < raw.size() && end > 0 && (static_cast<unsigned char>(raw[end]) & 0xC0) == 0x80) --end;  // UTF-8 boundary.
  std::string title;
  for (unsigned char c : raw.substr(0, end)) title += c < 32 || c == 127 ? ' ' : static_cast<char>(c);
  if (title.find_first_not_of(' ') == std::string::npos) title.clear();
  if (title.empty() && browser->IsLoading()) return;
  const PageTitle page{url, std::move(title)};
  const auto old = page_titles_.find(binding->second);
  if (old != page_titles_.end() && old->second == page) return;
  page_titles_[binding->second] = page;
  Publish();
  if (IsPeekPage(binding->second)) return;
  SavePageTitle(binding->second);
}
void WorkspaceController::SavePageTitle(const std::string& id) {
  if (stopped_ || shutting_down_ || !worker_) return;
  const auto title = page_titles_.find(id);
  if (title == page_titles_.end()) return;
  const auto node = std::find_if(tree_.nodes.begin(), tree_.nodes.end(), [&](const auto& n) { return n.id == id; });
  if (node == tree_.nodes.end() || !SamePageUrl(title->second.url, node->url)) return;
  if (node->page_title == title->second.text && !pending_titles_.contains(id)) return;
  pending_titles_[id] = {node->url, title->second.text};
  FlushPageTitle(id);
}
void WorkspaceController::FlushPageTitle(const std::string& id) {
  if (stopped_ || shutting_down_ || !worker_ || !saving_titles_.insert(id).second) return;
  // Coalesce rapid script-driven title changes; don't write every intermediate
  // notification. PrepareShutdown flushes the latest titles before confirming.
  CefPostDelayedTask(TID_UI, base::BindOnce([](std::weak_ptr<WorkspaceController> weak, std::string id) {
    auto self = weak.lock(); if (!self || self->stopped_) return;
    if (self->shutting_down_) { self->saving_titles_.erase(id); return; }
    const auto title = self->pending_titles_.at(id);
    self->worker_->GetTaskRunner()->PostTask(CefCreateClosureTask(base::BindOnce(
        [](std::shared_ptr<WorkspaceController> self, std::string id, PageTitle title) {
          auto result = self->data_->store->SetPageTitle(id, title.url, title.text);
          CefPostTask(TID_UI, base::BindOnce([](std::weak_ptr<WorkspaceController> weak, std::string id,
              PageTitle title, TreeResult result) {
            if (auto owner = weak.lock()) {
              owner->saving_titles_.erase(id);
              const bool saved = static_cast<bool>(result);
              owner->CompleteMetadata(std::move(result), {});
              if (saved && owner->pending_titles_.at(id) == title) owner->pending_titles_.erase(id);
              else if (saved) owner->FlushPageTitle(id);
            }
          }, std::weak_ptr<WorkspaceController>(self), id, title, std::move(result)));
        }, self, id, title)));
  }, weak_from_this(), id), 200);
}
void WorkspaceController::OnDocumentStarted(int browser, const CefString& url) {
  if (const auto state = state_.lock()) {
    const auto profile = state->browser_profiles.find(browser);
    const auto icons = profile == state->browser_profiles.end() ? state->favicons : profile->second->favicons;
    if (icons) icons->StartDocument(browser, IconHost(url));
  }
}
void WorkspaceController::OnFaviconChanged(CefRefPtr<CefBrowser> browser, const std::vector<CefString>& urls) {
  const auto state = state_.lock();
  const int id = browser->GetIdentifier();
  const auto profile = ProfileForBrowser(state,browser);
  const auto icons = profile ? profile->favicons : state ? state->favicons : nullptr;
  if (!state || !icons || !browser_nodes_.contains(id) || urls.empty()) return;
  const auto host = IconHost(browser->GetMainFrame()->GetURL());
  if (host.empty()) return;
  const auto token = icons->Begin(id, urls.front().ToString());
  if (!token) return;
  class Done final : public CefDownloadImageCallback {
   public:
    Done(std::weak_ptr<FaviconCache> cache, std::weak_ptr<AppSession> session, std::weak_ptr<HistoryService> history,
         std::string host, int browser, uint64_t token)
      : cache_(std::move(cache)), session_(std::move(session)), history_(std::move(history)),
        host_(std::move(host)), browser_(browser), token_(token) {}
    void OnDownloadImageFinished(const CefString&, int, CefRefPtr<CefImage> image) override {
      const auto cache = cache_.lock();
      if (!cache) return;
      int width = 0, height = 0;
      const auto png = image && !image->IsEmpty() ? image->GetAsPNG(1.0f, true, width, height) : nullptr;
      // A 32 px icon is a few KB; refuse anything larger.
      std::string bytes;
      if (png && png->GetSize() <= 12 * 1024) {
        bytes.resize(png->GetSize());
        if (png->GetData(bytes.data(), bytes.size(), 0) != bytes.size()) bytes.clear();
      }
      const auto result = cache->Complete(browser_, token_, host_, bytes);
      if (result.seeded) if (const auto history = history_.lock()) history->SaveFavicon(host_, bytes);
      if (!result.changed) return;
      // A live page can move to another window while the image is in flight.
      if (const auto session = session_.lock()) for (const auto& window : session->Windows()) {
        if (!window->workspace || !window->workspace->browser_nodes_.contains(browser_)) continue;
        window->workspace->Publish();
        break;
      }
    }
   private:
    std::weak_ptr<FaviconCache> cache_;
    std::weak_ptr<AppSession> session_;
    std::weak_ptr<HistoryService> history_;
    std::string host_;
    int browser_; uint64_t token_;
    IMPLEMENT_REFCOUNTING(Done);
  };
  // The browser downloads with the page's own context; nothing reaches the shell's network.
  browser->GetHost()->DownloadImage(urls.front(), true, 32, false,
      new Done(icons, session_, state->private_window ? nullptr : profile ? profile->history : state->history, host, id, *token));
}
bool WorkspaceController::PrepareShutdown(std::function<void(bool)> done) {
  CancelFolderRemoval();
  if (!ready_ || shutting_down_ || !creating_.empty() || !closing_.empty() || closing_commits_ || pending_commands_) {
    error_ = "Please wait for the current tab operation before closing the window"; Publish(); return false;
  }
  shutting_down_ = true; Publish();
  worker_->GetTaskRunner()->PostTask(CefCreateClosureTask(base::BindOnce(
      [](std::shared_ptr<WorkspaceController> self, std::map<std::string, PageTitle> titles, std::function<void(bool)> done) {
        auto result = self->data_->store->Read();
        for (const auto& [id, title] : titles) {
          if (!result) break;
          result = self->data_->store->SetPageTitle(id, title.url, title.text);
        }
        CefPostTask(TID_UI, base::BindOnce([](std::weak_ptr<WorkspaceController> weak, TreeResult result, std::function<void(bool)> done) {
          if (auto owner = weak.lock()) {
            if (!result) { owner->error_ = "Cannot verify saved session; window kept open"; owner->Publish(); }
            done(static_cast<bool>(result));
          }
        }, std::weak_ptr<WorkspaceController>(self), std::move(result), std::move(done)));
      }, shared_from_this(), pending_titles_, std::move(done))));
  return true;
}
void WorkspaceController::CancelShutdown() {
  shutting_down_ = false;
  for (const auto& [id, title] : pending_titles_) FlushPageTitle(id);
  Publish(); EnsureActive();
}
bool WorkspaceController::OnCrashed(CefRefPtr<CefBrowser> browser) {
  const auto id = browser->GetIdentifier();
  const auto binding = browser_nodes_.find(id);
  if (binding == browser_nodes_.end()) return false;
  if (shutting_down_ || closing_.contains(binding->second)) return true;
  if (!last_urls_.contains(id)) {
    const auto node = std::find_if(tree_.nodes.begin(), tree_.nodes.end(), [&](const auto& n) { return n.id == binding->second; });
    last_urls_[id] = node != tree_.nodes.end() ? node->url : "";
  }
  crashed_.insert(id); loaded_.erase(id);
  if (IsLinkPeekBrowser(id)) { if (const auto state=state_.lock(); state && state->embedded) state->embedded->PresentCrash(browser); Publish(); return true; }
  EnsureActive(); Publish(); return true;
}
void WorkspaceController::Reload(bool bypass) {
  if (shutting_down_ || !closing_.empty()) return;
  auto state = state_.lock();
  if (!state || !state->content) return;
  auto page = state->content;
  const auto id = page->GetIdentifier();
  if (crashed_.contains(id)) {
    const auto url = last_urls_[id];
    if (!PageUrlAllowed(url)) { error_ = "This page has no recoverable website address"; Publish(); return; }
    crashed_.erase(id); loaded_.erase(id);
    if (IsLinkPeekBrowser(id)) state->embedded->SetLinkPeek(id);
    else if (SelectedSplit()) EnsureActive(); else state->embedded->Activate(page);
    page->GetMainFrame()->LoadURL(url);
  } else if (bypass) page->ReloadIgnoreCache(); else page->Reload();
  FocusBrowser(page);
  Publish();
}
void WorkspaceController::NavigateHistory(bool forward) {
  if (shutting_down_ || !closing_.empty()) return;
  auto state = state_.lock();
  if (!state || !state->content) return;
  auto page = state->content;
  if (forward ? !page->CanGoForward() : !page->CanGoBack()) return;
  if (crashed_.erase(page->GetIdentifier())) {
    loaded_.erase(page->GetIdentifier());
    if (IsLinkPeekBrowser(page->GetIdentifier())) state->embedded->SetLinkPeek(page->GetIdentifier());
    else if (SelectedSplit()) EnsureActive(); else state->embedded->Activate(page);
  }
  if (forward) page->GoForward(); else page->GoBack();
  FocusBrowser(page);
  Publish();
}
void WorkspaceController::OpenNewTab() {
  if (!ready_ || shutting_down_ || !closing_.empty() || closing_commits_) return;
  if (auto state = state_.lock()) {
    if (state->command_bar) state->command_bar->Open(true);
  }
  ++new_tab_request_; Publish();
}
void WorkspaceController::FocusAddress() {
  if (shutting_down_) return;
  if (auto state = state_.lock()) {
    if (state->command_bar) state->command_bar->Open(false);
  }
  ++address_request_; Publish();
}
void WorkspaceController::RequestSidebar(const std::string& action) {
  if (shutting_down_ || (action != "newSpace" && action != "editSpace" && action != "newFolder")) return;
  sidebar_action_ = action; ++sidebar_request_; Publish();
}
void WorkspaceController::CloseActive() {
  if (HasLinkPeek()) { DismissLinkPeek(); return; }
  if (!tree_.active_id.empty()) Close(tree_.active_id, false);
}
void WorkspaceController::CycleTab(bool reverse) {
  if (!ready_ || shutting_down_ || !closing_.empty() || closing_commits_) return;
  auto order = OrderedTabsForSpace(tree_, tree_.active_space);
  for (const auto& split : tree_.splits) if (std::find(order.begin(),order.end(),split.anchor) != order.end()) {
    const auto other = std::find_if(tree_.nodes.begin(),tree_.nodes.end(),[&](const auto& node) { return node.id == split.other; });
    if (other != tree_.nodes.end() && !other->favorite) std::erase(order,split.other);
  }
  if (order.empty()) return;
  auto current = pending_selections_ ? selection_cursor_ : tree_.active_id;
  for (const auto& split : tree_.splits) if (current == split.other && std::find(order.begin(),order.end(),current) == order.end()) current = split.anchor;
  const auto found = std::find(order.begin(), order.end(), current);
  const auto index = found == order.end() ? (reverse ? 0 : order.size() - 1) : static_cast<size_t>(found - order.begin());
  selection_cursor_ = order[(index + (reverse ? order.size() - 1 : 1)) % order.size()];
  TreeCommand command{TreeAction::Activate}; command.id = selection_cursor_; ++pending_selections_;
  Dispatch(command, [weak = weak_from_this()](int, const std::string&, CefRefPtr<CefDictionaryValue>) {
    if (auto self = weak.lock(); self && --self->pending_selections_ == 0) self->selection_cursor_.clear();
  }, true);
}
void WorkspaceController::SwitchSpaceAt(int index) {
  if (!ready_ || shutting_down_ || !closing_.empty() || closing_commits_ || index < 0) return;
  auto spaces = tree_.spaces;
  std::sort(spaces.begin(), spaces.end(), [](const auto& a, const auto& b) { return a.position < b.position; });
  if (index >= static_cast<int>(spaces.size()) || spaces[index].id == tree_.active_space) return;
  TreeCommand command{TreeAction::SwitchSpace}; command.id = spaces[index].id;
  Dispatch(command, {}, true);
}
void WorkspaceController::QueueExternalUrls(const std::vector<std::string>& urls) {
  for (const auto& url : urls) {
    if (external_pending_.size() >= 16) break;
    if (UrlAllowed(url) || IsLocalFileUrl(url)) external_pending_.push_back(url);
  }
  DrainExternalUrls();
}
void WorkspaceController::DrainExternalUrls() {
  const auto state = state_.lock();
  if (external_opening_ || external_pending_.empty() || !CanOpen() || !state || !state->embedded || !state->window) return;
  auto url = std::move(external_pending_.front()); external_pending_.erase(external_pending_.begin());
  external_opening_ = true;
  OpenTyped(url, [weak = weak_from_this()](int code, const std::string& error, CefRefPtr<CefDictionaryValue>) {
    if (const auto self = weak.lock()) {
      self->external_opening_ = false;
      if (code) self->Notify(error);
      self->DrainExternalUrls();
    }
  });
}
void WorkspaceController::OpenLink(const std::string& url, bool foreground, Reply done, const std::string& initiator,
                                   CefRefPtr<CefBrowser> opener) {
  if (!UrlAllowed(url)) { if (done) done(409, "Cannot open this link now", nullptr); return; }
  CefURLParts parts; CefParseURL(url, parts);
  std::string after;
  if (opener) for (const auto& [id, browser] : sessions_) if (browser->IsSame(opener)) after = id;
  if (after.empty()) link_opener_.clear();
  else if (after != link_opener_) { link_opener_ = after; link_last_.clear(); }
  else if (std::any_of(tree_.nodes.begin(), tree_.nodes.end(), [&](const auto& n) { return n.id == link_last_ && !n.archived && !n.pinned && n.parent.empty(); }))
    after = link_last_;  // The next link from the same tab goes below the previous one.
  OpenPage(url, CefString(&parts.host).ToString().substr(0, 240), foreground, std::move(done), initiator, after);
}
void WorkspaceController::OpenFile(const std::string& url, bool foreground) {
  if (!IsLocalFileUrl(url) || !CanOpen()) return;
  OpenPage(url, FileTitle(url), foreground, {});
}
void WorkspaceController::OpenTyped(const std::string& url, Reply done) {
  if (IsLocalFileUrl(url)) OpenPage(url, FileTitle(url), true, std::move(done)); else OpenLink(url, true, std::move(done));
}
bool WorkspaceController::AdoptExtensionPage(const std::string& url, const std::string& profile) {
  // Never route website-initiated navigation here: a browser-initiated load of
  // a chrome-extension:// page would bypass web_accessible_resources checks.
  if (!PageUrlAllowed(url) || !CanOpen()) return false;
  if (!profile.empty() && profile != BrowsingProfileId(tree_)) {
    const auto space = std::find_if(tree_.spaces.begin(),tree_.spaces.end(),[&](const auto& s) { return s.profile == profile; });
    if (space == tree_.spaces.end()) return false;
    TreeCommand command; command.action = TreeAction::SwitchSpace; command.id = space->id; command.expected_revision = tree_.revision;
    Dispatch(command,[weak=weak_from_this(),url,profile](int code,const std::string&,CefRefPtr<CefDictionaryValue>) {
      if (const auto self=weak.lock(); self && !code) self->AdoptExtensionPage(url,profile);
    });
    return true;
  }
  std::string title = "Extension page";
  if (UrlAllowed(url)) { CefURLParts parts; CefParseURL(url, parts); title = CefString(&parts.host).ToString().substr(0, 240); }
  OpenPage(url, title, true, {});
  return true;
}
void WorkspaceController::ReopenClosed() {
  if (!ready_ || shutting_down_) return;
  const TreeNode* latest = nullptr;
  for (const auto& node : tree_.nodes)
    if (!node.folder && node.archived && node.archived_at > 0 && (!latest || node.archived_at > latest->archived_at)) latest = &node;
  if (!latest) { Notify("No recently closed tabs"); return; }
  auto command = CefDictionaryValue::Create(); command->SetString("method", "tree.restore");
  command->SetString("id", latest->id); command->SetDouble("expectedRevision", static_cast<double>(tree_.revision));
  Handle(command, [](int, const std::string&, CefRefPtr<CefDictionaryValue>) {});
}
int WorkspaceController::Duplicate(const std::string& id, std::string& error) {
  const auto node = std::find_if(tree_.nodes.begin(), tree_.nodes.end(), [&](const auto& n) { return n.id == id; });
  if (node == tree_.nodes.end() || node->folder || node->archived) { error = "Tab is not available"; return 404; }
  // The page's current address, not only the saved one.
  const auto live = sessions_.find(id);
  std::string url = node->url;
  if (live != sessions_.end() && !crashed_.contains(live->second->GetIdentifier())) {
    const auto current = live->second->GetMainFrame()->GetURL().ToString();
    if (PageUrlAllowed(current)) url = current;
  }
  if (IsInternalPageUrl(url)) { OpenInternalPage(url, node->title); return 0; }  // One tab per internal page.
  if (!PageUrlAllowed(url)) { error = "This tab cannot be duplicated"; return 409; }
  if (!CanOpen()) { error = "Cannot open a tab now"; return 409; }
  const auto live_title = page_titles_.find(id);
  const auto title = live_title != page_titles_.end() && live_title->second.url == url && !live_title->second.text.empty() ? live_title->second.text :
      SamePageUrl(url, node->url) && !node->page_title.empty() ? node->page_title : node->title;
  OpenPage(url, title.substr(0, 240), true, {});
  return 0;
}
int WorkspaceController::SetMuted(const std::string& id, bool muted, std::string& error) {
  const auto live = sessions_.find(id);
  if (live == sessions_.end()) { error = "Tab is not open"; return 409; }
  live->second->GetHost()->SetAudioMuted(muted);
  Publish();
  return 0;
}
void WorkspaceController::OpenInternalPage(std::string_view url, const std::string& title) {
  // Reuse an open tab of that page instead of stacking copies.
  for (const auto& node : tree_.nodes) {
    if (node.folder || node.archived || node.url != url || BrowsingProfileId(tree_,node.id) != BrowsingProfileId(tree_)) continue;
    auto command = CefDictionaryValue::Create(); command->SetString("method", "tree.activate");
    command->SetString("id", node.id); command->SetDouble("expectedRevision", static_cast<double>(tree_.revision));
    Handle(command, [](int, const std::string&, CefRefPtr<CefDictionaryValue>) {});
    return;
  }
  OpenPage(std::string(url), title, true, {});
}
void WorkspaceController::Notify(const std::string& text) {
  notice_ = text.substr(0, 240); ++notice_id_; Publish();
}
bool WorkspaceController::CanOpen() const {
  return ready_ && !shutting_down_ && !profile_changing_ && !folder_removal_ && closing_.empty() && !closing_commits_ && opening_links_ + creating_.size() < 16;
}
void WorkspaceController::OpenPage(const std::string& url, const std::string& title, bool foreground, Reply done, const std::string& initiator,
                                   const std::string& after) {
  if (!CanOpen()) { if (done) done(409, "Cannot open this link now", nullptr); return; }
  // Registered before the tab exists: a foreground page is created while the
  // command completes, before the reply below runs.
  // Both ends must be websites: LoadURLFromInitiator loads nothing otherwise.
  const auto web = [](const std::string& value) { return value.starts_with("https://") || value.starts_with("http://"); };
  if (web(initiator) && web(url)) link_initiator_by_url_[url] = initiator;
  TreeCommand command{TreeAction::CreateTab}; command.url = url;
  command.title = title; command.value = foreground; command.after = after;
  ++opening_links_;
  Dispatch(command, [weak = weak_from_this(), foreground, url, done = std::move(done)](int code, const std::string& error, CefRefPtr<CefDictionaryValue> snapshot) {
    if (auto self = weak.lock()) {
      --self->opening_links_;
      // Consumed by the page's creation below or above; never kept stale.
      struct Forget { WorkspaceController* self; std::string url; ~Forget() { self->link_initiator_by_url_.erase(url); } } forget{self.get(), url};
      auto state = self->state_.lock();
      if (!code && state && !self->shutting_down_) {
        const auto id = snapshot->GetString("createdId").ToString();
        if (!self->link_opener_.empty() && !id.empty()) self->link_last_ = id;
        const auto node = std::find_if(self->tree_.nodes.begin(), self->tree_.nodes.end(), [&](const auto& n) { return n.id == id; });
        if (!foreground && node != self->tree_.nodes.end() && !self->creating_.contains(id)) {
          self->creating_.insert(id);
          if (!self->CreateLive(node->url, id)) { self->creating_.erase(id); self->error_ = "Cannot create page"; self->Publish(); }
        }
      } else if (code && !done) { self->error_ = error; self->Publish(); }
    }
    if (done) done(code, error, snapshot);
  }, true);
}
void WorkspaceController::SetSidebarVisible(bool visible, bool live) {
  auto state = state_.lock();
  if (!state || !state->window || !state->embedded || shutting_down_) return;
  state->sidebar_visible = visible;
  // The page changes width once, never per frame (resizing a Chrome page every
  // frame shows unrendered strips). Showing docks the shape at once and the
  // shell slides its content in (Arc). Hiding is instant (user decision after
  // two animated variants stuttered).
  const auto client = state->window->GetClientAreaBoundsInScreen();
  const double width = SidebarWidth(state->sidebar_width, client.width);
  LARGE_INTEGER frequency, t0, t1, t2, t3; QueryPerformanceFrequency(&frequency); QueryPerformanceCounter(&t0);
  if (state->sidebar_peek) state->sidebar_peek->SetEnabled(!visible || state->embedded->IsBrowserFullscreen());
  QueryPerformanceCounter(&t1);
  // A live drag publishes only the layout delta; the DevTools dock follows
  // the page without queuing its own workspace refresh.
  if (live && state->devtools_dock) state->devtools_dock->SetLive(true);
  LayoutSidebar(visible ? width : 0, true);
  if (live && state->devtools_dock) state->devtools_dock->SetLive(false);
  QueryPerformanceCounter(&t2);
  if (live) PublishLayout(); else Publish();
  QueryPerformanceCounter(&t3);
  if (state->probe) {
    auto ms = [&](LARGE_INTEGER a, LARGE_INTEGER b) { return static_cast<double>(b.QuadPart - a.QuadPart) * 1000.0 / frequency.QuadPart; };
    auto data = CefDictionaryValue::Create(); data->SetBool("visible", visible);
    data->SetDouble("peek_ms", ms(t0, t1)); data->SetDouble("layout_ms", ms(t1, t2)); data->SetDouble("publish_ms", ms(t2, t3));
    state->Record("sidebar_visible_timing", data);
  }
}
void WorkspaceController::LayoutSidebar(double width, bool final) {
  auto state = state_.lock();
  if (!state || !state->window || !state->embedded) return;
  const auto client = state->window->GetClientAreaBoundsInScreen();
  const int x = static_cast<int>(std::lround(width)) + kContentInset;
  state->embedded->Resize(CefRect(x, kToolbarHeight, std::max(1, client.width - x - kContentInset),
                                  std::max(1, client.height - kToolbarHeight - kContentInset)));
  if (!final) return;
  state->embedded->QueueRaise();
  // Probe evidence once the animation has settled and the page is revealed
  // (a cold page's input is held until its first frame; up to ~2 s).
  if (state->probe) RecordSidebarLayout(state_, 40);
}
void WorkspaceController::RecordSidebarLayout(std::weak_ptr<State> weak, int attempts) {
  CefPostDelayedTask(TID_UI, base::BindOnce([](std::weak_ptr<State> weak, int attempts) {
    auto state = weak.lock();
    if (!state || !state->embedded || !state->content) return;
    if (state->embedded->Presenting() && attempts > 0) { RecordSidebarLayout(weak, attempts - 1); return; }
    auto data = CefDictionaryValue::Create();
    data->SetBool("visible", state->sidebar_visible);
    data->SetBool("native_geometry", state->embedded->ActiveMatchesBounds());
    data->SetBool("native_input", state->embedded->ActiveOwnsInputArea());
    state->Record("sidebar_native_layout", data);
  }, weak, attempts), attempts == 40 ? 0 : 50);
}
void WorkspaceController::SetSidebarWidth(int width, bool save, Reply done) {
  auto state = state_.lock();
  if (!ready_ || shutting_down_ || !state || !state->window || !state->embedded) {
    done(409, "Workspace is not ready to resize", nullptr); return;
  }
  state->sidebar_width = width;
  // Live drag moves (!save): layout delta only, no tree snapshot to every UI.
  if (state->sidebar_visible && !state->embedded->IsBrowserFullscreen()) SetSidebarVisible(true, !save);
  else if (save) Publish(); else PublishLayout();  // Floating resize leaves the native website viewport alone.
  // DOM pointer capture/focus in Alloy does not transfer the Win32 keyboard
  // target away from an embedded Chrome page. Resize is a Shell interaction.
  if (state->sidebar_visible && (!state->sidebar_peek || !state->sidebar_peek->is_open())) FocusShell(state);
  if (!save) { done(0, "", LayoutSnapshot()); return; }
  ++pending_commands_;
  worker_->GetTaskRunner()->PostTask(CefCreateClosureTask(base::BindOnce(
      [](std::shared_ptr<WorkspaceController> self, int width, Reply done) {
        auto result = self->data_->store->SetSidebarWidth(width);
        CefPostTask(TID_UI, base::BindOnce([](std::weak_ptr<WorkspaceController> weak, TreeResult result, Reply done) {
          if (auto owner = weak.lock()) { --owner->pending_commands_; owner->CompleteMetadata(std::move(result), std::move(done)); }
        }, std::weak_ptr<WorkspaceController>(self), std::move(result), std::move(done)));
      }, shared_from_this(), width, std::move(done))));
}
void WorkspaceController::ApplyTheme(const std::shared_ptr<State>& state) {
  state->theme_dark = ResolveDark(state->theme.mode, SystemPrefersDark());
  state->theme_palette_css.store(std::make_shared<const std::string>(PaletteCss(state->theme,state->theme_dark)));
  state->theme_packed = static_cast<std::uint32_t>(state->theme.hue) | static_cast<std::uint32_t>(state->theme.intensity) << 9 |
                        static_cast<std::uint32_t>(state->theme.noise) << 17 | static_cast<std::uint32_t>(state->theme_dark) << 24;
  SetWorkspaceBackgroundTheme(state->theme, state->theme_dark, state->window ? state->window->GetWindowHandle() : nullptr);
  if (!state->window) return;
  // Authored light colors: the window behind the shell and the caption (only
  // visible while the shell paints), see style.css :root.
  const auto background = state->theme.palette?PaletteBackgroundAt(state->theme,state->theme_dark,.5,.5):Themed({231, 222, 237}, state->theme, state->theme_dark, true);
  const auto text = Themed({54, 45, 64}, state->theme, state->theme_dark);
  const HWND window=state->window->GetWindowHandle();
  const bool first=state->theme_window!=window;
  const auto window_background=WindowBackground(state);
  if(first||window_background!=state->theme_window_background)state->window->SetBackgroundColor(window_background);
  // A system backdrop: no caption fill, or DWM paints it over the backdrop's top strip.
  const COLORREF caption = state->translucent ? DWMWA_COLOR_NONE : RGB(background.r, background.g, background.b), caption_text = RGB(text.r, text.g, text.b);
  if(first||caption!=state->theme_caption)DwmSetWindowAttribute(window, DWMWA_CAPTION_COLOR, &caption, sizeof(caption));
  if(first||caption_text!=state->theme_caption_text)DwmSetWindowAttribute(window, DWMWA_TEXT_COLOR, &caption_text, sizeof(caption_text));
  const BOOL dark = state->theme_dark;
  if(first||state->theme_dark!=state->theme_window_dark)DwmSetWindowAttribute(window, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
  if(state->translucent&&(first||state->backdrop!=state->theme_window_backdrop))ApplySystemBackdrop(window,state->backdrop);
  state->theme_window=window;state->theme_window_background=window_background;
  state->theme_caption=caption;state->theme_caption_text=caption_text;
  state->theme_window_dark=state->theme_dark;state->theme_window_backdrop=state->backdrop;
  if(state->embedded)state->embedded->RefreshTheme();
}
cef_color_t WorkspaceController::WindowBackground(const std::shared_ptr<State>& state) {
  // With a system backdrop the window stays transparent and the UI paints a translucent tint.
  const auto background = state->theme.palette?PaletteBackgroundAt(state->theme,state->theme_dark,.5,.5):Themed({231, 222, 237}, state->theme, state->theme_dark, true);
  if(state->translucent&&state->backdrop!="none")return CefColorSetARGB(0,0,0,0);
  return CefColorSetARGB(255,background.r,background.g,background.b);
}
bool WorkspaceController::ParseThemeCommand(CefRefPtr<CefDictionaryValue> d, Theme& theme, bool& save) {
  if (!d || d->GetSize() != 6+size_t(d->HasKey("palette"))+size_t(d->HasKey("spaceId")) || d->GetType("hue") != VTYPE_INT || d->GetType("intensity") != VTYPE_INT ||
      d->GetType("mode") != VTYPE_STRING || d->GetType("noise") != VTYPE_INT || d->GetType("save") != VTYPE_BOOL) return false;
  const auto mode = d->GetString("mode").ToString();
  const int mode_value = mode == "light" ? 0 : mode == "dark" ? 1 : mode == "system" ? 2 : -1;
  if (!ValidTheme(d->GetInt("hue"), d->GetInt("intensity"), mode_value, d->GetInt("noise"))) return false;
  theme = {d->GetInt("hue"), d->GetInt("intensity"), static_cast<ThemeMode>(mode_value), d->GetInt("noise")};
  if (d->HasKey("spaceId") && (d->GetType("spaceId") != VTYPE_STRING || d->GetString("spaceId").length()!=36)) return false;
  if (d->HasKey("palette")) {
    if (!d->HasKey("spaceId") || d->GetType("palette")!=VTYPE_DICTIONARY) return false;
    const auto p=d->GetDictionary("palette");if(p->GetSize()!=(p->GetInt("version")==2?8+size_t(p->HasKey("rotation")):7)) return false;
    for(const auto* key:{"version","count","radius","lightness","harmony","kind","fill"})if(p->GetType(key)!=VTYPE_INT)return false;
    ThemePalette value{p->GetInt("version"),p->GetInt("count"),p->GetInt("radius"),p->GetInt("lightness"),p->GetInt("harmony"),p->GetInt("kind"),p->GetInt("fill")};
    if(value.version==2){if(p->HasKey("rotation")){if(p->GetType("rotation")!=VTYPE_INT)return false;value.rotation=p->GetInt("rotation");}
      if(p->GetType("circles")!=VTYPE_LIST)return false;const auto list=p->GetList("circles");if(list->GetSize()!=3)return false;
      for(size_t i=0;i<3;++i){if(list->GetType(i)!=VTYPE_DICTIONARY)return false;const auto c=list->GetDictionary(i);if(c->GetSize()!=3)return false;
        for(const auto* key:{"hue","distance","size"})if(c->GetType(key)!=VTYPE_INT)return false;
        value.circles[i]={c->GetInt("hue"),c->GetInt("distance"),c->GetInt("size")};}}
    if(!ValidPalette(value))return false;
    theme.palette=value;
  }
  save = d->GetBool("save");
  return true;
}
void WorkspaceController::SetTheme(const Theme& theme, bool save, Reply done, const std::string& space) {
  auto state = state_.lock();
  if (!ready_ || shutting_down_ || !state) { done(409, "Workspace is not ready", nullptr); return; }
  const auto target=space.empty()?tree_.active_space:space;
  const bool active=target==tree_.active_space;
  if (std::none_of(tree_.spaces.begin(),tree_.spaces.end(),[&](const auto& item){return item.id==target;})) {done(404,"Space no longer exists",nullptr);return;}
  if (!active&&!save) {done(409,"Space changed; reopen the theme editor",nullptr);return;}
  if (!ValidTheme(theme.hue,theme.intensity,static_cast<int>(theme.mode),theme.noise) || (theme.palette&&!ValidPalette(*theme.palette))) {done(400,"Invalid theme",nullptr);return;}
  const auto started=std::chrono::steady_clock::now();
  auto applied=started,published=started;
  if(active){++theme_generation_;state->theme=theme;ApplyTheme(state);applied=std::chrono::steady_clock::now();if(save)Publish();else PublishTheme();published=std::chrono::steady_clock::now();}
  if (!save) {
    auto snapshot=ThemeSnapshot();
    if(state->palette_probe||state->theme_preview_probe){
      const auto elapsed=[](auto a,auto b){return std::chrono::duration<double,std::milli>(b-a).count();};
      auto data=CefDictionaryValue::Create();data->SetDouble("apply_ms",elapsed(started,applied));
      data->SetDouble("publish_ms",elapsed(applied,published));data->SetDouble("snapshot_ms",elapsed(published,std::chrono::steady_clock::now()));
      data->SetInt("nodes",static_cast<int>(tree_.nodes.size()));state->Record("theme_preview_timing",data);
    }
    done(0, "", snapshot); return;
  }
  if(active)RememberTheme(state);
  ++pending_commands_;
  worker_->GetTaskRunner()->PostTask(CefCreateClosureTask(base::BindOnce(
      [](std::shared_ptr<WorkspaceController> self, Theme theme, Reply done, std::string space,uint64_t generation) {
        auto result = self->data_->store->SetTheme(theme.hue, theme.intensity, static_cast<int>(theme.mode), theme.noise, space,theme.palette);
        CefPostTask(TID_UI, base::BindOnce([](std::weak_ptr<WorkspaceController> weak, TreeResult result, Reply done,std::string space,uint64_t generation) {
          if (auto owner = weak.lock()) {
            --owner->pending_commands_;owner->preserve_theme_preview_=space==owner->tree_.active_space&&generation!=owner->theme_generation_;
            owner->CompleteMetadata(std::move(result),std::move(done));owner->preserve_theme_preview_=false;
          }
        }, std::weak_ptr<WorkspaceController>(self), std::move(result), std::move(done),space,generation));
      }, shared_from_this(), theme, std::move(done),target,theme_generation_)));
}
void WorkspaceController::RevertThemePreview() {
  auto state = state_.lock(); const auto* space = tree_.ActiveSpace();
  if (!ready_ || shutting_down_ || !state || !space) return;
  const Theme saved{space->theme_hue, space->theme_intensity, static_cast<ThemeMode>(space->theme_mode), space->theme_noise, space->theme_palette};
  ++theme_generation_; state->theme = saved; ApplyTheme(state); PublishTheme();
}
void WorkspaceController::TogglePin() {
  if (auto state = state_.lock(); state && state->standalone_window) return;
  if (!ready_ || shutting_down_ || !closing_.empty() || closing_commits_) return;
  const auto node = std::find_if(tree_.nodes.begin(), tree_.nodes.end(), [&](const auto& n) { return n.id == tree_.active_id; });
  if (node == tree_.nodes.end()) return;
  TreeCommand command{TreeAction::Pin}; command.id = node->id; command.value = !node->pinned;
  const auto live = sessions_.find(node->id);
  if (live != sessions_.end()) {
    const auto url = live->second->GetMainFrame()->GetURL().ToString();
    if (PageUrlAllowed(url)) command.url = url;
  }
  Dispatch(command, {}, true);
}
void WorkspaceController::Navigate(const std::string& url, Reply done) {
  if (!ready_ || shutting_down_ || folder_removal_ || !closing_.empty() || closing_commits_ || !UserUrlAllowed(url)) { done(400, "Invalid active tab or URL", nullptr); return; }
  if (HasLinkPeek()) {
    const auto browser=LiveBrowser(link_peek_id_); if (!browser || link_peek_closing_) { done(409,"Link preview is opening",nullptr); return; }
    browser->GetMainFrame()->LoadURL(url); FocusBrowser(browser); Publish(); done(0,"",Snapshot()); return;
  }
  const auto open = [&] { OpenTyped(url, std::move(done)); };
  if (tree_.active_id.empty()) { open(); return; }
  const auto active = std::find_if(tree_.nodes.begin(), tree_.nodes.end(), [&](const auto& n) { return n.id == tree_.active_id; });
  // The history page never navigates away; an address typed there opens a tab.
  if (active != tree_.nodes.end() && IsInternalPageUrl(active->url)) { open(); return; }
  if (active != tree_.nodes.end() && active->pinned) {
    const auto live = sessions_.find(active->id);
    if (live == sessions_.end()) { done(409, "Page is not ready", nullptr); return; }
    if (crashed_.erase(live->second->GetIdentifier())) { if (SelectedSplit()) EnsureActive(); else if (auto state = state_.lock()) state->embedded->Activate(live->second); }
    live->second->GetMainFrame()->LoadURL(url); FocusBrowser(live->second); Publish(); done(0, "", Snapshot()); return;
  }
  TreeCommand command{TreeAction::SetUrl}; command.id = tree_.active_id; command.url = url;
  const auto id = command.id;
  Dispatch(command, [weak = weak_from_this(), id, url, done = std::move(done)](int code, const std::string& error, CefRefPtr<CefDictionaryValue> snapshot) {
    if (auto self = weak.lock(); self && !code) {
      auto session = self->sessions_.find(id);
      if (session != self->sessions_.end()) {
        if (self->crashed_.erase(session->second->GetIdentifier())) { if (self->SelectedSplit()) self->EnsureActive(); else if (auto state = self->state_.lock()) state->embedded->Activate(session->second); }
        session->second->GetMainFrame()->LoadURL(url); FocusBrowser(session->second);
      }
    }
    if (done) done(code, error, snapshot);
  }, true);
}
void WorkspaceController::Handle(CefRefPtr<CefDictionaryValue> request, Reply done) {
  if (profile_changing_) { done(409,"Finish changing the profile first",nullptr); return; }
  if (folder_removal_) { done(409,"Finish removing the folder first",nullptr); return; }
  if (shutting_down_) { done(409, "Window is closing", nullptr); return; }
  if (!ready_) { done(503, error_.empty() ? "Workspace is loading" : error_, nullptr); return; }
  if (shutting_down_ || !closing_.empty() || closing_commits_) { done(409, "Finish closing the page first", nullptr); return; }
  const auto method = request->GetString("method").ToString();
  if (HandleLinkPeek(request,done)) return;
  if (HandleMedia(request,done)) return;
  if (HandleSplit(request,done)) return;
  if (auto state = state_.lock(); state && state->standalone_window &&
      (method.starts_with("space.") || method == "tree.createFolder" || method == "tree.pin" || method == "tree.moveToSpace" ||
       (method == "tree.place" && request->GetString("section") != "today"))) {
    done(403, "This window contains only local tabs", nullptr); return;
  }
  auto reject = [&done]() { done(400, "Invalid workspace command", nullptr); };
  const auto revision_type = request->GetType("expectedRevision");
  if (revision_type != VTYPE_INT && revision_type != VTYPE_DOUBLE) { reject(); return; }
  const double revision = revision_type == VTYPE_INT ? request->GetInt("expectedRevision") : request->GetDouble("expectedRevision");
  if (!std::isfinite(revision) || revision < 0 || revision > 9007199254740991.0 || std::floor(revision) != revision) { reject(); return; }
  TreeCommand command{TreeAction::Activate}; command.expected_revision = static_cast<std::int64_t>(revision);
  size_t fields = 0;
  auto text = [&](const char* name, size_t maximum, std::string& target) {
    if (request->GetType(name) != VTYPE_STRING || request->GetString(name).length() > maximum) return false;
    target = request->GetString(name).ToString(); return true;
  };
  if (method == "split.create") {
    command.action = TreeAction::CreateSplit; fields = 5;
    if (!text("id",36,command.id) || !text("otherId",36,command.parent) || request->GetType("vertical") != VTYPE_BOOL) { reject(); return; }
    command.value = request->GetBool("vertical");
  } else if (method == "split.remove" || method == "split.swap") {
    command.action = method == "split.remove" ? TreeAction::RemoveSplit : TreeAction::SwapSplit; fields = 3;
    if (!text("id",36,command.id)) { reject(); return; }
  } else if (method == "split.rename") {
    command.action = TreeAction::RenameSplit; fields = 4;
    if (!text("id",36,command.id) || !text("title",240,command.title)) { reject(); return; }
  } else if (method == "split.layout") {
    command.action = TreeAction::LayoutSplit; fields = 5;
    if (!text("id",36,command.id) || request->GetType("vertical") != VTYPE_BOOL || request->GetType("ratio") != VTYPE_INT) { reject(); return; }
    command.position = request->GetInt("ratio"); command.value = request->GetBool("vertical"); split_preview_.reset();
    split_resizing_ = false; ++split_resize_generation_;
  } else if (method == "profile.create" || method == "profile.rename") {
    if (const auto state = state_.lock(); !state || state->private_window || state->standalone_window) { done(409,"Profiles are managed in the main browser",nullptr); return; }
    command.action = method == "profile.create" ? TreeAction::CreateProfile : TreeAction::RenameProfile;
    fields = method == "profile.create" ? 3 : 4;
    if (!text("title",40,command.title) || (method == "profile.rename" && !text("id",36,command.id))) { reject(); return; }
    if (method=="profile.create" && request->HasKey("spaceIds")) {
      ++fields;
      if (request->GetType("spaceIds")!=VTYPE_LIST) { reject(); return; }
      const auto spaces=request->GetList("spaceIds");
      if (spaces->GetSize()>24) { reject(); return; }
      for (size_t i=0;i<spaces->GetSize();++i) {
        if (spaces->GetType(i)!=VTYPE_STRING || spaces->GetString(i).length()!=36) { reject(); return; }
        command.spaces.push_back(spaces->GetString(i));
      }
    }
  } else if (method=="space.export") {
    command.action=TreeAction::ExportSpace; fields=4;
    if (!text("id",36,command.id) || !text("space",36,command.space)) { reject(); return; }
  } else if (method == "space.profile") {
    command.action = TreeAction::SetSpaceProfile; fields = 4;
    if (!text("id",36,command.id) || !text("profileId",36,command.profile) || !ValidBrowsingProfileId(command.profile)) { reject(); return; }
  } else if (method == "space.create") {
    command.action = TreeAction::CreateSpace; fields = 4;
    if (!text("title", 40, command.title) || !text("icon", 16, command.icon)) { reject(); return; }
    if (request->HasKey("profileId")) {
      ++fields;
      if (!text("profileId",36,command.profile) || !ValidBrowsingProfileId(command.profile)) { reject(); return; }
    }
  } else if (method == "tree.createFolder" || method == "tree.createTab") {
    command.action = method == "tree.createFolder" ? TreeAction::CreateFolder : TreeAction::CreateTab;
    fields = method == "tree.createFolder" ? 4 : 5;
    if (!text("parentId", 36, command.parent) || !text("title", 240, command.title)) { reject(); return; }
    if (command.action == TreeAction::CreateTab) {
      if (!text("url", 4096, command.url) || !UserUrlAllowed(command.url)) { reject(); return; }
      command.value = true;
    }
  } else {
    if (!text("id", 36, command.id)) { reject(); return; }
    if (method == "tree.rename") {
      command.action = TreeAction::Rename; fields = 4;
      if (!text("title", 240, command.title)) { reject(); return; }
    } else if (method == "tree.move" || method == "tree.place") {
      command.action = method == "tree.move" ? TreeAction::Move : TreeAction::Place; fields = method == "tree.move" ? 5 : 6;
      if (!text("parentId", 36, command.parent) || request->GetType("position") != VTYPE_INT) { reject(); return; }
      command.position = request->GetInt("position");
      if (method == "tree.place" && !text("section", 12, command.section)) { reject(); return; }
    } else if (method == "tree.pin" || method == "tree.expand") {
      command.action = method == "tree.pin" ? TreeAction::Pin : TreeAction::Expand; fields = 4;
      if (request->GetType("value") != VTYPE_BOOL) { reject(); return; }
      command.value = request->GetBool("value");
    } else if (method == "tree.usePageTitle") { command.action = TreeAction::AutomaticTitle; fields = 3; }
    else if (method == "tree.activate") { command.action = TreeAction::Activate; fields = 3; }
    else if (method == "tree.restore") { command.action = TreeAction::Restore; fields = 3; }
    else if (method == "tree.moveToSpace") {
      command.action = TreeAction::MoveToSpace; fields = 4;
      if (!text("space", 36, command.space)) { reject(); return; }
      // If the page on screen leaves, show the next one of this space.
      const auto order = OrderedTabsForSpace(tree_, tree_.active_space);
      for (const auto& candidate : order) if (candidate != command.id) { command.next_active = candidate; break; }
    }
    else if (method == "space.edit") {
      command.action = TreeAction::EditSpace; fields = 5;
      if (!text("title", 40, command.title) || !text("icon", 16, command.icon)) { reject(); return; }
    }
    else if (method == "space.delete") { command.action = TreeAction::DeleteSpace; fields = 3; }
    else if (method == "space.switch") { command.action = TreeAction::SwitchSpace; fields = 3; }
    else if (method == "space.move") {
      command.action = TreeAction::MoveSpace; fields = 4;
      if (request->GetType("position") != VTYPE_INT) { reject(); return; }
      command.position = request->GetInt("position");
    }
    else if (method == "tree.removeFolder") {
      command.action = TreeAction::RemoveLeaf; fields = 3;
      const auto node = std::find_if(tree_.nodes.begin(), tree_.nodes.end(), [&](const auto& n) { return n.id == command.id; });
      if (node == tree_.nodes.end() || !node->folder) { reject(); return; }
      if (request->HasKey("confirmation")) {
        if (request->GetSize()!=4 || request->GetType("confirmation")!=VTYPE_DICTIONARY) { reject(); return; }
        const auto confirmation=request->GetDictionary("confirmation");
        if (confirmation->GetSize()!=2 || confirmation->GetType("tabs")!=VTYPE_INT ||
            (confirmation->GetType("revision")!=VTYPE_INT && confirmation->GetType("revision")!=VTYPE_DOUBLE)) { reject(); return; }
        const double approved=confirmation->GetType("revision")==VTYPE_INT ? confirmation->GetInt("revision") : confirmation->GetDouble("revision");
        if (approved!=revision || command.expected_revision!=tree_.revision) { done(409,"Workspace changed; review folder removal again",nullptr); return; }
        command.confirmed_nodes=FolderNodeIds(tree_,command.id);
        const std::set<std::string> ids(command.confirmed_nodes.begin(),command.confirmed_nodes.end());
        const auto tabs=std::count_if(tree_.nodes.begin(),tree_.nodes.end(),[&](const auto& n) { return !n.folder && ids.contains(n.id); });
        if (confirmation->GetInt("tabs")!=tabs) { done(409,"Folder contents changed; review removal again",nullptr); return; }
        command.action=TreeAction::RemoveFolder; RemoveFolder(std::move(command),std::move(done)); return;
      }
    }
    else if (method == "tree.close" || method == "tree.archive" || method == "tree.reset") {
      if (request->GetSize() != 3) { reject(); return; }
      if (command.expected_revision != tree_.revision) { done(409, "Workspace changed; retry", nullptr); return; }
      const auto node = std::find_if(tree_.nodes.begin(), tree_.nodes.end(), [&](const auto& n) { return n.id == command.id; });
      if (node == tree_.nodes.end() || node->folder || node->archived) { reject(); return; }
      if (method != "tree.reset") { Close(command.id, method == "tree.archive", std::move(done)); return; }
      if (!node->pinned || !PageUrlAllowed(node->url)) { reject(); return; }
      const auto url = node->url, id = node->id;
      Dispatch(command, [weak = weak_from_this(), id, url, done = std::move(done)](int code, const std::string& error, CefRefPtr<CefDictionaryValue> snapshot) {
        if (auto self = weak.lock(); self && !code) {
          const auto live = self->sessions_.find(id);
          if (live != self->sessions_.end()) live->second->GetMainFrame()->LoadURL(url);
        }
        if (done) done(code, error, snapshot);
      }); return;
    }
    else { done(404, "Unknown workspace method", nullptr); return; }
  }
  if (request->GetSize() != fields) { reject(); return; }
  if (command.action==TreeAction::SetSpaceProfile || command.action==TreeAction::CreateProfile || command.action==TreeAction::ExportSpace) {
    ChangeProfile(std::move(command),std::move(done)); return;
  }
  if (command.action == TreeAction::MoveToSpace || command.action == TreeAction::Place || command.action == TreeAction::Move) {
    std::string destination = BrowsingProfileId(tree_);
    if (command.action == TreeAction::MoveToSpace) {
      const auto space = std::find_if(tree_.spaces.begin(),tree_.spaces.end(),[&](const auto& s) { return s.id == command.space; });
      if (space != tree_.spaces.end()) destination = space->profile;
    } else if (!command.parent.empty()) destination = BrowsingProfileId(tree_,command.parent);
    if (destination != BrowsingProfileId(tree_,command.id)) {
      auto ids = FolderNodeIds(tree_,command.id); if (ids.empty()) ids.push_back(command.id);
      for (const auto& split : tree_.splits) if (std::find(ids.begin(),ids.end(),split.anchor)!=ids.end() ||
          std::find(ids.begin(),ids.end(),split.other)!=ids.end()) {
        ids.push_back(split.anchor); ids.push_back(split.other);
      }
      for (const auto& window : session_->Windows()) if (window->workspace_data == data_ && window->workspace)
        for (const auto& id : ids) if (window->workspace->LiveBrowser(id) || window->workspace->creating_.contains(id) || window->workspace->parked_peeks_.contains(id)) {
          done(409,"Unload these tabs and close their Link Peeks before moving to another profile",nullptr); return;
        }
    }
  }
  if (command.action == TreeAction::Pin || command.action == TreeAction::Place || command.action == TreeAction::Move) {
    const auto live = sessions_.find(command.id);
    if (live != sessions_.end()) {
      auto url = live->second->GetMainFrame()->GetURL().ToString();
      if (PageUrlAllowed(url)) command.url = std::move(url);
    }
  }
  Dispatch(std::move(command), std::move(done));
}
}  // namespace red
