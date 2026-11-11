#include "app/browsing_profiles.h"
#include "app/session.h"
#include "windows/pointer.h"
#include "windows/panel_popup.h"
#include "windows/command_actions.h"
#include <dwmapi.h>
#include <algorithm>
#include <cmath>
#include <optional>
#include "downloads/download_manager.h"
#include "extensions/extension_catalog.h"
#include "windows/clipboard.h"
#include "windows/share_sheet.h"
#include "windows/extension_popup.h"
#include "windows/extensions_window.h"
#include "site/site_data.h"
#include "windows/embedded_content.h"
#include "site/site_settings.h"
#include "windows/boost_window.h"
#include "windows/ui_context_menu.h"
#include "windows/main_menu.h"
#include "history/history_commands.h"
#include "history/history_service.h"
#include "integrations/providers.h"
#include "integrations/live_reader.h"
#include "integrations/integration_previews.h"
#include <set>
#include "include/base/cef_callback.h"
#include "include/cef_app.h"
#include "include/cef_parser.h"
#include "include/views/cef_fill_layout.h"
#include "include/wrapper/cef_closure_task.h"
#include "trust_policy.h"
#include "windows/focus.h"
#include "windows/sidebar_peek.h"
#include "windows/window_close.h"
#include "windows/workspace_layout.h"
#include "windows/integration_card_bounds.h"
#include "workspace/controller.h"

namespace red {
namespace {
constexpr int kPanelWidth = 320, kHistoryWidth = 420;

class PanelClient final : public CefClient, public CefLifeSpanHandler, public CefLoadHandler, public CefDisplayHandler,
    public CefRequestHandler, public CefKeyboardHandler, public CefMessageRouterBrowserSide::Handler {
 public:
  PanelClient(std::weak_ptr<PanelPopup> owner, std::weak_ptr<State> state) : owner_(owner), state_(state) {
    router_ = CefMessageRouterBrowserSide::Create(CefMessageRouterConfig()); router_->AddHandler(this, false);
  }
  ~PanelClient() override { router_->RemoveHandler(this); }
  CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return this; }
  CefRefPtr<CefLoadHandler> GetLoadHandler() override { return this; }
  CefRefPtr<CefDisplayHandler> GetDisplayHandler() override { return this; }
  CefRefPtr<CefRequestHandler> GetRequestHandler() override { return this; }
  CefRefPtr<CefKeyboardHandler> GetKeyboardHandler() override { return this; }
  CefRefPtr<CefContextMenuHandler> GetContextMenuHandler() override {
    const auto owner = owner_;
    return new UiContextMenu(state_, {[owner] { const auto o = owner.lock(); return o ? o->window() : nullptr; },
                                      [owner] { if (const auto o = owner.lock()) o->MenuOpened(); },
                                      [owner] { if (const auto o = owner.lock()) o->MenuClosed(); }});
  }
  bool OnConsoleMessage(CefRefPtr<CefBrowser>, cef_log_severity_t, const CefString& message, const CefString&, int) override {
    // Probe evidence of what the popover actually rendered (fixed JSON from our UI).
    const auto text = message.ToString();
    if (auto s=state_.lock();s && s->probe && s->sidebar_peek_probe && text.starts_with("RED_INTEGRATION:")) {
      const auto v=CefParseJSON(text.substr(16),JSON_PARSER_RFC);
      if(v && v->GetType()==VTYPE_DICTIONARY)s->Record("integration_document",v->GetDictionary());
    }
    if (auto s = state_.lock(); s && s->sidebar_peek_probe && text.starts_with("RED_PIN_EDITOR:")) {
      const auto v = CefParseJSON(text.substr(15), JSON_PARSER_RFC);
      if (v && v->GetType() == VTYPE_DICTIONARY) s->Record("pin_editor_document", v->GetDictionary());
    }
    if (auto s = state_.lock(); s && s->probe && text.starts_with("RED_PANEL_RENDERED:")) {
      auto v = CefParseJSON(text.substr(19), JSON_PARSER_RFC);
      if (v && v->GetType() == VTYPE_DICTIONARY) s->Record("panel_rendered", v->GetDictionary());
    }
    if(auto s=state_.lock();s&&s->palette_probe)for(const std::string prefix:{"RED_PALETTE_PROBE:","RED_PALETTE_REOPEN:"})if(text.starts_with(prefix)) {
      const auto value=CefParseJSON(text.substr(prefix.size()),JSON_PARSER_RFC);
      if(value&&value->GetType()==VTYPE_DICTIONARY){const auto data=value->GetDictionary();const bool reopen=prefix=="RED_PALETTE_REOPEN:";
        // Record transfers ownership of the dictionary into its JSON row.
        // Read the outcome first, while this dictionary is still valid.
        const bool passed=data->GetBool("passed");
        s->Record(reopen?"palette_editor_reopen":"palette_editor_probe",data);
        if(reopen)s->palette_checks=passed;
        else if(passed) {
          if(auto panel=owner_.lock())panel->Hide(true);
          CefPostDelayedTask(TID_UI,base::BindOnce([](std::weak_ptr<State> weak){if(auto state=weak.lock();state&&state->panel_popup)state->panel_popup->Toggle("theme",CefRect(14,3,24,24));},std::weak_ptr<State>(s)),300);
        }
      }
    }
    return false;
  }
  void OnAfterCreated(CefRefPtr<CefBrowser> b) override { if (auto o = owner_.lock()) o->Created(b); }
  void OnBeforeClose(CefRefPtr<CefBrowser> b) override { router_->OnBeforeClose(b); if (auto o = owner_.lock()) o->Closed(b->GetIdentifier()); }
  bool OnBeforePopup(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, int, const CefString&, const CefString&,
      cef_window_open_disposition_t, bool, const CefPopupFeatures&, CefWindowInfo&, CefRefPtr<CefClient>&,
      CefBrowserSettings&, CefRefPtr<CefDictionaryValue>&, bool*) override { return true; }
  bool OnBeforeBrowse(CefRefPtr<CefBrowser> b, CefRefPtr<CefFrame> f, CefRefPtr<CefRequest> r, bool, bool) override {
    router_->OnBeforeBrowse(b, f); return !f->IsMain() || r->GetURL().ToString() != kPanelUrl;
  }
  void OnRenderProcessTerminated(CefRefPtr<CefBrowser> b, TerminationStatus, int, const CefString&) override {
    router_->OnRenderProcessTerminated(b);
    if (auto o = owner_.lock()) o->Shutdown();
  }
  void OnLoadEnd(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame> f, int) override { if (f->IsMain()) if (auto o = owner_.lock()) o->Ready(); }
  bool OnPreKeyEvent(CefRefPtr<CefBrowser>, const CefKeyEvent& e, CefEventHandle, bool*) override {
    if (e.type == KEYEVENT_RAWKEYDOWN && e.windows_key_code == VK_F4 && (e.modifiers & EVENTFLAG_ALT_DOWN)) {
      if (auto o = owner_.lock(); o && o->modal()) { o->Hide(true); return true; }
      if (auto s = state_.lock(); s && s->window) { s->window->Close(); return true; }
    }
    return false;
  }
  bool OnProcessMessageReceived(CefRefPtr<CefBrowser> b, CefRefPtr<CefFrame> f, CefProcessId p, CefRefPtr<CefProcessMessage> m) override {
    if (auto s = state_.lock(); s && s->probe && p == PID_RENDERER && f->IsMain() && m->GetName() == "red.probe.token") {
      auto d = m->GetArgumentList()->GetDictionary(0)->Copy(false); d->SetString("role", "panel"); s->Record("renderer_token", d); return true;
    }
    return router_->OnProcessMessageReceived(b, f, p, m);
  }
  bool OnQuery(CefRefPtr<CefBrowser> b, CefRefPtr<CefFrame> f, int64_t q, const CefString& r, bool p, CefRefPtr<Callback> cb) override {
    if (auto o = owner_.lock()) return o->Query(b, f, q, r, p, cb); return false;
  }
  void OnQueryCanceled(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, int64_t q) override { if (auto o = owner_.lock()) o->Cancel(q); }
 private:
  std::weak_ptr<PanelPopup> owner_; std::weak_ptr<State> state_;
  CefRefPtr<CefMessageRouterBrowserSide> router_;
  IMPLEMENT_REFCOUNTING(PanelClient);
};
class PanelView final : public CefBrowserViewDelegate {
 public: cef_runtime_style_t GetBrowserRuntimeStyle() override { return CEF_RUNTIME_STYLE_ALLOY; }
 private: IMPLEMENT_REFCOUNTING(PanelView);
};
class PanelWindow final : public CefWindowDelegate {
 public:
  PanelWindow(std::weak_ptr<PanelPopup> owner, CefRefPtr<CefBrowserView> view, bool passive)
      : owner_(owner), view_(view), passive_(passive) {}
  cef_runtime_style_t GetWindowRuntimeStyle() override { return CEF_RUNTIME_STYLE_ALLOY; }
  bool IsFrameless(CefRefPtr<CefWindow>) override { return true; }
  bool CanResize(CefRefPtr<CefWindow>) override { return false; }
  bool CanMaximize(CefRefPtr<CefWindow>) override { return false; }
  bool CanMinimize(CefRefPtr<CefWindow>) override { return false; }
  CefRefPtr<CefWindow> GetParentWindow(CefRefPtr<CefWindow>, bool* menu, bool* activate) override {
    *menu = passive_; *activate = !passive_;
    if (auto o = owner_.lock()) return o->parent(); return nullptr;
  }
  CefSize GetPreferredSize(CefRefPtr<CefView>) override { return CefSize(kPanelWidth, 120); }
  void OnWindowCreated(CefRefPtr<CefWindow> w) override {
    // Dark by default (user preference); matches the document's background.
    // Always dark; the theme only rotates its tint.
    const auto background = kPopupSurface;
    w->SetTitle("Sol — Panel"); w->SetBackgroundColor(CefColorSetARGB(255, background.r, background.g, background.b));
    const DWM_WINDOW_CORNER_PREFERENCE corners = DWMWCP_ROUND;
    DwmSetWindowAttribute(w->GetWindowHandle(), DWMWA_WINDOW_CORNER_PREFERENCE, &corners, sizeof(corners));
    const BOOL dark = TRUE;  // Dark DWM border and shadow on Windows 11.
    DwmSetWindowAttribute(w->GetWindowHandle(), DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
    w->SetToFillLayout(); w->AddChildView(view_);
    if (auto o = owner_.lock()) o->Attach(w);
  }
  // Clicking anywhere else closes the popover, like Arc's.
  void OnWindowActivationChanged(CefRefPtr<CefWindow>, bool active) override {
    if (!active && !passive_) if (auto o = owner_.lock(); o && o->is_presented()) o->Deactivated();
  }
  bool CanClose(CefRefPtr<CefWindow>) override {
    if (auto o = owner_.lock(); o && !o->shutting_down()) { o->Hide(true); return false; } return true;
  }
  void OnWindowDestroyed(CefRefPtr<CefWindow>) override { view_ = nullptr; if (auto o = owner_.lock()) o->Destroyed(); }
 private:
  std::weak_ptr<PanelPopup> owner_; CefRefPtr<CefBrowserView> view_; bool passive_;
  IMPLEMENT_REFCOUNTING(PanelWindow);
};
// Download row menu. Commands map to a fixed action list; the download ID was
// validated against the native list before the menu opened.
class DownloadMenuDelegate final : public CefMenuModelDelegate {
 public:
  DownloadMenuDelegate(const std::shared_ptr<State>& state, std::weak_ptr<PanelPopup> owner, int id)
      : state_(state), owner_(owner), id_(id), downloads_(state->downloads) {}
  void MenuClosed(CefRefPtr<CefMenuModel>) override { if (auto o = owner_.lock()) o->MenuClosed(); }
  void ExecuteCommand(CefRefPtr<CefMenuModel>, int command, cef_event_flags_t) override {
    static constexpr const char* kActions[] = {"open", "copy", "show", "remove", "recycle"};
    if (command < 28000 || command >= 28000 + 5) return;
    auto state = state_.lock(); if (!state || !downloads_) return;
    std::string error;
    if (downloads_->Control(static_cast<uint32_t>(id_), kActions[command - 28000], error) && state->workspace)
      state->workspace->Notify(error);
  }
 private:
  std::weak_ptr<State> state_; std::weak_ptr<PanelPopup> owner_; int id_;
  std::shared_ptr<DownloadManager> downloads_;
  IMPLEMENT_REFCOUNTING(DownloadMenuDelegate);
};
// Site popover "…" menu (Arc's): fixed commands for the visible page.
enum SiteMenu { kClearCache = 28100, kClearCookies, kManageExtensions, kAddExtension, kAllSiteSettings };
class SiteMenuDelegate final : public CefMenuModelDelegate {
 public:
  SiteMenuDelegate(std::weak_ptr<State> state, std::weak_ptr<PanelPopup> owner) : state_(state), owner_(owner) {}
  void MenuClosed(CefRefPtr<CefMenuModel>) override { if (auto o = owner_.lock()) o->MenuClosed(); }
  void ExecuteCommand(CefRefPtr<CefMenuModel>, int command, cef_event_flags_t) override {
    auto state = state_.lock(); auto owner = owner_.lock(); if (!state || !owner || !state->workspace) return;
    if (command == kAllSiteSettings) { owner->ShowSiteDetails(); return; }
    owner->MenuDone(); owner->Hide(command != kManageExtensions);
    if (command == kClearCache) ClearSiteCache(state);
    else if (command == kClearCookies) ClearSiteCookies(state);
    else if (command == kAddExtension) state->workspace->OpenLink("https://chromewebstore.live.com/", true);
    else if (command == kManageExtensions) {
      if (!state->extensions_window) state->extensions_window = std::make_shared<ExtensionsWindow>(state);
      state->extensions_window->Show();
    }
  }
 private:
  std::weak_ptr<State> state_; std::weak_ptr<PanelPopup> owner_;
  IMPLEMENT_REFCOUNTING(SiteMenuDelegate);
};
}  // namespace

void PanelPopup::ShowSiteDetails() { ++site_details_; Publish(); }
void PanelPopup::SiteMenu(int x, int y, CefRefPtr<CefMessageRouterBrowserSide::Callback> callback) {
  auto s = state_.lock();
  const auto bounds = window_->GetBounds();
  if (x < 0 || y < 0 || x > bounds.width || y > bounds.height) { callback->Failure(400, "Invalid site menu"); return; }
  const bool site = !SiteOrigin(s->content).empty();
  auto model = CefMenuModel::CreateMenuModel(new SiteMenuDelegate(s, weak_from_this()));
  model->AddItem(kClearCache, "Clear Cache"); model->AddItem(kClearCookies, "Clear Cookies");
  model->AddSeparator();
  model->AddItem(kManageExtensions, "Manage Extensions"); model->AddItem(kAddExtension, "Add Extension");
  model->AddSeparator();
  model->AddItem(kAllSiteSettings, "All Site Settings");
  for (const int command : {kClearCache, kClearCookies, kAllSiteSettings}) model->SetEnabled(command, site);
  callback->Success(Response());
  menu_open_ = true;
  window_->ShowMenu(model, CefPoint(bounds.x + x, bounds.y + y), CEF_MENU_ANCHOR_TOPLEFT);
}

CefRefPtr<CefWindow> PanelPopup::parent() const { auto s = state_.lock(); return s ? s->window : nullptr; }
void PanelPopup::Created(CefRefPtr<CefBrowser> b) {
  browser_ = b;
  if (auto s = state_.lock()) {
    (hover_only_ ? s->integration_panel_id : s->panel_id) = b->GetIdentifier();
    RegisterSessionUi(s, b->GetIdentifier()); s->browsers[b->GetIdentifier()] = b; s->Record("panel_created");
  }
}
void PanelPopup::Closed(int id) {
  observer_ = nullptr; browser_ = nullptr;
  if (auto s = state_.lock()) {
    UnregisterSessionUi(s, id); s->browsers.erase(id);
    (hover_only_ ? s->integration_panel_id : s->panel_id) = -1;
    if (s->window_close) s->window_close->OnClosed(id);
    if (!s->window && s->browsers.empty()) QuitIfLastWindow(s);
  }
}
void PanelPopup::RestoreModalOwner() {
  const HWND owner = modal_owner_;
  modal_owner_ = nullptr;
  if (owner) if (const auto s = state_.lock(); s && s->embedded) s->embedded->SetModalShade(false);
  if (IsWindow(owner)) EnableWindow(owner, TRUE);
}
void PanelPopup::Destroyed() {
  RestoreModalOwner(); presentation_.Reset(); window_ = nullptr; ready_ = prepared_ = presented_ = false;
}
void PanelPopup::Reposition() {
  if (hover_ && hover_->is_open()) hover_->Reposition();
  auto p = parent(); if (!p || !window_) return;
  if (presentation_.closing()) { presentation_.Conceal(); return; }
  const auto root = p->GetClientAreaBoundsInScreen();
  const int height = std::clamp(height_, 60, std::max(60, root.height - kToolbarHeight - 16));
  // Center on the opening button where possible. At the window edges retain
  // the button's actual horizontal position as the growth origin, rather than
  // making the card grow from its unrelated geometric center.
  const bool compact_integration = hover_only_ && integration_ &&
      (integration_->HasKey("kind") || integration_->GetString("service") == "figma");
  const bool modal = this->modal();
  const int desired=kind_=="moveToProfile" ? 360 : kind_=="history" || modal ? kHistoryWidth : compact_integration ? 248 : kPanelWidth;
  const bool editor=kind_=="space" || kind_=="newSpace" || kind_=="theme";
  const auto state=state_.lock();
  const auto page=WorkspaceContentArea(root.width,root.height,state && state->sidebar_visible,state?state->sidebar_width:kSidebarWidth);
  const int width = std::max(1, std::min(desired, editor?page.width-32:root.width-16));
  const float button_center = anchor_.x + anchor_.width / 2.f;
  int x = static_cast<int>(std::lround(button_center - width / 2.f));
  int y = kind_ == "archive" ? anchor_.y - height - 6 : anchor_.y + anchor_.height + 6;
  x = std::clamp(x, 8, std::max(8, root.width - width - 8));
  y = std::clamp(y, kToolbarHeight, std::max(kToolbarHeight, root.height - height - 8));
  if (editor) { x=page.x+std::min(24,std::max(0,(page.width-width)/2)); y=std::min(kToolbarHeight+24,std::max(kToolbarHeight,root.height-height-8)); }
  origin_x_ = std::clamp((button_center - x) / width, 0.f, 1.f);
  if (editor) origin_x_=0;
  if (modal) { x = (root.width - width) / 2; y = (root.height - height) / 2; origin_x_ = .5f; }
  CefRect target(root.x + x, root.y + y, width, height);
  if (hover_only_) {
    const auto placement = PlaceIntegrationCard(root, anchor_, width, height, kToolbarHeight, integration_favorite_);
    target = placement.bounds; origin_x_ = placement.origin_x; integration_origin_y_ = placement.origin_y;
  }
  if (window_->GetBounds() != target) { if (presented_) presentation_.Settle(); window_->SetBounds(target); }
  if (modal_owner_ && state && state->embedded) state->embedded->SetModalShade(true);
}
bool PanelPopup::EditPinnedPage(const std::string& id) {
  const auto s = state_.lock();
  if (!s || !s->workspace || shutdown_ || modal() || menu_open_ || !PopupOwnerIsForeground(s->window) ||
      !IsWindowEnabled(s->window->GetWindowHandle())) return false;
  const auto snapshot = s->workspace->Snapshot();
  const auto nodes = snapshot->GetList("nodes");
  for (size_t i = 0; nodes && i < nodes->GetSize(); ++i) {
    const auto node = nodes->GetDictionary(i);
    if (node->GetString("id") != id || node->GetString("kind") != "tab" || node->GetBool("archived") ||
        node->GetBool("closing") || !node->GetBool("pinned") ||
        (!node->GetBool("favorite") && node->GetString("space") != snapshot->GetString("activeSpace"))) continue;
    pinned_page_ = CefDictionaryValue::Create();
    pinned_page_->SetString("id", id);
    auto title = node->GetString("title");
    if (!node->GetBool("customTitle") && !node->GetString("pageTitle").empty() &&
        node->GetString("currentUrl") == node->GetString("url")) title = node->GetString("pageTitle");
    pinned_page_->SetString("title", title);
    pinned_page_->SetString("url", node->GetString("url"));
    Toggle("editPinned", {});
    return editing_pin();
  }
  return false;
}
bool PanelPopup::ConfirmProfileMove(const std::string& id, const std::string& space) {
  const auto s = state_.lock();
  if (!s || !s->workspace || shutdown_ || modal() || menu_open_ || !PopupOwnerIsForeground(s->window) ||
      !IsWindowEnabled(s->window->GetWindowHandle())) return false;
  const auto snapshot = s->workspace->Snapshot();
  const auto spaces = snapshot->GetList("spaces");
  auto profile_of = [&](const std::string& wanted, std::string* name) -> std::optional<std::string> {
    for (size_t i = 0; spaces && i < spaces->GetSize(); ++i) {
      const auto row = spaces->GetDictionary(i);
      if (row->GetString("id") != wanted) continue;
      if (name) *name = row->GetString("name").ToString();
      const auto profile = row->GetString("profileId").ToString();
      return profile.empty() ? std::string("default") : profile;
    }
    return std::nullopt;
  };
  std::string space_name;
  const auto target = profile_of(space, &space_name);
  if (!target) return false;
  const auto nodes = snapshot->GetList("nodes");
  for (size_t i = 0; nodes && i < nodes->GetSize(); ++i) {
    const auto node = nodes->GetDictionary(i);
    if (node->GetString("id") != id || node->GetBool("archived") || node->GetBool("closing")) continue;
    // Only a move across profiles needs this confirmation.
    const auto source = profile_of(node->GetString("space").ToString(), nullptr);
    if (!source || *source == *target) return false;
    auto title = node->GetString("title").ToString();
    if (node->GetString("kind") == "tab" && !node->GetBool("customTitle") && !node->GetString("pageTitle").empty())
      title = node->GetString("pageTitle").ToString();
    profile_move_ = CefDictionaryValue::Create();
    profile_move_->SetString("id", id);
    profile_move_->SetString("space", space);
    profile_move_->SetString("title", title.substr(0, 240));
    profile_move_->SetString("spaceName", space_name.substr(0, 120));
    moving_tab_ = false;
    Toggle("moveToProfile", {});
    return open_ && kind_ == "moveToProfile";
  }
  return false;
}
void PanelPopup::Toggle(const std::string& kind, const CefRect& anchor) {
  auto s = state_.lock(); if (!s || !s->window || !s->workspace || s->workspace->WindowClosing() || shutdown_) return;
  if (hover_only_ != (kind == "integration") || modal()) return;
  if (hover_) hover_->DismissIntegration();
  if (open_ && kind == kind_) { Hide(true, true); return; }
  DropDraftTheme();
  kind_ = kind; anchor_ = anchor; ++request_; open_ = true;
  opened_ms_ = GetTickCount64();
  prepared_ = presented_ = false; presentation_.Conceal();
  if (!window_) {
    auto shell = s->browsers.find(s->shell_id); if (shell == s->browsers.end()) return;
    CefBrowserSettings settings;
    const auto background = kPopupSurface;
    settings.background_color = CefColorSetARGB(255, background.r, background.g, background.b);
    auto extra = CefDictionaryValue::Create(); extra->SetBool("trusted_ui", true);
    auto view = CefBrowserView::CreateBrowserView(new PanelClient(weak_from_this(), s), std::string(kPanelUrl),
        settings, extra, shell->second->GetHost()->GetRequestContext(), new PanelView());
    view->SetBackgroundColor(settings.background_color);
    CefWindow::CreateTopLevelWindow(new PanelWindow(weak_from_this(), view, hover_only_));
  }
  Reposition(); Publish();
  auto d = CefDictionaryValue::Create(); d->SetString("kind", kind); s->Record("panel_open", d);
}
void PanelPopup::Ready() { ready_ = true; Publish(); }
bool PanelPopup::PreviewIntegration(const std::string& node, const CefRect& anchor, HWND surface) {
  const auto s = state_.lock();
  if (!s || !s->workspace || s->private_window || s->workspace->WindowClosing() ||
      s->window->IsMinimized() || !IsWindowVisible(surface) || shutdown_) return false;
  if (!hover_only_) {
    if (open_ || presentation_.closing()) return false;
    if (!hover_ || (hover_->shutting_down() && !hover_->browser() && !hover_->window()))
      hover_.reset(new PanelPopup(state_, true));
    return hover_->PreviewIntegration(node, anchor, surface);
  }
  const auto target = s->workspace->SavedPreview(node);
  const auto seed = s->workspace->DetachedSeed(node);
  if (!seed || !seed->pinned) return false;
  const auto provider = !target ? RecentIntegration(seed->url) : std::nullopt;
  const auto live = target ? LiveIntegration(*target) : std::nullopt;
  const auto profile = provider || live ? s->workspace->ProfileForNode(node) : nullptr;
  if ((!provider && !live) || !BrowserIntegrationEnabled(s, live ? live->id : provider->id) ||
      !profile || (!live && !profile->history) || profile != s->workspace->ProfileForNode("")) return false;
  POINT cursor{};
  const HWND root = s->window->GetWindowHandle();
  if (!GetCursorPos(&cursor) || GetAncestor(WindowFromPoint(cursor),GA_ROOT) != GetAncestor(surface,GA_ROOT) ||
      !ScreenToClient(root,&cursor)) return false;
  const float scale = GetDpiForWindow(root)/96.f;
  const float x = cursor.x/scale, y = cursor.y/scale;
  if (x < anchor.x || x >= anchor.x+anchor.width || y < anchor.y || y >= anchor.y+anchor.height) return false;
  if (open_ && integration_ && integration_->GetString("node") == node && integration_surface_ == surface &&
      integration_favorite_ == seed->favorite) return true;
  if (open_) Hide(false);
  integration_ = CefDictionaryValue::Create();
  integration_->SetString("node", node); integration_->SetString("service", live ? live->id : provider->id); integration_->SetString("name", live ? live->name : provider->name);
  if (live) { integration_->SetString("kind", live->IsMail() ? "mail" : "calendar"); integration_->SetString("status", "loading"); }
  integration_->SetBool("loading", true); integration_->SetList("items", CefListValue::Create());
  integration_->SetInt("revision",0);
  integration_source_ = target ? *target : seed->url;
  integration_profile_ = profile;
  integration_cached_ = target.has_value();
  integration_favorite_ = seed->favorite;
  integration_cache_revision_ = -1;
  integration_surface_ = surface;
  Toggle("integration", anchor); integration_left_ = 0;
  const int opening = request_;
  if (live) {
    // The card shows the last snapshot at once; one older than 30 s is read again and replaces it.
    if (const auto cache = s->workspace->integration_previews()) cache->RefreshIfOlder(integration_source_, std::chrono::seconds(30));
    WatchIntegration(opening); return true;
  }
  profile->history->Suggest(provider->query, 300, [weak = weak_from_this(), opening, node, policy = *provider, profile](std::vector<Visit> visits) {
    const auto self = weak.lock(); if (!self || !self->open_ || self->request_ != opening || !self->integration_) return;
    const auto state = self->state_.lock();
    const auto current = state && state->workspace ? state->workspace->DetachedSeed(node) : std::nullopt;
    if (!current || !BrowserIntegrationEnabled(state, policy.id) || current->url != self->integration_source_ ||
        state->workspace->ProfileForNode(node) != profile || state->workspace->ProfileForNode("") != profile) { self->Hide(false); return; }
    // Visits arrive newest first. A Figma file appears once (its latest visit), named after the file:
    // the newest visit whose title carries the name, else the name in its address, else "Untitled".
    struct Row { std::string url, title; int64_t visited_at; bool named; };
    std::vector<Row> rows; std::map<std::string, size_t> seen;
    const bool figma = policy.id == "figma";
    for (const auto& visit : visits) {
      if (!RecentIntegrationPage(policy, visit.url)) continue;
      const auto file = figma ? FigmaFileKey(visit.url) : std::string();
      const auto key = file.empty() ? visit.url : "file:" + file;
      std::string title = visit.title; bool named = true;
      if (figma && !file.empty()) {
        title = FigmaTitleName(visit.title);
        if (title.empty()) { title = FigmaUrlName(visit.url); named = false; }
      }
      if (const auto found = seen.find(key); found != seen.end()) {
        auto& row = rows[found->second];
        if (!row.named && (named || row.title.empty()) && !title.empty()) { row.title = title; row.named = named; }
        continue;
      }
      if (rows.size() == 8) continue;
      seen.emplace(key, rows.size());
      rows.push_back({visit.url, title, visit.visited_at, named});
    }
    auto items = CefListValue::Create();
    for (const auto& entry : rows) {
      auto row = CefDictionaryValue::Create(); row->SetString("url", entry.url);
      row->SetString("title", (entry.title.empty() && figma ? std::string("Untitled") : entry.title).substr(0, 400));
      if (figma && entry.visited_at >= 0 && entry.visited_at <= 8640000000000000LL)
        row->SetDouble("visitedAt", static_cast<double>(entry.visited_at));
      items->SetDictionary(items->GetSize(), row);
    }
    self->integration_->SetList("items", items); self->integration_->SetBool("loading", false); self->Publish();
  });
  WatchIntegration(opening); return true;
}
bool PanelPopup::DismissIntegration() {
  if (hover_) return hover_->DismissIntegration();
  if (!hover_only_ || !open_) return false;
  Hide(false); return true;
}
bool PanelPopup::PreviewOpeningValid(const std::shared_ptr<State>& s) const {
  if (!s || !s->workspace || !integration_ || !integration_cached_) return false;
  const auto id = integration_->GetString("node").ToString();
  const auto profile = integration_profile_.lock();
  return profile && s->workspace->ProfileForNode("") == profile && s->workspace->ProfileForNode(id) == profile &&
      s->workspace->SavedPreview(id) == integration_source_;
}
void PanelPopup::SyncIntegrationPreview() {
  const auto s = state_.lock();
  if (!PreviewOpeningValid(s)) { Hide(false); return; }
  const auto cache = s->workspace->integration_previews();
  const auto result = cache ? cache->Cached(integration_source_) : nullptr;
  const int revision = result ? result->GetInt("revision") : cache && cache->Queued(integration_source_) ? 0 : -2;
  if (integration_cache_revision_ == revision) return;
  integration_cache_revision_ = revision;
  integration_->SetInt("revision",integration_->GetInt("revision")+1);
  integration_->SetBool("loading",revision == 0);
  integration_->SetString("status",result ? result->GetString("status") : CefString(revision == 0 ? "loading" : "unsupported"));
  integration_->SetBool("composeAvailable",result && result->GetBool("composeAvailable"));
  if (result && result->GetType("mailView") == VTYPE_STRING) integration_->SetString("mailView",result->GetString("mailView"));
  else integration_->Remove("mailView");
  integration_->SetList("items",result ? result->GetList("items")->Copy() : CefListValue::Create());
}
namespace {
using PanelReply = CefRefPtr<CefMessageRouterBrowserSide::Callback>;
struct PinnedPreviewAction {
  std::weak_ptr<State> state;
  std::weak_ptr<int> opening;
  std::shared_ptr<BrowsingProfile> profile;
  std::string node, url, target, token, service;
  PanelReply reply;
  ULONGLONG deadline;
  bool compose, refresh_mail;
  bool navigated = false;
  std::string initial_url;
  int browser_id = -1;
};
void RefreshOpenedMail(std::shared_ptr<PinnedPreviewAction> action, bool wait_for_navigation) {
  const auto s = action->state.lock();
  if (!s || !s->workspace || s->workspace->WindowClosing() ||
      s->workspace->ProfileForNode("") != action->profile || s->workspace->ProfileForNode(action->node) != action->profile ||
      s->workspace->SavedPreview(action->node) != action->target) return;
  if (wait_for_navigation) {
    const auto browser = s->workspace->LiveBrowser(action->node);
    const auto frame = browser ? browser->GetMainFrame() : nullptr;
    if (!frame || browser->GetIdentifier() != action->browser_id) return;
    const auto source = frame->GetURL().ToString();
    const auto provider = LiveIntegration(action->target);
    const bool opened = source == action->url || (provider && source != action->initial_url &&
        source.starts_with(provider->base + "#all/") && MailboxTarget(source) == action->target);
    if (!opened || browser->IsLoading()) {
      if (GetTickCount64() < action->deadline)
        CefPostDelayedTask(TID_UI,base::BindOnce(&RefreshOpenedMail,action,true),350);
      return;
    }
  }
  if (const auto cache = s->workspace->integration_previews()) cache->RefreshAfterMailOpen(action->target);
}
void RunPinnedPreview(std::shared_ptr<PinnedPreviewAction> action) {
  const auto s = action->state.lock();
  if (action->opening.expired() || !s || !s->workspace || s->workspace->WindowClosing() ||
      !BrowserIntegrationEnabled(s, action->service) ||
      s->workspace->ActiveNode() != action->node ||
      s->workspace->ProfileForNode(action->node) != action->profile || s->workspace->ProfileForNode("") != action->profile ||
      (!action->target.empty() && s->workspace->SavedPreview(action->node) != action->target)) {
    action->reply->Failure(409,"Preview tab changed");
    return;
  }
  if (GetTickCount64() >= action->deadline) {
    s->workspace->Notify(action->compose ? "Couldn't open the compose form" : "Couldn't open the selected preview item");
    action->reply->Failure(409,"Preview item unavailable");
    return;
  }
  if (s->workspace->HasLinkPeek()) {
    s->workspace->DismissLinkPeek([action](bool closed) {
      if (closed) RunPinnedPreview(action);
      else action->reply->Failure(409,"The link preview is still open");
    });
    return;
  }
  const auto browser = s->workspace->LiveBrowser(action->node);
  const auto frame = browser ? browser->GetMainFrame() : nullptr;
  const auto retry = [action] {
    CefPostDelayedTask(TID_UI,base::BindOnce(&RunPinnedPreview,action),350);
  };
  if (!frame) { retry(); return; }
  if (action->browser_id < 0) action->browser_id = browser->GetIdentifier();
  if (action->browser_id != browser->GetIdentifier()) {
    action->reply->Failure(409,"Preview tab changed");
    return;
  }
  const auto source = frame->GetURL().ToString();
  const bool click = action->compose || !action->token.empty();
  const bool on_target = IsPreviewPage(action->target,source) &&
      (!IsCalendarDayView(action->url) || action->navigated || source == action->url);
  if (!action->navigated && (!click || !on_target)) {
    action->initial_url = source;
    action->navigated = true;
    s->workspace->NavigatePinned(action->node,action->url,[action,click,retry](int code,const std::string& error,CefRefPtr<CefDictionaryValue>) {
      if (code) action->reply->Failure(code,error);
      else if (click) retry();
      else {
        if (action->refresh_mail) RefreshOpenedMail(action,true);
        action->reply->Success("{}");
      }
    });
    return;
  }
  if (on_target) {
    if (const auto provider=LiveIntegration(source)) {
      LiveRead::Click(browser,*provider,source,action->compose?LiveRead::Action::Compose:LiveRead::Action::Open,
          action->token,[action,retry](bool clicked) {
            if (!clicked) { retry(); return; }
            if (action->refresh_mail) RefreshOpenedMail(action,false);
            action->reply->Success("{}");
          });
    }
    return;
  }
  if (source.empty() || source == "about:blank" || source == action->initial_url || PreviewTarget(source) == action->target) {
    retry();
  } else {
    action->reply->Failure(409,"Preview tab changed");
  }
}
}
void PanelPopup::OpenCachedPreview(std::string token, bool compose, PanelReply cb) {
  const auto s = state_.lock();
  if (!PreviewOpeningValid(s)) { cb->Failure(409,"Preview changed"); return; }
  const auto target = integration_source_;
  const auto provider = LiveIntegration(target);
  if (!provider || (!token.empty() && !IntegrationRowToken(*provider, token))) {
    cb->Failure(403,"Invalid preview item"); return;
  }
  const bool direct = !compose && (token.empty() || provider->id == "gmail");
  const bool refresh_mail = provider->IsMail() && !compose && !token.empty();
  const auto url = direct && !token.empty() ? provider->base + "#all/" + token :
      provider->id == "googleCalendar" ? provider->base + "r/day" : target;
  OpenPreviewInPin(url,direct ? std::string{} : std::move(token),compose,cb,refresh_mail);
}
void PanelPopup::OpenPreviewInPin(const std::string& url, std::string token, bool compose, PanelReply cb, bool refresh_mail) {
  const auto s = state_.lock();
  const auto profile = integration_profile_.lock();
  if (!s || !s->workspace || !profile || !integration_) {
    cb->Failure(409,"Preview changed");
    return;
  }
  const auto node = integration_->GetString("node").ToString();
  pending_integration_action_ = std::make_shared<int>(0);
  auto action = std::make_shared<PinnedPreviewAction>(PinnedPreviewAction{
      state_,pending_integration_action_,profile,node,url,integration_cached_?integration_source_:std::string{},
      std::move(token),integration_->GetString("service").ToString(),cb,GetTickCount64()+12000,compose,refresh_mail,false,{},-1});
  Hide(false);
  auto activate = CefDictionaryValue::Create();
  activate->SetString("method","tree.activate");
  activate->SetString("id",node);
  activate->SetDouble("expectedRevision",s->workspace->Snapshot()->GetDouble("treeRevision"));
  s->workspace->Handle(activate,[action](int code,const std::string& error,CefRefPtr<CefDictionaryValue>) {
    if (code) action->reply->Failure(code,error);
    else RunPinnedPreview(action);
  });
}
void PanelPopup::WatchIntegration(int opening) {
  if (!open_ || request_ != opening || kind_ != "integration") return;
  const auto s = state_.lock();
  if (!s || !s->workspace || s->workspace->WindowClosing() || !IntegrationOwnerVisible()) { Hide(false); return; }
  if (integration_cached_) {
    if (!PreviewOpeningValid(s)) { Hide(false); return; }
  } else {
    const auto node = integration_ ? s->workspace->DetachedSeed(integration_->GetString("node")) : std::nullopt;
    if (!node || !node->pinned || node->archived || node->url != integration_source_ ||
        s->workspace->ProfileForNode(node->id) != s->workspace->ProfileForNode("")) { Hide(false); return; }
  }
  // A short grace period spans the gap between the tile and the owned card.
  if (PointerOverIntegration()) integration_left_ = 0;
  else if (!integration_left_) integration_left_ = GetTickCount64();
  else if (GetTickCount64() - integration_left_ > 350) { Hide(false); return; }
  CefPostDelayedTask(TID_UI, base::BindOnce([](std::weak_ptr<PanelPopup> weak, int request) {
    if (auto self = weak.lock()) self->WatchIntegration(request);
  }, weak_from_this(), opening), 100);
}
bool PanelPopup::IntegrationOwnerVisible() const {
  const auto owner = parent();
  return owner && !owner->IsMinimized() && IsWindowVisible(owner->GetWindowHandle()) && IsWindowVisible(integration_surface_);
}
bool PanelPopup::PointerOverIntegration() const {
  if (!IntegrationOwnerVisible()) return false;
  POINT cursor{};
  if (!GetCursorPos(&cursor)) return false;
  const HWND hit = GetAncestor(WindowFromPoint(cursor),GA_ROOT);
  if (!hit) return false;
  if (presented_ && window_ && hit == window_->GetWindowHandle()) return true;
  return hit == GetAncestor(integration_surface_,GA_ROOT) && PointerOverOpener();
}
bool PanelPopup::PointerOverOpener() const {
  const auto owner = parent(); if (!owner) return false;
  const float scale = GetDpiForWindow(owner->GetWindowHandle()) / 96.f;
  POINT cursor{};
  if (!GetCursorPos(&cursor) || !ScreenToClient(owner->GetWindowHandle(), &cursor)) return false;
  const float x = cursor.x / scale, y = cursor.y / scale;
  return x >= anchor_.x && x < anchor_.x + anchor_.width &&
      y >= anchor_.y && y < anchor_.y + anchor_.height;
}
void PanelPopup::Deactivated() {
  if (!open_ || menu_open_ || modal()) return;
  // The spaces probe drives the editor while another app may hold the foreground.
  if (const auto state = state_.lock(); state && state->spaces_probe) return;
  const bool over_opener = PointerOverOpener(), owner_foreground = PopupOwnerIsForeground(parent());
  if (const auto state = state_.lock()) {
    auto details = CefDictionaryValue::Create(); details->SetInt("request", request_);
    details->SetBool("over_opener", over_opener); details->SetBool("owner_foreground", owner_foreground);
    details->SetBool("left_down", PrimaryButtonDown());
    state->Record("popup_deactivated", details);
  }
  // Windows activates the shell on mouse-down; the renderer's button click
  // arrives on mouse-up. Preserve the open state for that one gesture so Toggle
  // closes it instead of reopening a panel already dismissed by activation.
  // Views activation notifications can arrive after mouse-up, so the current
  // physical button state cannot identify the click that caused deactivation.
  if (over_opener && owner_foreground) {
    CefPostTask(TID_UI, base::BindOnce([](std::weak_ptr<PanelPopup> weak, int request) {
      if (auto self = weak.lock()) self->FinishOpenerPress(request);
    }, weak_from_this(), request_));
  } else Hide(false);
}
void PanelPopup::FinishOpenerPress(int request) {
  if (!open_ || request != request_ || !window_ || window_->IsActive() || menu_open_) return;
  if (!PopupOwnerIsForeground(parent()) || !PointerOverOpener()) { Hide(false); return; }
  if (PrimaryButtonDown()) {
    CefPostDelayedTask(TID_UI, base::BindOnce([](std::weak_ptr<PanelPopup> weak, int opening) {
      if (auto self = weak.lock()) self->FinishOpenerPress(opening);
    }, weak_from_this(), request), 16);
    return;
  }
  // Cancelled pointer gestures still dismiss the panel. Allow the asynchronous
  // mouse-up IPC to run first; normal toggles close immediately, without waiting
  // for this fallback. Request/lifetime checks exclude a later opening.
  CefPostDelayedTask(TID_UI, base::BindOnce([](std::weak_ptr<PanelPopup> weak, int opening) {
    if (auto self = weak.lock()) self->DismissInactive(opening);
  }, weak_from_this(), request), 150);
}
void PanelPopup::DismissInactive(int request) {
  if (open_ && request == request_ && window_ && !window_->IsActive()) Hide(false);
}
void PanelPopup::Hide(bool restore_focus, bool animate) {
  if (hover_) hover_->Hide(false);
  if ((!open_ && !presentation_.closing()) || menu_open_) return;
  const bool animated = animate && presented_ && !modal();
  DropDraftTheme();
  if (kind_ == "integration" && integration_ && LiveIntegration(integration_source_)) {
    integration_->SetList("items",CefListValue::Create()); integration_->SetBool("composeAvailable",false);
  }
  open_ = false;
  prepared_ = presented_ = false;
  pinned_page_ = nullptr; saving_pin_ = false;
  RestoreModalOwner();
  const bool closing = animated && presentation_.Dismiss(kind_ == "archive", origin_x_);
  if (!closing) presentation_.Conceal();
  if (observer_) observer_->Success(Response());
  if (auto s = state_.lock()) {
    s->Record("panel_hidden");
    auto details = CefDictionaryValue::Create(); details->SetInt("request", request_);
    details->SetBool("animated", closing); details->SetBool("button", animate);
    s->Record("popup_dismissed", details);
    if (restore_focus && s->window) { s->window->Activate(); if (s->content) FocusBrowser(s->content); else FocusShell(s); }
  }
}
void PanelPopup::DropDraftTheme() {
  if (!draft_theme_) return;
  draft_theme_ = false;
  if (auto s = state_.lock(); s && s->workspace) s->workspace->RevertThemePreview();
}
void PanelPopup::Shutdown() {
  if (hover_) hover_->Shutdown();
  menu_open_ = false; Hide(false); shutdown_ = true; observer_ = nullptr; if (window_) window_->Close();
}
std::string PanelPopup::Response(CefRefPtr<CefDictionaryValue> theme) const {
  auto s = state_.lock(); auto d = CefDictionaryValue::Create();
  d->SetInt("request", request_); d->SetString("kind", kind_); d->SetBool("open", open_);
  d->SetDictionary("snapshot", theme ? theme->Copy(false) : s && s->workspace ? s->workspace->Snapshot() : CefDictionaryValue::Create());
  // One archive for every Space, sent only while its panel is open.
  if (open_ && kind_ == "archive" && s && s->workspace) d->SetList("archive", s->workspace->ArchiveSnapshot());
  if (kind_ == "integration" && integration_) d->SetDictionary("integration", integration_->Copy(false));
  if (editing_pin() && pinned_page_) d->SetDictionary("pinnedPage", pinned_page_->Copy(false));
  if (open_ && kind_ == "moveToProfile" && profile_move_) d->SetDictionary("profileMove", profile_move_->Copy(false));
  if (s && kind_ == "site") {
    d->SetDictionary("site", SiteInfo(s));
    // Installed extensions for the popover's Extensions row (names and PNG
    // icons from the packages; no paths).
    auto list = CefListValue::Create();
    if (s->extensions)
      for (const auto& item : s->extensions->List(s->WebContext())) {
        auto row = CefDictionaryValue::Create();
        row->SetString("id", item.id); row->SetString("name", item.name); row->SetString("icon", item.icon);
        row->SetBool("popup", !item.popup.empty()); row->SetBool("options", !item.options.empty());
        row->SetBool("action", item.action);
        list->SetDictionary(list->GetSize(), row);
      }
    d->SetList("extensions", list);
    d->SetInt("siteDetails", site_details_);
  }
  auto value = CefValue::Create(); value->SetDictionary(d); return CefWriteJSON(value, JSON_WRITER_DEFAULT);
}
void PanelPopup::Publish(CefRefPtr<CefDictionaryValue> theme) {
  if (hover_) hover_->Publish(theme);
  if (open_ && kind_ == "integration" && integration_ &&
      !BrowserIntegrationEnabled(state_.lock(), integration_->GetString("service").ToString())) { Hide(false); return; }
  if (open_ && integration_cached_) SyncIntegrationPreview();
  if (observer_ && open_) observer_->Success(Response(theme));
}
void PanelPopup::Cancel(int64_t query) { if (query == observer_id_) { observer_ = nullptr; observer_id_ = -1; } }
void PanelPopup::DownloadMenu(int id, int x, int y, CefRefPtr<CefMessageRouterBrowserSide::Callback> callback) {
  auto s = state_.lock();
  bool complete = false, known = false;
  const auto list = s->downloads->List();
  for (size_t i = 0; i < list->GetSize(); ++i) {
    const auto row = list->GetDictionary(i);
    if (row->GetInt("id") == id) { known = true; complete = row->GetString("state") == "complete"; }
  }
  const auto bounds = window_->GetBounds();
  if (!known || x < 0 || y < 0 || x > bounds.width || y > bounds.height) { callback->Failure(400, "Invalid download menu"); return; }
  auto model = CefMenuModel::CreateMenuModel(new DownloadMenuDelegate(s, weak_from_this(), id));
  model->AddItem(28000, "Open"); model->AddItem(28001, "Copy"); model->AddItem(28002, "Show in File Explorer");
  model->AddSeparator();
  model->AddItem(28003, "Remove from list"); model->AddItem(28004, "Move to Recycle Bin");
  for (const int command : {28000, 28001, 28002, 28004}) model->SetEnabled(command, complete);
  callback->Success(Response());
  // The menu may take activation from the popover; keep it open meanwhile.
  menu_open_ = true;
  window_->ShowMenu(model, CefPoint(bounds.x + x, bounds.y + y), CEF_MENU_ANCHOR_TOPLEFT);
}
void PanelPopup::ProbeClick(const std::string& text, int attempts) {
  auto s = state_.lock(); if (!s || !s->probe || (!s->organization_probe && !s->downloads_probe && !s->site_probe && !s->extension_popup_probe && !s->capture_mute_probe && !s->spaces_probe && s->history_probe.empty())) return;
  if (!open_ || !ready_ || !observer_ || !browser_) {
    if (attempts > 0) CefPostDelayedTask(TID_UI, base::BindOnce([](std::weak_ptr<PanelPopup> weak, std::string value, int tries) {
      if (auto o = weak.lock()) o->ProbeClick(value, tries);
    }, weak_from_this(), text, attempts - 1), 30);
    return;
  }
  auto v = CefValue::Create(); v->SetString(text);
  // Fixed script; only the JSON-encoded probe text varies.
  const std::string script = "(async()=>{const t=" + CefWriteJSON(v, JSON_WRITER_DEFAULT).ToString() + ";"
      "for(let i=0;i<60;i++){const b=[...document.querySelectorAll('button')].find(x=>(x.getAttribute('aria-label')||'').includes(t)||x.textContent.includes(t));"
      "if(b){b.click();return;}await new Promise(r=>setTimeout(r,30));}console.error('RED_PANEL_CLICK_MISSING');})()";
  browser_->GetMainFrame()->ExecuteJavaScript(script, std::string(kPanelUrl), 0);
}
void PanelPopup::MenuClosed() {
  menu_open_ = false;
  if (window_ && !window_->IsActive()) Hide(false);
}
bool PanelPopup::Query(CefRefPtr<CefBrowser> b, CefRefPtr<CefFrame> f, int64_t query, const CefString& raw,
                       bool persistent, CefRefPtr<CefMessageRouterBrowserSide::Callback> cb) {
  auto s = state_.lock();
  if (!s || !IsPanelUi(b->GetIdentifier(), hover_only_ ? s->integration_panel_id.load() : s->panel_id.load(),
                       f->IsMain(), f->GetURL().ToString())) { cb->Failure(403, "Untrusted panel document"); return true; }
  if (raw.length() > (modal() ? 8192u : 4096u)) { cb->Failure(413, "Request too large"); return true; }
  auto value = CefParseJSON(raw, JSON_PARSER_RFC);
  if (!value || value->GetType() != VTYPE_DICTIONARY || value->GetDictionary()->GetType("method") != VTYPE_STRING) { cb->Failure(400, "Invalid command"); return true; }
  auto d = value->GetDictionary(); const auto method = d->GetString("method").ToString();
  if (HandleUiFocus(s, b, method, d->GetSize(), persistent, cb)) return true;
  if (persistent != (method == "panel.subscribe")) { cb->Failure(400, "Only the subscription persists"); return true; }
  if (method == "panel.subscribe" && d->GetSize() == 1) {
    if (observer_) { cb->Failure(409, "Already subscribed"); return true; }
    observer_ = cb; observer_id_ = query; Publish(); return true;
  }
  if (method == "panel.close" && d->GetSize() == 1) {
    if (saving_pin_) { cb->Failure(409, "Saving pinned page"); return true; }
    if (moving_tab_) { cb->Failure(409, "Moving tab"); return true; }
    cb->Success(Response()); Hide(!hover_only_); return true;
  }
  if ((method == "panel.resize" || method == "panel.present") &&
      (!open_ || !window_ || d->GetType("request") != VTYPE_INT || d->GetInt("request") != request_)) {
    cb->Failure(409, "Panel opening changed"); return true;
  }
  if (method == "panel.resize" && d->GetSize() == 3 && d->GetType("height") == VTYPE_INT &&
      d->GetInt("height") >= 60 && d->GetInt("height") <= 640) {
    if (!presented_ && !(hover_only_ ? PointerOverIntegration() : PopupOwnerIsForeground(s->window))) {
      Hide(false); cb->Failure(409, "Panel opener is unavailable"); return true;
    }
    height_ = d->GetInt("height"); Reposition(); window_->Layout();
    // Cloaking a staged active window can move foreground away from its owner.
    // The modal appears at once at its final size, so it needs no staging.
    prepared_ = true; if (!presented_ && !modal()) presentation_.Stage();
    cb->Success(PopupViewport(window_, request_)); return true;
  }
  if (method == "panel.present" && d->GetSize() == 3 && d->GetType("reducedMotion") == VTYPE_BOOL && prepared_) {
    if (!(hover_only_ ? PointerOverIntegration() : PopupOwnerIsForeground(s->window))) {
      Hide(false); cb->Failure(409, "Panel opener is unavailable"); return true;
    }
    if (!presented_) {
      presented_ = true;
      const bool reduced_motion = d->GetBool("reducedMotion");
      const bool modal = this->modal();
      if (!hover_only_ && !modal) { window_->Show(); window_->Activate(); FocusBrowser(browser_); }
      presentation_.Reveal(reduced_motion || modal, kind_ == "archive", origin_x_, !modal,
                           hover_only_ ? integration_origin_y_ : -1.f, !modal);
      if (modal) {
        window_->Activate(); FocusBrowser(browser_);
        if (s->sidebar_peek) s->sidebar_peek->Hide();
        // The same owned panel also hosts modeless popovers. Block input to
        // the root only for this opening, after the editor takes focus.
        const HWND owner = s->window->GetWindowHandle();
        if (IsWindowEnabled(owner)) {
          modal_owner_ = owner;
          EnableWindow(owner, FALSE);
          if (s->embedded) s->embedded->SetModalShade(true);
        }
      }
      auto details = CefDictionaryValue::Create(); details->SetInt("request", request_);
      if (modal) details->SetString("kind", kind_);
      details->SetDouble("elapsed_ms", static_cast<double>(GetTickCount64() - opened_ms_));
      details->SetInt("height", window_->GetBounds().height); details->SetBool("layered", presentation_.layered());
      details->SetBool("composed", presentation_.composed());
      details->SetDouble("origin_x", origin_x_);
      details->SetInt("x", window_->GetBounds().x - s->window->GetClientAreaBoundsInScreen().x);
      details->SetDouble("button_center_x", anchor_.x + anchor_.width / 2.0);
      details->SetInt("width", window_->GetBounds().width);
      details->SetBool("bottom", kind_ == "archive");
      if (hover_only_) {
        const auto bounds = window_->GetBounds();
        details->SetString("kind", "integration");
        const auto peek = s->browsers.find(s->peek_id);
        details->SetString("surface", peek != s->browsers.end() &&
            peek->second->GetHost()->GetWindowHandle() == integration_surface_ ? "floating" : "docked");
        details->SetBool("motion_enabled", !reduced_motion && NativeMotionEnabled());
        details->SetBool("favorite", integration_favorite_);
        details->SetDouble("origin_y", integration_origin_y_);
        details->SetInt("anchor_right", anchor_.x + anchor_.width);
        details->SetInt("anchor_top", anchor_.y);
        details->SetInt("anchor_bottom", anchor_.y + anchor_.height);
        details->SetInt("y", bounds.y - s->window->GetClientAreaBoundsInScreen().y);
      }
      details->SetBool("foreground", GetForegroundWindow() == window_->GetWindowHandle());
      details->SetBool("keyboard_focus", GetAncestor(GetFocus(), GA_ROOT) == window_->GetWindowHandle());
      details->SetBool("command", false); s->Record("popup_presented", details);
    }
    cb->Success(PopupViewport(window_, request_, presentation_.composed(), origin_x_, kind_ == "archive", !modal(),
                             hover_only_ ? integration_origin_y_ : -1.f)); return true;
  }
  if (!open_ || !s->workspace || s->workspace->WindowClosing()) { cb->Failure(409, "Panel is not open"); return true; }
  if (kind_ == "moveToProfile") {
    if (method != "move.confirm") { cb->Failure(404, "Unknown move confirmation command"); return true; }
    if (d->GetSize() != 2 || d->GetType("request") != VTYPE_INT || d->GetInt("request") != request_ ||
        !presented_ || moving_tab_ || !profile_move_) { cb->Failure(409, "Move confirmation changed"); return true; }
    // The same command the sidebar sends; the controller validates it and
    // unloads the live pages before moving (WorkspaceController::UnloadThenDispatch).
    auto command = CefDictionaryValue::Create();
    command->SetString("method", "tree.moveToSpace");
    command->SetString("id", profile_move_->GetString("id"));
    command->SetString("space", profile_move_->GetString("space"));
    command->SetBool("unload", true);
    command->SetDouble("expectedRevision", s->workspace->Snapshot()->GetDouble("treeRevision"));
    moving_tab_ = true;
    s->workspace->Handle(command, [weak = weak_from_this(), cb, opening = request_](int code, const std::string& error,
                                                                                    CefRefPtr<CefDictionaryValue>) {
      if (code) cb->Failure(code, error);
      else cb->Success("{}");
      const auto self = weak.lock();
      if (!self || self->request_ != opening || !self->open_ || self->kind_ != "moveToProfile") return;
      self->moving_tab_ = false;
      if (!code) self->Hide(true);
    });
    return true;
  }
  if (editing_pin()) {
    if (method != "pin.save") { cb->Failure(404, "Unknown pinned page editor command"); return true; }
    if (d->GetSize() != 4 || d->GetType("request") != VTYPE_INT || d->GetInt("request") != request_ ||
        !presented_ || saving_pin_ || !pinned_page_) { cb->Failure(409, "Pinned page editor opening changed"); return true; }
    if (d->GetType("title") != VTYPE_STRING || d->GetType("url") != VTYPE_STRING || d->GetString("url").empty()) {
      cb->Failure(400, "Invalid title or address"); return true;
    }
    saving_pin_ = true;
    std::string error;
    const int code = s->workspace->SetPinnedPage(pinned_page_->GetString("id"), d->GetString("url"), d->GetString("title"), error,
        [weak = weak_from_this(), cb, opening = request_](int code, const std::string& error, CefRefPtr<CefDictionaryValue>) {
          const auto self = weak.lock();
          if (code) cb->Failure(code, error);
          else cb->Success("{}");
          if (!self || self->request_ != opening || !self->editing_pin()) return;
          self->saving_pin_ = false;
          if (!code) self->Hide(true);
        });
    if (code) { saving_pin_ = false; cb->Failure(code, error); }
    return true;
  }
  if (kind_ == "integration" && method.starts_with("integration.") && integration_ &&
      !BrowserIntegrationEnabled(s, integration_->GetString("service").ToString())) { cb->Failure(409, "Integration is disabled"); return true; }
  if (kind_ == "integration" && (method == "integration.open" || method == "integration.join")) {
    if (d->GetSize() != 4 || d->GetType("request") != VTYPE_INT || d->GetInt("request") != request_ ||
        d->GetType("revision") != VTYPE_INT || !integration_ || d->GetInt("revision") != integration_->GetInt("revision") ||
        d->GetType("index") != VTYPE_INT || !integration_ || integration_->GetBool("loading")) { cb->Failure(409, "Integration opening changed"); return true; }
    const auto items = integration_->GetList("items"); const int index = d->GetInt("index");
    if (integration_cached_) {
      if (!items || index < 0 || static_cast<size_t>(index) >= items->GetSize() ||
          !PreviewOpeningValid(s) || !s->workspace->integration_previews() || !s->workspace->integration_previews()->Cached(integration_source_)) {
        cb->Failure(409,"Preview changed"); return true;
      }
      const auto item = items->GetDictionary(index);
      if (method == "integration.open") {
        OpenCachedPreview(item->GetString("token"),false,cb); return true;
      }
      const auto live = LiveIntegration(integration_source_);
      const auto url = item->GetString("join").ToString();
      if (!live || live->id != "googleCalendar" || !IntegrationJoinUrl(url)) {
        cb->Failure(403,"Meeting link unavailable"); return true;
      }
      OpenPreviewInPin(url,{},false,cb);
      return true;
    }
    const auto node = s->workspace->DetachedSeed(integration_->GetString("node"));
    const auto provider = node ? RecentIntegration(node->url) : std::nullopt;
    if (method != "integration.open" || index < 0 || !items || static_cast<size_t>(index) >= items->GetSize() ||
        !node || !node->pinned || node->archived || !provider || node->url != integration_source_ ||
        s->workspace->ProfileForNode(node->id) != s->workspace->ProfileForNode("")) { cb->Failure(409, "Integration is unavailable"); return true; }
    const auto url = items->GetDictionary(index)->GetString("url").ToString();
    if (!RecentIntegrationPage(*provider,url)) { cb->Failure(403,"Page is outside this integration"); return true; }
    OpenPreviewInPin(url,{},false,cb);
    return true;
  }
  if (kind_ == "integration" && method == "integration.control") {
    if (d->GetSize()!=3 || d->GetType("request")!=VTYPE_INT || d->GetInt("request")!=request_ || d->GetType("action")!=VTYPE_STRING || !integration_) { cb->Failure(409,"Integration opening changed"); return true; }
    const auto action=d->GetString("action");
    if (integration_cached_) {
      if (!PreviewOpeningValid(s)) { cb->Failure(409,"Preview changed"); return true; }
      const auto cache = s->workspace->integration_previews();
      if (action == "refresh" && cache && cache->Queued(integration_source_)) {
        cache->Refresh(integration_source_); cb->Success("{}"); return true;
      }
      if (action == "open" || (action == "compose" && integration_->GetBool("composeAvailable") &&
          cache && cache->Cached(integration_source_))) {
        OpenCachedPreview({},action=="compose",cb); return true;
      }
      cb->Failure(400,"Invalid preview control"); return true;
    }
    if (action == "createFigma" && integration_->GetString("service") == "figma") {
      const auto node = s->workspace->DetachedSeed(integration_->GetString("node"));
      const auto provider = node ? RecentIntegration(node->url) : std::nullopt;
      const auto profile = integration_profile_.lock();
      if (!node || !node->pinned || node->archived || !provider || provider->id != "figma" ||
          node->url != integration_source_ || !profile || s->workspace->ProfileForNode(node->id) != profile ||
          s->workspace->ProfileForNode("") != profile) { cb->Failure(409,"Figma tab changed"); return true; }
      OpenPreviewInPin(std::string(CommandDocumentUrl("newFigmaDesign")),{},false,cb);
      return true;
    }
    cb->Failure(400,"Invalid integration control");return true;
  }
  if (hover_only_) { cb->Failure(404,"Unknown integration command"); return true; }
  if (kind_ == "space" && method == "space.edit") {
    // The space editor names the space; the revision check happens natively.
    auto command = d->Copy(false);
    command->SetDouble("expectedRevision", s->workspace->Snapshot()->GetDouble("treeRevision"));
    const bool deleting = method == "space.delete";
    s->workspace->Handle(command, [weak = weak_from_this(), cb, deleting](int code, const std::string& error, CefRefPtr<CefDictionaryValue>) {
      auto self = weak.lock(); if (!self) return;
      if (code) { cb->Failure(code, error); return; }
      cb->Success(self->Response());
      if (deleting) self->Hide(true);
    });
    return true;
  }
  if (kind_ == "newSpace" && method == "space.create") {
    // The new-space editor: the space is made only now, with its name, icon
    // and the theme previewed on the window, saved as that space's own theme.
    Theme theme; bool save = false;
    if (d->GetSize() != (d->HasKey("profileId") ? 5U : 4U) || d->GetType("theme") != VTYPE_DICTIONARY ||
        !WorkspaceController::ParseThemeCommand(d->GetDictionary("theme"), theme, save) || !save) { cb->Failure(400, "Invalid space command"); return true; }
    auto command = d->Copy(false); command->Remove("theme");
    command->SetDouble("expectedRevision", s->workspace->Snapshot()->GetDouble("treeRevision"));
    const auto before = s->workspace->Snapshot()->GetString("activeSpace").ToString();
    s->workspace->Handle(command, [weak = weak_from_this(), cb, theme, before](int code, const std::string& error, CefRefPtr<CefDictionaryValue>) {
      auto self = weak.lock(); auto s = self ? self->state_.lock() : nullptr; if (!self || !s || !s->workspace) return;
      if (code) { cb->Failure(code, error); return; }
      const auto created = s->workspace->Snapshot()->GetString("activeSpace").ToString();
      if (created.empty() || created == before) { cb->Failure(500, "The new space did not open"); return; }
      self->draft_theme_ = false;
      s->workspace->SetTheme(theme, true, [weak, cb](int code, const std::string& error, CefRefPtr<CefDictionaryValue>) {
        auto self = weak.lock(); if (!self) return;
        if (code) { cb->Failure(code, error); return; }
        cb->Success(self->Response()); self->Hide(true);
      }, created);
    });
    return true;
  }
  if ((kind_ == "theme" || kind_ == "space" || kind_ == "newSpace") && method == "workspace.setTheme") {
    Theme theme; bool save = false;
    if (!WorkspaceController::ParseThemeCommand(d, theme, save)) { cb->Failure(400, "Invalid theme command"); return true; }
    // The new-space editor only previews; its theme is saved with the space.
    if (kind_ == "newSpace") { if (save) { cb->Failure(400, "Create the space to keep its theme"); return true; } draft_theme_ = true; }
    s->workspace->SetTheme(theme, save, [weak = weak_from_this(), cb, save](int code, const std::string& error, CefRefPtr<CefDictionaryValue>) {
      auto self = weak.lock(); if (!self) return;
      if (code) { cb->Failure(code, error); return; }
      cb->Success(save ? self->Response() : "{}");
    },d->GetString("spaceId").ToString());
    return true;
  }
  if (kind_ == "archive" && method == "tree.restore" && d->GetSize() == 2 && d->GetType("id") == VTYPE_STRING) {
    // Restore only an archived tab; the revision check happens in the controller.
    if (!s->workspace->IsArchived(d->GetString("id").ToString())) { cb->Failure(404, "Archived tab is no longer available"); return true; }
    auto command = CefDictionaryValue::Create(); command->SetString("method", "tree.restore"); command->SetString("id", d->GetString("id"));
    command->SetDouble("expectedRevision", s->workspace->tree_revision());
    s->workspace->Handle(command, [weak = weak_from_this(), cb](int code, const std::string& error, CefRefPtr<CefDictionaryValue>) {
      auto self = weak.lock(); if (!self) return;
      if (code) { cb->Failure(code, error); return; }
      cb->Success(self->Response()); self->Hide(true);
    });
    return true;
  }
  if (kind_ == "site") {
    // Changes apply to the visible page's origin only, echoed back by the UI.
    std::string error; int code = -1;
    if (method == "site.set" && d->GetSize() == 4 && d->GetType("origin") == VTYPE_STRING &&
        d->GetType("key") == VTYPE_STRING && d->GetType("value") == VTYPE_STRING)
      code = SetSitePermission(s, d->GetString("origin"), d->GetString("key"), d->GetString("value"), error);
    else if (method == "site.reset" && d->GetSize() == 2 && d->GetType("origin") == VTYPE_STRING)
      code = ResetSitePermissions(s, d->GetString("origin"), error);
    else if (method == "site.autoPip" && d->GetSize() == 3 && d->GetType("origin") == VTYPE_STRING && d->GetType("value") == VTYPE_BOOL) {
      // Saved per site in Red's preferences (applies to every window).
      const auto origin = d->GetString("origin").ToString();
      if (origin.empty() || origin != SiteOrigin(s->content) || !s->session || !s->session->preferences) { cb->Failure(409, "The page changed; reopen site controls"); return true; }
      auto next = s->session->preferences->Get();
      if (next.auto_pip_sites.size() >= 500 && !next.auto_pip_sites.contains(origin)) { cb->Failure(409, "Too many sites with their own setting"); return true; }
      next.auto_pip_sites[origin] = d->GetBool("value");
      s->session->preferences->Save(next, [weak = weak_from_this(), cb](const std::string& failure) {
        const auto self = weak.lock(); if (!self) return;
        if (!failure.empty()) { cb->Failure(500, failure); return; }
        // The switch shows the saved value, published like site.set's.
        cb->Success(self->Response()); self->Publish();
      });
      return true;
    }
    else if (method == "site.boostEditor" && d->GetSize() == 1 && s->content) {
      // The editor is its own window bound to this page's host (windows/boost_window.cpp).
      const auto page = s->content;
      cb->Success(Response()); Hide(false);
      ShowBoostEditor(s, page); return true;
    }
    else if (method == "site.reload" && d->GetSize() == 1 && s->content) {
      s->content->Reload(); cb->Success(Response()); Hide(true); return true;
    }
    else if (method == "site.share" && d->GetSize() == 1) {
      // The visible page's address from the native snapshot, never from the UI.
      const auto snapshot = s->workspace->Snapshot();
      const auto url = snapshot->GetString("url").ToWString();
      if (url.empty()) { cb->Failure(409, "No page address to copy"); return true; }
      // The Windows share sheet, anchored to the main window (this popup hides
      // first so the sheet takes focus). Falls back to copying the address.
      const auto entry = s->content ? s->content->GetHost()->GetVisibleNavigationEntry() : nullptr;
      const auto title = entry ? entry->GetTitle().ToWString() : std::wstring{};
      const HWND owner = s->window ? s->window->GetWindowHandle() : nullptr;
      cb->Success(Response()); Hide(true);
      if (ShowShareSheet(owner, url, title)) { s->Record("page_share_opened"); return true; }
      const int failure = CopyLinkText(owner, url);
      if (s->embedded) s->embedded->Notify("copy", failure ? (failure == 409 ? L"The clipboard is busy. Try again." : L"Could not copy the page address") : L"Copied current URL");
      if (!failure) s->Record("page_link_copied");
      return true;
    }
    else if (method == "extension.open" && d->GetSize() == 2 && d->GetType("id") == VTYPE_STRING) {
      const auto* item = s->extensions ? s->extensions->Find(d->GetString("id")) : nullptr;
      if (!item) { cb->Failure(404, "Extension is no longer installed"); return true; }
      if (s->probe) {
        auto data = CefDictionaryValue::Create(); data->SetString("name", item->name);
        data->SetBool("icon", !item->icon.empty()); data->SetBool("popup", !item->popup.empty());
        s->Record("extension_open_clicked", data);
      }
      cb->Success(Response());
#if CEF_API_ADDED(CEF_EXPERIMENTAL)
      // As a click on its toolbar button (Red engine patch 02): Chrome grants
      // the visible page's per-tab permissions (activeTab, tab capture) and
      // either asks us to show the popup it names (set at runtime too) or
      // dispatches action.onClicked itself (popup-less extensions, e.g. PiP).
      if (item->action && s->content) {
        const std::string id = item->id;
        CefString popup_url;
        const int ran = s->content->GetHost()->RunExtensionAction(id, popup_url);
        const std::string origin = "chrome-extension://" + id + "/", url = popup_url.ToString();
        if (s->probe) {
          auto data = CefDictionaryValue::Create(); data->SetInt("result", ran); data->SetBool("popup_url", !url.empty());
          s->Record("extension_action_run", data);
        }
        if (ran == 2) { Hide(true); return true; }
        if (ran == 1 && url.starts_with(origin) && s->extension_popup && window_) {
          const auto bounds = window_->GetBounds();
          Hide(false);
          s->extension_popup->Open(id, url.substr(origin.size()), CefPoint(bounds.x + bounds.width, bounds.y));
          return true;
        }
      }
#endif
      if (!item->popup.empty() && s->extension_popup && window_) {
        // The popup takes the popover's place, right-aligned under the toolbar.
        const auto bounds = window_->GetBounds();
        const std::string id = item->id, popup = item->popup;
        Hide(false);
        s->extension_popup->Open(id, popup, CefPoint(bounds.x + bounds.width, bounds.y));
      } else if (!item->options.empty()) {
        const std::string url = "chrome-extension://" + item->id + "/" + item->options;
        Hide(false); s->workspace->AdoptExtensionPage(url);
      }
      return true;
    }
    else if (method == "site.menu" && d->GetSize() == 3 && d->GetType("x") == VTYPE_INT && d->GetType("y") == VTYPE_INT && window_) {
      SiteMenu(d->GetInt("x"), d->GetInt("y"), cb); return true;
    }
    else if (method == "extension.menu" && d->GetSize() == 4 && d->GetType("id") == VTYPE_STRING && d->GetType("x") == VTYPE_INT &&
             d->GetType("y") == VTYPE_INT && window_ && s->extensions && s->extensions->Find(d->GetString("id"))) {
      // Right click on an extension: Open / Pin to Top Bar / Manage, at the pointer (panel DIP).
      const auto bounds = window_->GetBounds(); const std::string id = d->GetString("id");
      const int x = std::clamp(d->GetInt("x"), 0, bounds.width), y = std::clamp(d->GetInt("y"), 0, bounds.height);
      // The popover stays while the menu is open (as with its native menus) and gets focus back.
      cb->Success(Response()); menu_open_ = true;
      ShowExtensionMenu(s, id, CefPoint(bounds.x + x, bounds.y + y), CefPoint(bounds.x + bounds.width, bounds.y), window_,
          [weak = weak_from_this()] { if (auto self = weak.lock()) self->MenuDone(); },
          [weak = weak_from_this()] { if (auto self = weak.lock()) self->MenuClosed(); });
      return true;
    }
    else if (method == "extension.store" && d->GetSize() == 1) {
      cb->Success(Response()); Hide(false);
      s->workspace->OpenLink("https://chromewebstore.live.com/", true); return true;
    }
    if (code > 0) { cb->Failure(code, error); return true; }
    if (code == 0) { cb->Success(Response()); Publish(); return true; }
  }
  if (kind_ == "history" && HandleHistoryCommand(s, method, d, cb, [weak = weak_from_this()] {
        if (auto self = weak.lock()) self->Hide(false);
      })) return true;
  if (kind_ == "downloads" && s->downloads) {
    if (method == "downloads.openPage" && d->GetSize() == 1) { s->workspace->OpenDownloadsPage(); cb->Success(Response()); Hide(false); return true; }
    if (method == "downloads.clear" && d->GetSize() == 1) { s->downloads->ClearFinished(false); cb->Success(Response()); return true; }
    const auto action = d->GetType("action") == VTYPE_STRING ? d->GetString("action").ToString() : "";
    if (method == "downloads.control" && d->GetSize() == 3 && d->GetType("id") == VTYPE_INT && d->GetInt("id") >= 0 &&
        (action == "cancel" || action == "pause" || action == "resume" || action == "open" || action == "show" || action == "remove")) {
      std::string error;
      if (const int code = s->downloads->Control(static_cast<uint32_t>(d->GetInt("id")), action, error)) { cb->Failure(code, error); return true; }
      cb->Success(Response()); return true;
    }
    if (method == "downloads.menu" && d->GetSize() == 4 && d->GetType("id") == VTYPE_INT && d->GetType("x") == VTYPE_INT && d->GetType("y") == VTYPE_INT) {
      DownloadMenu(d->GetInt("id"), d->GetInt("x"), d->GetInt("y"), cb); return true;
    }
  }
  cb->Failure(400, "Unsupported panel command"); return true;
}
}  // namespace red
