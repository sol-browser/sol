#include "windows/passwords_window.h"
#include "windows/sidebar_actions.h"
#include <windows.h>
#include <commctrl.h>
#include <dwmapi.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "include/cef_app.h"
#include "include/base/cef_callback.h"
#include "include/cef_browser.h"
#include "include/cef_command_line.h"
#include "include/cef_parser.h"
#include "include/cef_preference.h"
#include "include/cef_process_message.h"
#include "include/cef_sandbox_win.h"
#include "include/cef_scheme.h"
#include "include/cef_task.h"
#include "include/cef_version.h"
#include "include/views/cef_box_layout.h"
#include "include/views/cef_browser_view.h"
#include "include/views/cef_window.h"
#include "include/views/cef_fill_layout.h"
#include "include/wrapper/cef_closure_task.h"
#include "include/wrapper/cef_helpers.h"
#include "include/wrapper/cef_message_router.h"
#include "include/wrapper/cef_stream_resource_handler.h"
#include "trust_policy.h"
#include "app/state.h"
#include "app/session.h"
#include "app/browsing_profiles.h"
#include "app/profile_location.h"
#include "app/updates.h"
#include "red_version.h"
#include "app/release_notes.h"
#include "app/chrome_session_restore.h"
#include "app/widevine.h"
#include "app/memory_report.h"
#include "history/title_target.h"
#include "integrations/integration_previews.h"
#include "windows/website_popup.h"
#include "windows/website_popup_host.h"
#include "app/windows_locale.h"
#include "windows/welcome_music.h"
#include "windows/welcome_window.h"
#include "experiments/tab_probe.h"
#include "experiments/window_transfer.h"
#include "windows/embedded_content.h"
#include "windows/focus.h"
#include "windows/workspace_layout.h"
#include "windows/branding.h"
#include "windows/window_close.h"
#include "windows/command_bar.h"
#include "windows/panel_popup.h"
#include "windows/extension_popup.h"
#include "windows/sidebar_peek.h"
#include "extensions/extension_catalog.h"
#include "windows/sidebar_menu.h"
#include "windows/devtools.h"
#include "windows/devtools_dock.h"
#include "windows/device_emulation.h"
#include "windows/clipboard.h"
#include "navigation/file_url.h"
#include "navigation/shortcuts.h"
#include "include/cef_keyboard_handler.h"
#include "include/cef_drag_handler.h"
#include "workspace/controller.h"
#include "include/cef_id_mappers.h"
#include "include/cef_jsdialog_handler.h"
#include <optional>
#include "experiments/shell_capture.h"
#include "experiments/lifecycle_probe.h"
#include "experiments/extension_observation.h"
#include "experiments/organization_probe.h"
#include "experiments/input_probe.h"
#include "experiments/browsing_probe.h"
#include "experiments/fullscreen_probe.h"
#include "experiments/permissions_probe.h"
#include "experiments/site_probe.h"
#include "experiments/history_probe.h"
#include "experiments/tab_actions_probe.h"
#include "experiments/audible_probe.h"
#include "experiments/protocols_probe.h"
#include "experiments/close_motion_probe.h"
#include "experiments/sidebar_peek_probe.h"
#include "experiments/google_integrations_probe.h"
#include "experiments/integration_live_check.h"
#include "experiments/sidebar_perf_probe.h"
#include "experiments/pip_probe.h"
#include "windows/auto_pip.h"
#include "experiments/capture_mute_probe.h"
#include "experiments/theme_probe.h"
#include "experiments/spaces_probe.h"
#include "experiments/boost_probe.h"
#include "experiments/menu_probe.h"
#include "experiments/private_probe.h"
#include "experiments/pin_probe.h"
#include "windows/page_menu.h"
#include "windows/ui_context_menu.h"
#include "extensions/pinned_extensions.h"
#include "extensions/extension_catalog.h"
#include "windows/boosts.h"
#include "experiments/ua_brand_probe.h"
#include "experiments/link_headers_probe.h"
#include "import/browser_import.h"
#include "experiments/page_load_trace.h"
#include "experiments/downloads_page_probe.h"
#include "experiments/extension_popup_probe.h"
#include "site/site_settings.h"
#include "history/history_service.h"
#include "history/history_commands.h"
#include "downloads/downloads_page.h"
#include "history/favicon_cache.h"
#include "windows/main_menu.h"
#include "windows/tab_history_menu.h"
#include "windows/settings_window.h"
#include "windows/default_browser.h"
#include "windows/window_icons.h"
#include "windows/browser_identity.h"
#include "navigation/external_launch.h"
#include "windows/window_placement.h"
#include "windows/window_corners.h"
#include "include/views/cef_display.h"
#include "renderer/audible_detector.h"
#include "extensions/profile_guard.h"
#include "extensions/shell_reset.h"
#include "app/web_profile_repair.h"
#include "extensions/install_notice.h"
#include "downloads/download_manager.h"
#include "include/cef_download_handler.h"
#include "experiments/toolbar_probe.h"

namespace {
namespace fs = std::filesystem;

using State = red::State;
void RestoreContentStack(const std::shared_ptr<State>& state) {
  if (!state->workspace_mode && !state->embedded_tabs_probe) return;
  if (state->embedded) state->embedded->QueueRaise();
}

// This captures the renderer's actual Windows token, not merely a CEF setting.
CefRefPtr<CefDictionaryValue> TokenEvidence() {
  auto data = CefDictionaryValue::Create();
  data->SetInt("pid", static_cast<int>(GetCurrentProcessId()));
  HANDLE token = nullptr;
  if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
    data->SetBool("token_query_ok", false);
    return data;
  }
  DWORD app_container = 0;
  DWORD length = 0;
  const bool app_ok = GetTokenInformation(token, TokenIsAppContainer, &app_container,
                                          sizeof(app_container), &length) != FALSE;
  GetTokenInformation(token, TokenIntegrityLevel, nullptr, 0, &length);
  std::vector<unsigned char> buffer(length);
  bool integrity_ok = length > 0 && GetTokenInformation(token, TokenIntegrityLevel,
                                      buffer.data(), length, &length) != FALSE;
  DWORD integrity = 0xffffffff;
  if (integrity_ok) {
    auto label = reinterpret_cast<TOKEN_MANDATORY_LABEL*>(buffer.data());
    auto sid = label->Label.Sid;
    integrity = *GetSidSubAuthority(sid, *GetSidSubAuthorityCount(sid) - 1);
  }
  data->SetBool("token_query_ok", app_ok && integrity_ok);
  data->SetBool("app_container", app_container != 0);
  data->SetBool("restricted", IsTokenRestricted(token) != FALSE);
  data->SetInt("integrity_rid", static_cast<int>(integrity));
  CloseHandle(token);
  return data;
}

// A workspace window's system backdrop (Settings, or a probe's override). Translucency is decided
// here, before the window exists; Mica and Acrylic then switch live (docs/acrylic.md).
void ChooseBackdrop(const std::shared_ptr<State>& state) {
#if defined(RED_CEF_TRANSLUCENT)
  state->backdrop = !state->backdrop_switch.empty() ? state->backdrop_switch :
      state->session && state->session->preferences ? state->session->preferences->Get().backdrop : "none";
  state->translucent = state->workspace_mode && state->backdrop != "none" && red::SystemBackdropSupported();
#endif
}

class StyleDelegate final : public CefBrowserViewDelegate {
 public:
  // |background|: re-applied on theme changes (CEF resets view backgrounds to
  // the native theme's then); 0 keeps CEF's default.
  explicit StyleDelegate(cef_runtime_style_t style, cef_color_t background = 0) : style_(style), background_(background) {}
  cef_runtime_style_t GetBrowserRuntimeStyle() override { return style_; }
  void OnThemeChanged(CefRefPtr<CefView> view) override { if (background_) view->SetBackgroundColor(background_); }
  CefSize GetPreferredSize(CefRefPtr<CefView>) override {
    return style_ == CEF_RUNTIME_STYLE_ALLOY ? CefSize(1200, 96) : CefSize(1200, 700);
  }
 private:
  cef_runtime_style_t style_;
  cef_color_t background_;
  IMPLEMENT_REFCOUNTING(StyleDelegate);
};

// Resolve only an immutable in-memory asset map. Request paths never become
// filesystem paths. The browser identity check also covers subresource requests.
// Serves one cached favicon; keeps its bytes alive for the stream's lifetime.
class FaviconResource final : public CefStreamResourceHandler {
 public:
  FaviconResource(std::shared_ptr<std::string> bytes, CefRefPtr<CefStreamReader> stream, const CefResponse::HeaderMap& headers)
      : CefStreamResourceHandler(200, "OK", "image/png", headers, stream), bytes_(std::move(bytes)) {}
 private:
  std::shared_ptr<std::string> bytes_;
};
class ThemeResource final : public CefStreamResourceHandler {
 public:
  ThemeResource(std::shared_ptr<std::string> body, CefRefPtr<CefStreamReader> stream, const CefResponse::HeaderMap& headers,
                const std::string& mime = "text/css")
      : CefStreamResourceHandler(200, "OK", mime, headers, stream), body_(std::move(body)) {}
 private:
  std::shared_ptr<std::string> body_;  // Owns the bytes the stream reads.
};
class UiResources final : public CefSchemeHandlerFactory {
 public:
  UiResources(std::shared_ptr<State> state, const fs::path& directory) : state_(state) {
    std::error_code error;
    fs::recursive_directory_iterator iterator(directory, error), end;
    for (; !error && iterator != end; iterator.increment(error)) {
      const auto& item = *iterator;
      if (!item.is_regular_file(error) || item.is_symlink(error)) continue;
      auto extension = item.path().extension().string();
      std::string mime = extension == ".html" ? "text/html" :
                         extension == ".js" ? "text/javascript" :
                         extension == ".css" ? "text/css" : "";
      if (mime.empty()) continue;
      std::ifstream input(item.path(), std::ios::binary);
      assets_["/" + item.path().lexically_relative(directory).generic_string()] =
          {mime, std::string(std::istreambuf_iterator<char>(input), {})};
    }
  }
  CefRefPtr<CefResourceHandler> Create(CefRefPtr<CefBrowser> browser,
      CefRefPtr<CefFrame> frame, const CefString&, CefRefPtr<CefRequest> request) override {
    // A UI browser created before the session (the first-run welcome) is matched against this window.
    const auto owner = browser && state_->session ? state_->session->UiOwner(browser->GetIdentifier()) : nullptr;
    const auto state = owner ? owner : state_;
    if (!browser || (browser->GetIdentifier() != state->shell_id.load() && browser->GetIdentifier() != state->welcome_id.load() && browser->GetIdentifier() != state->command_id.load() &&
                     browser->GetIdentifier() != state->panel_id.load() && browser->GetIdentifier() != state->integration_panel_id.load() &&
                     browser->GetIdentifier() != state->peek_id.load() &&
                     browser->GetIdentifier() != state->settings_id.load() && browser->GetIdentifier() != state->boost_id.load() &&
                     browser->GetIdentifier() != state->menu_id[0].load() && browser->GetIdentifier() != state->menu_id[1].load() &&
                     browser->GetIdentifier() != state->menu_id[2].load() && !state->internal_pages.contains(browser->GetIdentifier()) &&
                     !state->IsDevToolsWindowUi(browser->GetIdentifier())) || !frame ||
        request->GetMethod() != "GET") return nullptr;
    CefURLParts parts;
    if (!CefParseURL(request->GetURL(), parts) || CefString(&parts.host) != "ui" ||
        parts.username.length || parts.password.length || parts.port.length ||
        parts.query.length || parts.fragment.length) return nullptr;
    auto path = CefString(&parts.path).ToString();
    if (path == "/theme.css") {
      // The current theme as CSS variables, linked before the UI's own styles
      // so the very first frame is themed (scripts apply it only later).
      const auto packed = state->theme_packed.load();
      const int hue = packed & 511, intensity = packed >> 9 & 255, noise = packed >> 17 & 127;
      const bool dark = packed >> 24 & 1;
      char css[200];
      std::snprintf(css, sizeof(css), ":root{--theme-hue:%d;--theme-chroma:%.2f;--theme-dark:%d;--theme-noise:%.2f;--theme-vivid:%.2f;%s}",
                    hue, red::ThemeChromaScale(intensity), dark ? 1 : 0, noise / 100.0, std::max(0, intensity - 100) / double(red::kThemeMaxIntensity - 100), dark ? "color-scheme:dark" : "");
      auto body = std::make_shared<std::string>(css);
      if (const auto palette=state->theme_palette_css.load()) *body += ":root{"+*palette+"}";
      // The document kind is stamped into HTML before parsing. Unlike :has(),
      // these backgrounds apply before React mounts or its first snapshot.
      *body += ":root[data-ui=panel],:root[data-ui=panel] body{background:#24252b;color-scheme:dark}"
               ":root[data-ui=command],:root[data-ui=command] body{background:#101012;color-scheme:dark}"
               ":root[data-ui=settings],:root[data-ui=settings] body{background:#1c1a20;color-scheme:dark}"
               ":root[data-ui=page],:root[data-ui=page] body{background:oklch(.2334 0 0);color-scheme:dark}"
               ":root[data-ui=menu],:root[data-ui=menu] body{background:#24252b;color-scheme:dark}";
      CefResponse::HeaderMap headers;
      headers.emplace("X-Content-Type-Options", "nosniff");
      headers.emplace("Cache-Control", "no-store");
      return new ThemeResource(body, CefStreamReader::CreateForData(body->data(), body->size()), headers);
    }
    if (path.starts_with("/favicon/")) {
      // Versions are immutable within this cache and reset on the next run.
      auto png = !state->private_window && state->session && state->session->profiles ? state->session->profiles->FaviconPng(path) :
          state->favicons ? state->favicons->PngForPath(path) : std::nullopt;
      if (!png) return nullptr;
      auto bytes = std::make_shared<std::string>(std::move(*png));
      auto stream = CefStreamReader::CreateForData(bytes->data(), bytes->size());
      CefResponse::HeaderMap headers;
      headers.emplace("X-Content-Type-Options", "nosniff");
      headers.emplace("Cache-Control", "no-store");
      return new FaviconResource(bytes, stream, headers);
    }
    if (path == "/welcome-music.mp3") {
      // The welcome's track, for the welcome to read its rhythm (Windows plays it).
      const auto track = red::WelcomeMusic::Track();
      auto stream = track.empty() ? nullptr : CefStreamReader::CreateForFile(track.wstring());
      if (!stream) return nullptr;
      CefResponse::HeaderMap headers;
      headers.emplace("X-Content-Type-Options", "nosniff");
      headers.emplace("Cache-Control", "no-store");
      return new CefStreamResourceHandler(200, "OK", "audio/mpeg", headers, stream);
    }
    auto found = assets_.find(path == "/command.html" || path == "/welcome.html" || path == "/panel.html" || path == "/peek.html" || path == "/settings.html" || path == "/boost.html" || path == "/menu.html" || path == "/history.html" || path == "/downloads.html" || path == "/devtools.html" ? "/index.html" : path);
    if (found == assets_.end()) return nullptr;
    auto& asset = found->second;
    auto stream = CefStreamReader::CreateForData(asset.body.data(), asset.body.size());
    CefResponse::HeaderMap headers;
    headers.emplace("Content-Security-Policy", "default-src 'none'; script-src 'self'; style-src 'self'; img-src 'self' data:; connect-src 'self'; base-uri 'none'; frame-ancestors 'none'; form-action 'none'");
    headers.emplace("X-Content-Type-Options", "nosniff");
    headers.emplace("Cache-Control", "no-store");
    if (path == "/panel.html" || path == "/welcome.html" || path == "/command.html" || path == "/settings.html" || path == "/boost.html" || path == "/menu.html" || path == "/history.html" || path == "/downloads.html") {
      auto body = std::make_shared<std::string>(asset.body);
      const auto html = body->find("<html");
      if (html != std::string::npos) body->insert(html + 5, path == "/panel.html" ? " data-ui=\"panel\"" : path == "/welcome.html" ? " data-ui=\"welcome\"" : path == "/settings.html" ? " data-ui=\"settings\"" : path == "/boost.html" ? " data-ui=\"boost\"" : path == "/menu.html" ? " data-ui=\"menu\"" : path == "/history.html" || path == "/downloads.html" ? " data-ui=\"page\"" : " data-ui=\"command\"");
      return new ThemeResource(body, CefStreamReader::CreateForData(body->data(), body->size()), headers, "text/html");
    }
    return new CefStreamResourceHandler(200, "OK", asset.mime, headers, stream);
  }
 private:
  struct Asset { std::string mime; std::string body; };
  std::shared_ptr<State> state_;
  std::map<std::string, Asset> assets_;
  IMPLEMENT_REFCOUNTING(UiResources);
};

class Client;
// Zooms |page| (Ctrl+wheel, Ctrl+=/-/0, the address bar's zoom badge) and shows
// the level as a toast over that page; the top bar's badge follows.
void ZoomPage(const std::shared_ptr<State>& state, CefRefPtr<CefBrowser> page, cef_zoom_command_t command) {
  page->GetHost()->Zoom(command);
  CefPostTask(TID_UI, base::BindOnce([](std::weak_ptr<State> weak, CefRefPtr<CefBrowser> page) {
    const auto state = weak.lock();
    if (!state || !state->embedded || !page->IsValid()) return;
    const int percent = static_cast<int>(std::lround(100 * std::pow(1.2, page->GetHost()->GetZoomLevel())));
    // Just the level ("110%"), over the page that zoomed (in a split, the pane under the pointer).
    state->embedded->Notify("zoom", std::to_wstring(percent) + L"%", {}, L"", 2500, page->GetIdentifier());
    if (state->workspace) state->workspace->Refresh();
    auto d = CefDictionaryValue::Create(); d->SetInt("percent", percent); state->Record("page_zoom", d);
  }, std::weak_ptr<State>(state), page));
}
// Website tabs' clients by browser ID (UI thread), for live tab transfer.
std::map<int, Client*>& PageClients() { static std::map<int, Client*> clients; return clients; }
class Client final : public CefClient, public CefLifeSpanHandler,
    public CefLoadHandler, public CefDisplayHandler, public CefRequestHandler,
    public CefFocusHandler, public CefJSDialogHandler, public CefKeyboardHandler,
    public CefCommandHandler, public CefDragHandler, public CefResourceRequestHandler, public CefContextMenuHandler, public CefDownloadHandler, public CefPermissionHandler,
    public CefMessageRouterBrowserSide::Handler {
 public:
  // |foreign|: a workspace browser that Chrome created itself (extension
  // tabs.create, onboarding/options pages) rather than one our host created.
  // |internal_page|: our history/downloads page tab (trusted UI profile, page IPC only).
  Client(std::shared_ptr<State> state, bool shell, std::string node_id = "", bool popup = false,
         std::pair<int, int> reservation = {-1, -1}, bool foreign = false, bool internal_page = false)
      : node_id_(std::move(node_id)), state_(state), shell_(shell), popup_(popup), foreign_(foreign),
        internal_page_(internal_page), reservation_(reservation) {
    router_ = CefMessageRouterBrowserSide::Create(CefMessageRouterConfig());
    router_->AddHandler(this, false);
  }
  ~Client() override {
    router_->RemoveHandler(this);
    // Popup creation was aborted after Sol made its window.
    if (popup_host_ && !popup_created_) red::ReleaseWebsitePopupHost(popup_host_);
  }
  void SetPopupHost(HWND host) { popup_host_ = host; }
  CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return this; }
  CefRefPtr<CefLoadHandler> GetLoadHandler() override { return this; }
  CefRefPtr<CefDisplayHandler> GetDisplayHandler() override { return this; }
  CefRefPtr<CefRequestHandler> GetRequestHandler() override { return this; }
  CefRefPtr<CefFocusHandler> GetFocusHandler() override { return this; }
  CefRefPtr<CefCommandHandler> GetCommandHandler() override { return this; }
  CefRefPtr<CefJSDialogHandler> GetJSDialogHandler() override { return this; }
  CefRefPtr<CefKeyboardHandler> GetKeyboardHandler() override { return this; }
  CefRefPtr<CefDragHandler> GetDragHandler() override { return this; }
  // The shell accepts local files dropped on the sidebar; remember their paths
  // from the OS drag so the drop command opens exactly those (up to 10).
  bool OnDragEnter(CefRefPtr<CefBrowser>, CefRefPtr<CefDragData> data, DragOperationsMask) override {
    if (!shell_) return false;
    state_->shell_drop_files.clear();
    if (!data || !data->IsFile()) return false;
    // GetFileNames returns display names/base names in pinned CEF 154, not
    // paths. Only GetFilePaths can be validated and opened as local files.
    std::vector<CefString> paths; data->GetFilePaths(paths);
    for (const auto& path : paths) {
      const auto url = red::LocalFileUrl(fs::path(path.ToWString()));
      if (!url.empty() && state_->shell_drop_files.size() < 10) state_->shell_drop_files.push_back(url);
    }
    return false;
  }
  // Only for navigations to external protocols (mailto:, tg:, ...).
  CefRefPtr<CefResourceRequestHandler> GetResourceRequestHandler(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>,
      CefRefPtr<CefRequest> request, bool is_navigation, bool, const CefString&, bool&) override {
    if (shell_ || internal_page_ || foreign_ || !is_navigation || !state_->workspace) return nullptr;
    if (auto fixture=red::IntegrationFixtureResource(state_,request->GetURL().ToString()))return fixture;
    CefURLParts parts;
    if (!CefParseURL(request->GetURL(), parts) || !red::IsExternalProtocolScheme(CefString(&parts.scheme).ToString())) return nullptr;
    return this;
  }
  // Hand the link to Chrome's external protocol handler: it blocks denied
  // schemes (shell:, vbscript:, ...), applies its gesture rules and asks
  // "Open <app>?" with a per-site "always allow", like Chrome.
  void OnProtocolExecution(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, CefRefPtr<CefRequest> request, bool& allow) override {
    CefURLParts parts; CefParseURL(request->GetURL(), parts);
    allow = red::IsExternalProtocolScheme(CefString(&parts.scheme).ToString());
    auto data = CefDictionaryValue::Create(); data->SetString("scheme", CefString(&parts.scheme).ToString().substr(0, 64));
    data->SetBool("allowed", allow); state_->Record("external_protocol", data);
  }
  CefRefPtr<CefDownloadHandler> GetDownloadHandler() override { return this; }
  CefRefPtr<CefPermissionHandler> GetPermissionHandler() override { return this; }
  // Websites in the workspace download through our manager; the trusted shell never downloads.
  bool CanDownload(CefRefPtr<CefBrowser>, const CefString&, const CefString&) override {
    return !shell_ && !internal_page_ && state_->downloads != nullptr;
  }
  bool OnBeforeDownload(CefRefPtr<CefBrowser> browser, CefRefPtr<CefDownloadItem> item, const CefString& suggested_name,
                        CefRefPtr<CefBeforeDownloadCallback> callback) override {
    const auto profile = red::ProfileForBrowser(state_,browser);
    const auto downloads = profile ? profile->downloads : state_->downloads;
    return !shell_ && downloads && downloads->OnBeforeDownload(item, suggested_name, callback);
  }
  void OnDownloadUpdated(CefRefPtr<CefBrowser> browser, CefRefPtr<CefDownloadItem> item,
                         CefRefPtr<CefDownloadItemCallback> callback) override {
    const auto profile = red::ProfileForBrowser(state_,browser);
    const auto downloads = profile ? profile->downloads : state_->downloads;
    if (!shell_ && downloads) downloads->OnUpdated(item, callback);
  }
  // Red's own UI (the shell) gets no browser page menu; website pages get Chrome's, drawn as Red's.
  CefRefPtr<CefContextMenuHandler> GetContextMenuHandler() override { if (shell_) return new red::UiContextMenu(state_); return this; }
  void OnBeforeContextMenu(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>,
      CefRefPtr<CefContextMenuParams>, CefRefPtr<CefMenuModel> model) override {
    if (shell_ || foreign_ || !state_->workspace) return;
    if (internal_page_) { model->Clear(); return; }  // Trusted UI: no inspector, no page menu.
    if (state_->probe) { auto d = CefDictionaryValue::Create(); d->SetInt("items", static_cast<int>(model->GetCount())); state_->Record("page_context_menu", d); }
    // Chrome Runtime already supplies Inspect. Replace it with our exact
    // point-targeted command instead of showing two inspector entries.
    const int builtin = cef_id_for_command_id_name("IDC_CONTENT_CONTEXT_INSPECTELEMENT");
    const bool replaced = builtin >= 0 && model->Remove(builtin);
    if (state_->probe) {
      auto d = CefDictionaryValue::Create(); d->SetBool("replaced_builtin", replaced);
      state_->Record("devtools_context_menu", d);
    }
    // "Open link as <profile>": Chrome lists Red's other profiles, and the only
    // other one is the trusted UI (Shell) profile. Websites never load there.
    for (size_t i = model->GetCount(); i-- > 0;) if (red::OpensLinkInProfile(model->GetCommandIdAt(i))) model->RemoveAt(i);
    // Chrome's incognito item opens the link in a Red private window (OnContextMenuCommand).
    if (const int private_link = cef_id_for_command_id_name("IDC_CONTENT_CONTEXT_OPENLINKOFFTHERECORD"); private_link >= 0) {
      if (state_->private_window) model->Remove(private_link);  // Already private: "new window" opens one.
      else model->SetLabel(private_link, "Open link in private window");
    }
    if (model->GetCount()) model->AddSeparator();
    model->AddItem(red::kInspectElementMenu, "Inspect element");
  }
  // Website pages: Chrome's menu, drawn as Red's own (windows/page_menu.cpp); the choice goes back to Chrome.
  bool RunContextMenu(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, CefRefPtr<CefContextMenuParams>, CefRefPtr<CefMenuModel> model,
      CefRefPtr<CefRunContextMenuCallback> callback) override {
    if (shell_ || foreign_ || internal_page_ || !state_->workspace) return false;
    return red::ShowPageMenu(state_, model, callback);
  }
  bool OnContextMenuCommand(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame>,
      CefRefPtr<CefContextMenuParams> params, int command, EventFlags) override {
    if (red::OpensLinkInProfile(command)) return true;  // Removed above; never run it either.
    const bool private_link = command == cef_id_for_command_id_name("IDC_CONTENT_CONTEXT_OPENLINKOFFTHERECORD");
    if ((private_link || command == cef_id_for_command_id_name("IDC_CONTENT_CONTEXT_OPENLINKNEWWINDOW")) && !shell_ && !foreign_ && !internal_page_) {
      // A new Red window (private: docs/private-windows.md) instead of Chrome's own browser window,
      // which Red would otherwise turn into a tab. A private page's "new window" stays private.
      const auto url = params->GetLinkUrl().ToString();
      if ((url.starts_with("https://") || url.starts_with("http://")) && url.size() <= 8192 && state_->session && state_->session->new_window_with_link) {
        const bool opened = state_->session->new_window_with_link(state_, url, private_link || state_->private_window);
        if (state_->probe) { auto d = CefDictionaryValue::Create(); d->SetBool("opened", opened); d->SetBool("private", private_link); state_->Record("link_window_opened", d); }
      }
      return true;
    }
    if (command != red::kInspectElementMenu || shell_ || foreign_ || internal_page_) return false;
    return red::ShowPageDevTools(state_, browser, CefPoint(params->GetXCoord(), params->GetYCoord()));
  }
  void OnBeforeDevToolsPopup(CefRefPtr<CefBrowser> browser, CefWindowInfo& info,
      CefRefPtr<CefClient>& client, CefBrowserSettings&, CefRefPtr<CefDictionaryValue>& extra,
      bool* use_default_window) override {
    client = red::DevToolsClient(state_, browser->GetIdentifier());
    extra = CefDictionaryValue::Create();  // Never inherit trusted_ui or UI capabilities.
    red::ConfigurePageDevTools(state_, browser->GetIdentifier(), info);
    *use_default_window = true;
  }
  // Frameless workspace window: only the trusted shell's main frame may define
  // where the window can be dragged. Websites never move the window.
  void OnDraggableRegionsChanged(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
      const std::vector<CefDraggableRegion>& regions) override {
    if (!shell_ || !state_->workspace || !state_->window || !frame->IsMain() ||
        !red::IsTrustedUi(browser->GetIdentifier(), state_->shell_id.load(), true, frame->GetURL().ToString())) return;
    // The shell view fills the window from (0,0): view and window coordinates match.
    state_->window->SetDraggableRegions(regions);
  }
  bool OnPreKeyEvent(CefRefPtr<CefBrowser>, const CefKeyEvent& event, CefEventHandle,
                    bool*) override {
    if (event.type == KEYEVENT_RAWKEYDOWN && event.windows_key_code == VK_ESCAPE &&
        !(event.modifiers & (EVENTFLAG_CONTROL_DOWN | EVENTFLAG_ALT_DOWN | EVENTFLAG_SHIFT_DOWN | EVENTFLAG_COMMAND_DOWN)) &&
        state_->panel_popup && state_->panel_popup->DismissIntegration()) return true;
    if (!popup_ && state_->workspace && state_->content && state_->devtools_dock && event.type == KEYEVENT_RAWKEYDOWN &&
        event.windows_key_code == 'M' && (event.modifiers & EVENTFLAG_CONTROL_DOWN) && (event.modifiers & EVENTFLAG_SHIFT_DOWN) &&
        !(event.modifiers & (EVENTFLAG_ALT_DOWN | EVENTFLAG_COMMAND_DOWN)))
      return state_->devtools_dock->ToggleDevice(state_->content->GetIdentifier());
    if (!popup_ && state_->workspace && event.type == KEYEVENT_RAWKEYDOWN && !(event.modifiers & EVENTFLAG_COMMAND_DOWN))
      if (auto action = red::ShortcutKeyFor(state_, event.windows_key_code, (event.modifiers & EVENTFLAG_CONTROL_DOWN) != 0,
          (event.modifiers & EVENTFLAG_SHIFT_DOWN) != 0, (event.modifiers & EVENTFLAG_ALT_DOWN) != 0))
        return red::ExecuteShortcut(state_, *action);
    return false;
  }
  bool OnBeforeUnloadDialog(CefRefPtr<CefBrowser> browser, const CefString&, bool is_reload,
      CefRefPtr<CefJSDialogCallback> callback) override {
    if (!state_->workspace) return false;
    if (state_->window_close && state_->window_close->DiscardApproved(browser->GetIdentifier(), is_reload)) {
      state_->Record("window_close_unload_approved"); callback->Continue(true, ""); return true;
    }
    if (state_->workspace->AutoArchiveClosing(browser->GetIdentifier())) {
      callback->Continue(false,""); state_->workspace->CancelClose(browser->GetIdentifier());
      if (state_->window_close) state_->window_close->Cancel(browser->GetIdentifier());
      return true;
    }
    CefPostTask(TID_UI, base::BindOnce([](std::shared_ptr<State> state, CefRefPtr<CefBrowser> browser, CefRefPtr<CefJSDialogCallback> callback) {
      const HWND owner = state->window ? state->window->GetWindowHandle() : nullptr;
      auto probe_decision = red::OrganizationUnloadDecision(state, browser->GetIdentifier());
      if (!probe_decision) probe_decision = red::LifecycleUnloadDecision(state, browser->GetIdentifier());
      const bool leave = probe_decision.has_value() ? *probe_decision : MessageBoxW(owner,
          L"This page may contain unsaved changes. Leave the page?", L"Leave page?", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) == IDYES;
      if (!leave) {
        state->workspace->CancelClose(browser->GetIdentifier());
        if (state->window_close) state->window_close->Cancel(browser->GetIdentifier());
      }
      callback->Continue(leave, "");
    }, state_, browser, callback));
    return true;
  }
  bool OnChromeCommand(CefRefPtr<CefBrowser> browser, int command, cef_window_open_disposition_t disposition) override {
    if (!state_->workspace || popup_) return false;
    if (command == cef_id_for_command_id_name("IDC_SHOW_PASSWORD_MANAGER") || command == cef_id_for_command_id_name("IDC_VIEW_PASSWORDS")) {
      // Resolve the source browser, which can belong to a background Space.
      if(const auto profile=red::ProfileForBrowser(state_,browser))red::ShowPasswords(state_,profile->id);
      return true;
    }
    if (command == cef_id_for_command_id_name("IDC_DEV_TOOLS")) {
      red::ShowPageDevTools(state_, shell_ ? state_->content : browser, CefPoint(), true); return true;
    }
    static const int zoom_in = cef_id_for_command_id_name("IDC_ZOOM_PLUS"), zoom_out = cef_id_for_command_id_name("IDC_ZOOM_MINUS"),
        zoom_reset = cef_id_for_command_id_name("IDC_ZOOM_NORMAL");
    if (command == zoom_in || command == zoom_out || command == zoom_reset) {
      // Ctrl+wheel, Ctrl+=/-/0 on a page: zoom it and show Red's toast.
      // Chrome's own zoom bubble cannot be turned off yet (cef-patches.md).
      // Sol's own pages (History, Downloads) never zoom: they share the sol://ui
      // origin and profile with the sidebar and top bar, and Chrome keeps zoom
      // per origin, so zooming them scaled the whole browser.
      if (shell_ || internal_page_) return true;
      ZoomPage(state_, browser, command == zoom_in ? CEF_ZOOM_COMMAND_IN : command == zoom_out ? CEF_ZOOM_COMMAND_OUT : CEF_ZOOM_COMMAND_RESET);
      return true;
    }
    if (auto action = red::ChromeShortcut(command)) return red::ExecuteShortcut(state_, *action);
    // Sol's own pages (trusted UI): none of Chrome's page commands (view source,
    // save or print the page, DevTools console), only find in page.
    if (internal_page_) {
      static const int find = cef_id_for_command_id_name("IDC_FIND"), next = cef_id_for_command_id_name("IDC_FIND_NEXT"),
          previous = cef_id_for_command_id_name("IDC_FIND_PREVIOUS");
      return command != find && command != next && command != previous;
    }
    return command == cef_id_for_command_id_name("IDC_NEW_WINDOW") ||
        command == cef_id_for_command_id_name("IDC_NEW_INCOGNITO_WINDOW");
  }
  void OnGotFocus(CefRefPtr<CefBrowser> browser) override {
    // A click in the shell (address, composer) gives it CEF focus but can leave
    // the Win32 keyboard target on the page's own child window, so typing went
    // nowhere visible. Move only the Win32 target; CEF focus is already here.
    if (!red::BrowserIsForeground(browser) || !red::BrowserOwnsKeyboard(state_, browser)) return;
    if (!shell_ && !popup_ && !foreign_ && state_->workspace) state_->workspace->OnPageFocused(browser);
    if (shell_ && state_->ui_editing) state_->keyboard_target = browser->GetIdentifier();
    if (!shell_ && !popup_ && !foreign_ && state_->workspace && state_->content &&
        state_->content->GetIdentifier() == browser->GetIdentifier()) {
      state_->ui_editing = false; state_->keyboard_target = browser->GetIdentifier();
      const auto shell = state_->browsers.find(state_->shell_id);
      if (shell != state_->browsers.end()) {
        // Chrome child and Alloy Shell have separate focus managers. Moving
        // Win32 focus alone can leave both renderers reporting document focus.
        shell->second->GetHost()->SetFocus(false);
        shell->second->GetMainFrame()->ExecuteJavaScript(
            "{const e=document.activeElement;if(!document.hasFocus()&&e instanceof HTMLButtonElement&&e.closest('.topbar'))e.blur();}",
            std::string(red::kUiUrl), 0);
      }
    }
    RestoreContentStack(state_);
    if (!shell_ && !foreign_ && state_->tabs) state_->tabs->OnFocus(browser->GetIdentifier());
  }

  void OnAfterCreated(CefRefPtr<CefBrowser> browser) override {
    CEF_REQUIRE_UI_THREAD();
    if (shell_ || internal_page_) red::RegisterSessionUi(state_, browser->GetIdentifier());
    state_->browsers[browser->GetIdentifier()] = browser;
    if (!shell_ && state_->workspace) {
      auto profile = red::ProfileForBrowser(state_,browser);
      if (!profile && internal_page_) profile = state_->workspace->ProfileForNode(node_id_);
      if (profile) {
        state_->browser_profiles[browser->GetIdentifier()] = profile;
        if (!internal_page_ && !profile->install_watch) profile->install_watch = red::WatchExtensionInstalls(state_,profile->context);
      }
    }
    if (MovablePage()) {
      const int id = browser->GetIdentifier();
      PageClients()[id] = this;
      if (state_->workspace && browser->GetHost()->GetRuntimeStyle() == CEF_RUNTIME_STYLE_CHROME &&
          !SetWindowSubclass(browser->GetHost()->GetWindowHandle(), PageWindowMessage, id, 0))
        state_->Record("page_activation_observer_failed");
    }
    if (foreign_) { OnForeignCreated(browser); return; }
    if (reservation_.first != -1) state_->popup_pending.erase(reservation_);
    if (popup_) state_->popup_browsers.insert(browser->GetIdentifier());
    if (popup_host_) { popup_created_ = true; red::AttachWebsitePopupBrowser(popup_host_, browser); }
    if (internal_page_) state_->internal_pages.insert(browser->GetIdentifier());
    if (!shell_ && !popup_ && !internal_page_) page_load_trace_ = red::TracePageLoad(state_, browser);
    if (!shell_ && !popup_ && !internal_page_) red::InstallVideoPipButton(state_, browser);
    if (!shell_ && !popup_ && !internal_page_) red::InstallBoosts(state_, browser);
    if (shell_) state_->shell_id = browser->GetIdentifier();
    // Workspace selection/visibility is committed by EnsureActive after the
    // child's automatic initial Show(), including the first restored page.
    else if (!popup_ && !state_->content && !state_->workspace_mode) state_->content = browser;
    auto details = CefDictionaryValue::Create();
    details->SetString("role", shell_ ? "shell" : popup_ ? "popup" : "content");
    details->SetInt("browser_id", browser->GetIdentifier());
    state_->Record("browser_created", details);
    if (!shell_ && !popup_ && state_->embedded) state_->embedded->OnCreated(browser);
    if (!shell_ && !popup_ && state_->workspace) {
      if (named_tab_url_.empty()) state_->workspace->OnCreated(browser, node_id_);
      else state_->workspace->RegisterNamedTab(browser, named_tab_url_, reservation_.first, named_tab_foreground_);
    }
    if (!shell_ && !popup_ && state_->tabs) state_->tabs->OnCreated(browser);
  }
  // A website tab (not the shell, a popup, an extension-made or internal page)
  // can move live to another window.
  bool MovablePage() const { return !shell_ && !popup_ && !foreign_ && !internal_page_; }
  // After a live move every callback of this page serves its new window.
  void Retarget(std::shared_ptr<State> state, std::string node_id) {
    state_ = std::move(state); node_id_ = std::move(node_id); page_load_trace_ = nullptr;
  }
  bool DoClose(CefRefPtr<CefBrowser> browser) override {
    if (!internal_page_ || browser->GetHost()->GetRuntimeStyle() != CEF_RUNTIME_STYLE_ALLOY) return false;
    // An Alloy child would send WM_CLOSE to our root window (re-entering the
    // window close sequence); destroy only this tab's child window instead.
    const HWND handle = browser->GetHost()->GetWindowHandle();
    CefPostTask(TID_UI, base::BindOnce([](HWND window) { if (IsWindow(window)) DestroyWindow(window); }, handle));
    return true;
  }
  void OnBeforeClose(CefRefPtr<CefBrowser> browser) override {
    if (MovablePage()) RemoveWindowSubclass(browser->GetHost()->GetWindowHandle(), PageWindowMessage, browser->GetIdentifier());
    PageClients().erase(browser->GetIdentifier());
    red::UnregisterSessionUi(state_, browser->GetIdentifier());
    state_->internal_pages.erase(browser->GetIdentifier());
    state_->pip_buttons.erase(browser->GetIdentifier());
    if (state_->session) red::ForgetBoostPage(*state_->session, browser->GetIdentifier());
    if (!shell_) red::ClosePageDevTools(state_, browser->GetIdentifier());
    lifecycle_capture_ = nullptr;
    page_load_trace_ = nullptr;
    router_->OnBeforeClose(browser);
    queries_->pending.clear();
    if (queries_->subscription && state_->workspace) state_->workspace->SetObserver({});
    queries_->subscription.reset();
    // A foreign root stays suppressed until Windows destroys it: Chrome shows
    // it after our close callbacks (measured), see embedded_content.cpp.
    const bool page = !shell_ && !popup_ && !foreign_;
    if (page && state_->embedded) state_->embedded->OnClosed(browser->GetIdentifier());
    if (page && state_->workspace) state_->workspace->OnClosed(browser->GetIdentifier(), browser->GetMainFrame()->GetURL());
    if (page && state_->tabs) state_->tabs->OnClosed(browser->GetIdentifier());
    state_->popup_browsers.erase(browser->GetIdentifier());
    if (state_->media_access.erase(browser->GetIdentifier()) && state_->workspace) state_->workspace->Refresh();
    if (state_->audible_frames.erase(browser->GetIdentifier()) && state_->workspace) state_->workspace->Refresh();
    std::erase_if(state_->popup_pending, [&](const auto& key) { return key.first == browser->GetIdentifier(); });
    state_->browsers.erase(browser->GetIdentifier());
    state_->browser_profiles.erase(browser->GetIdentifier());
    if (shell_) state_->shell_id = -1;
    else if (state_->content == browser) state_->content = nullptr;
    if (popup_host_) red::ReleaseWebsitePopupHost(popup_host_);
    state_->Record(shell_ ? "shell_closed" : popup_ ? "popup_closed" : foreign_ ? "foreign_closed" : "content_closed");
    if (state_->window_close) state_->window_close->OnClosed(browser->GetIdentifier());
    if (state_->browsers.empty() && (!state_->window_close || !state_->window)) red::QuitIfLastWindow(state_);
  }
  bool OnBeforePopup(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, int popup_id,
      const CefString& target_url, const CefString& frame_name, cef_window_open_disposition_t disposition, bool gesture,
      const CefPopupFeatures& features, CefWindowInfo& window_info, CefRefPtr<CefClient>& client,
      CefBrowserSettings&, CefRefPtr<CefDictionaryValue>&, bool*) override {
    if (shell_ || foreign_ || internal_page_ || !state_->workspace || state_->workspace->WindowClosing() || !gesture) return true;
    CefURLParts parts;
    const bool website = CefParseURL(target_url, parts) && parts.host.length && !parts.username.length && !parts.password.length &&
        (CefString(&parts.scheme) == "https" || CefString(&parts.scheme) == "http");
    if (!website && target_url != "about:blank") return true;
    const bool shift=(GetAsyncKeyState(VK_SHIFT)&0x8000)!=0, explicit_tab=(GetAsyncKeyState(VK_CONTROL)&0x8000) || (GetAsyncKeyState(VK_MENU)&0x8000);
    if (website && !features.isPopup && !explicit_tab && (frame_name.empty() || frame_name=="_blank") &&
        (disposition==CEF_WOD_NEW_FOREGROUND_TAB || (shift && disposition==CEF_WOD_NEW_WINDOW))) {
      if (state_->workspace->IsLinkPeekBrowser(browser->GetIdentifier())) { browser->GetMainFrame()->LoadURL(target_url); return true; }
      if (state_->workspace->RouteLinkPeek(browser,target_url,shift)) return true;
    }
    const bool tab = website && !features.isPopup &&
        (disposition == CEF_WOD_NEW_FOREGROUND_TAB || disposition == CEF_WOD_NEW_BACKGROUND_TAB);
    const bool foreground = disposition != CEF_WOD_NEW_BACKGROUND_TAB;
    // Chrome Runtime normalizes ordinary target=_blank links to an empty name.
    if (tab && (frame_name.empty() || frame_name == "_blank")) {
      // The opening frame is the link's initiator (Referer, Sec-Fetch-Site).
      state_->workspace->OpenLink(target_url, foreground, {}, frame ? frame->GetURL().ToString() : "", browser);
      state_->Record("link_popup_routed_to_tab"); return true;
    }
    if (state_->popup_pending.size() + state_->popup_browsers.size() >= 8) return true;
    const auto key = std::make_pair(browser->GetIdentifier(), popup_id);
    if (tab && state_->window && state_->embedded && browser->GetHost()->GetRequestContext()->IsSharingWith(state_->WebContext())) {
      // Keep Chromium's browsing context for a named target. Cancelling and
      // loading its URL in a fresh tab loses target reuse, the opener and POST.
      CefRefPtr<Client> named = new Client(state_, false, "", false, key);
      named->named_tab_url_ = target_url.ToString();
      named->named_tab_foreground_ = foreground;
      client = named;
      state_->popup_pending.insert(key);
      state_->embedded->ConfigureNewTabWindow(window_info);
      return false;
    }
    state_->popup_pending.insert(key);
    CefRefPtr<Client> popup = new Client(state_, false, "", true, key);
    client = popup;
    // Left to CEF this is a full Chrome window (tabs, toolbar) on the primary monitor, and with Sol
    // as parent CEF embeds it inside Sol's window (both measured). Sol makes a plain window owned by
    // its own, centred over it on its monitor and sized as the page asked, and creates the popup as
    // that window's child. Chrome style is kept: popups match their opener's runtime style.
    if (state_->window) {
      const HWND owner = state_->window->GetWindowHandle();
      MONITORINFO monitor{sizeof(monitor)};
      RECT owner_rect{};
      if (owner && GetWindowRect(owner, &owner_rect) && GetMonitorInfoW(MonitorFromWindow(owner, MONITOR_DEFAULTTONEAREST), &monitor)) {
        const UINT dpi = std::max(96u, GetDpiForWindow(owner));
        const double scale = dpi / 96.0;
        const RECT& work = monitor.rcWork;
        const CefSize content = red::WebsitePopupContentSize(features.widthSet, features.width, features.heightSet, features.height,
            CefSize(static_cast<int>((work.right - work.left) / scale), static_cast<int>((work.bottom - work.top) / scale)));
        RECT outer{0, 0, static_cast<LONG>(content.width * scale), static_cast<LONG>(content.height * scale)};
        AdjustWindowRectExForDpi(&outer, red::kWebsitePopupStyle, FALSE, 0, dpi);
        const CefRect bounds = red::WebsitePopupBounds(
            CefRect(owner_rect.left, owner_rect.top, owner_rect.right - owner_rect.left, owner_rect.bottom - owner_rect.top),
            CefRect(work.left, work.top, work.right - work.left, work.bottom - work.top),
            CefSize(outer.right - outer.left, outer.bottom - outer.top));
        if (const HWND host = red::CreateWebsitePopupHost(owner, bounds, state_->theme_dark)) {
          RECT client_rect{};
          GetClientRect(host, &client_rect);
          window_info.SetAsChild(host, CefRect(0, 0, client_rect.right, client_rect.bottom));
          window_info.runtime_style = CEF_RUNTIME_STYLE_CHROME;
          popup->SetPopupHost(host);
        }
      }
    }
    state_->Record("native_popup_allowed");
    return false;  // CEF creates the real browsing context, retaining window.opener.
  }
  void OnBeforePopupAborted(CefRefPtr<CefBrowser> browser, int id) override {
    state_->popup_pending.erase({browser->GetIdentifier(), id});
  }
  bool OnOpenURLFromTab(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, const CefString& url,
                       cef_window_open_disposition_t disposition, bool gesture) override {
    if (foreign_) return true;
    if (shell_ || !state_->workspace || disposition == CEF_WOD_CURRENT_TAB) return false;
    const bool shift=(GetAsyncKeyState(VK_SHIFT)&0x8000)!=0, explicit_tab=(GetAsyncKeyState(VK_CONTROL)&0x8000) || (GetAsyncKeyState(VK_MENU)&0x8000);
    if (gesture && !explicit_tab && (disposition==CEF_WOD_NEW_FOREGROUND_TAB || (shift && disposition==CEF_WOD_NEW_WINDOW))) {
      CefURLParts parts;
      const bool website=CefParseURL(url,parts) && parts.host.length && !parts.username.length && !parts.password.length &&
        (CefString(&parts.scheme)=="http" || CefString(&parts.scheme)=="https");
      if (website && state_->workspace->IsLinkPeekBrowser(browser->GetIdentifier())) { browser->GetMainFrame()->LoadURL(url); return true; }
      if (state_->workspace->RouteLinkPeek(browser,url,shift)) return true;
    }
    // A file dropped on a page: Blink opens it in a new foreground tab, without
    // a page gesture. Chrome rewrites file:// to about:blank#blocked before this
    // point unless the person's drop granted that file to the renderer.
    if (red::IsLocalFileUrl(url.ToString()) && (disposition == CEF_WOD_NEW_FOREGROUND_TAB || disposition == CEF_WOD_NEW_BACKGROUND_TAB)) {
      state_->workspace->OpenFile(url.ToString(), disposition == CEF_WOD_NEW_FOREGROUND_TAB);
      state_->Record("file_drop_opened"); return true;
    }
    if (gesture && (disposition == CEF_WOD_NEW_BACKGROUND_TAB || disposition == CEF_WOD_NEW_FOREGROUND_TAB || disposition == CEF_WOD_NEW_WINDOW))
      state_->workspace->OpenLink(url, disposition != CEF_WOD_NEW_BACKGROUND_TAB, {}, frame ? frame->GetURL().ToString() : "", browser);
    state_->Record("link_open_disposition"); return true;
  }
  bool OnBeforeBrowse(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                      CefRefPtr<CefRequest> request, bool gesture, bool redirect) override {
    router_->OnBeforeBrowse(browser, frame);
    if (!shell_ && !popup_ && !foreign_ && !internal_page_ && frame->IsMain() && state_->tab_trace.is_open()) {
      auto metrics = CefDictionaryValue::Create(); metrics->SetInt("browser_id", browser->GetIdentifier());
      state_->Record("page_load_navigation", metrics);
    }
    if (shell_) return request->GetURL().ToString() != red::kUiUrl || !frame->IsMain();
    // The history page is locked to its own document; pages open in new tabs.
    if (internal_page_) return !red::IsInternalPageUrl(request->GetURL().ToString()) || !frame->IsMain();
    if (foreign_) {
      // Nothing of a foreign page loads in place; its address is reopened.
      const auto url = request->GetURL().ToString();
      if (!frame->IsMain()) return true;
      if (red::ClassifyForeignPage(url) == red::ForeignPage::Wait) return false;
      ResolveForeign(browser, url); return true;
    }
    CefURLParts parts;
    if (!CefParseURL(request->GetURL(), parts)) return true;
    auto scheme = CefString(&parts.scheme).ToString();
    if (!popup_ && state_->workspace && frame->IsMain() && gesture && !redirect && request->GetMethod()=="GET" &&
        (request->GetTransitionType()&TT_SOURCE_MASK)==TT_LINK && !(GetAsyncKeyState(VK_CONTROL)&0x8000) && !(GetAsyncKeyState(VK_MENU)&0x8000) &&
        state_->workspace->RouteLinkPeek(browser,request->GetURL(),(GetAsyncKeyState(VK_SHIFT)&0x8000)!=0)) return true;
    // Extension pages reach workspace tabs only through AdoptExtensionPage or
    // an extension page's own navigation; Chrome still enforces
    // web_accessible_resources for website-initiated loads.
    // Local files: Chrome itself blocks website-initiated navigation to
    // file://, so what reaches a page is the person's own (drop, typed path).
    // External protocols continue to Chrome's handler (OnProtocolExecution).
    // Chrome's print preview shows the page through two frames of its own dialog, which CEF
    // reports as subframes of the printed page: allow exactly those (websites cannot navigate
    // to chrome:// or chrome-untrusted:// themselves).
    if (!frame->IsMain() && (request->GetURL().ToString().starts_with("chrome://print/") ||
        request->GetURL().ToString().starts_with("chrome-untrusted://print/"))) return false;
    return scheme != "https" && scheme != "http" && scheme != "about" &&
           !(state_->workspace && scheme == "chrome-extension") &&
           !(state_->workspace && scheme == "file") &&
           !(state_->workspace && red::IsExternalProtocolScheme(scheme)) &&
           !(state_->probe && scheme == "chrome");
  }
  void OnRenderProcessTerminated(CefRefPtr<CefBrowser> browser, TerminationStatus status,
      int error_code, const CefString&) override {
    router_->OnRenderProcessTerminated(browser);
    // A dead renderer cannot report that its frames stopped playing.
    if (state_->audible_frames.erase(browser->GetIdentifier()) && state_->workspace) state_->workspace->Refresh();
    if (foreign_) return;
    const bool recoverable = !shell_ && !popup_ && state_->workspace && state_->workspace->OnCrashed(browser);
    if (!recoverable) state_->exit_code = 2;
    auto details = CefDictionaryValue::Create(); details->SetInt("browser_id", browser->GetIdentifier());
    details->SetInt("status", status); details->SetInt("error_code", error_code);
    details->SetBool("recoverable", recoverable); state_->Record("renderer_terminated", details);
  }
  bool OnQuery(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
      int64_t query_id, const CefString& request, bool persistent,
      CefRefPtr<CefMessageRouterBrowserSide::Callback> callback) override {
    if (internal_page_) {
      // Only the registered history tab, main frame, exact URL; history methods only.
      if (!red::IsInternalPageUi(state_->internal_pages.contains(browser->GetIdentifier()), frame->IsMain(), frame->GetURL().ToString())) {
        callback->Failure(403, "Unauthorized caller"); return true;
      }
      if (request.length() > 8192 || persistent) { callback->Failure(400, "Invalid history request"); return true; }
      auto parsed = CefParseJSON(request, JSON_PARSER_RFC);
      auto dict = parsed && parsed->GetType() == VTYPE_DICTIONARY ? parsed->GetDictionary() : nullptr;
      if (!dict || dict->GetType("method") != VTYPE_STRING ||
          !(frame->GetURL().ToString() == red::kHistoryUrl
              ? red::HandleHistoryCommand(state_, dict->GetString("method").ToString(), dict, callback, {}, red::ProfileForBrowser(state_,browser))
              : red::HandleDownloadsPageCommand(state_, dict->GetString("method").ToString(), dict, callback, red::ProfileForBrowser(state_,browser))))
        callback->Failure(403, "Not a command of this page");
      return true;
    }
    if (!red::IsTrustedUi(browser->GetIdentifier(), state_->shell_id.load(),
                         frame->IsMain(), frame->GetURL().ToString())) {
      callback->Failure(403, "Unauthorized caller");
      return true;
    }
    RestoreContentStack(state_);
    if (request.length() > 8192) { callback->Failure(413, "Request too large"); return true; }
    auto parsed = CefParseJSON(request, JSON_PARSER_RFC);
    auto dict = parsed && parsed->GetType() == VTYPE_DICTIONARY ? parsed->GetDictionary() : nullptr;
    if (!dict || dict->GetType("method") != VTYPE_STRING) {
      callback->Failure(400, "Invalid command"); return true;
    }
    auto method = dict->GetString("method").ToString();
    if (red::HandleUiFocus(state_, browser, method, dict->GetSize(), persistent, callback)) return true;
    if (method == "workspace.sidebarResizeHovered") {
      // Trusted shell only (checked above). Never expose global pointer
      // coordinates or rely on DOM mouse-leave across native child HWNDs.
      if (!state_->workspace || persistent || dict->GetSize() != 2 || dict->GetType("width") != VTYPE_INT ||
          (dict->GetInt("width") != 2 && dict->GetInt("width") != 8)) {
        callback->Failure(400, "Invalid sidebar hover request"); return true;
      }
      bool hovered = false;
      if (state_->window && state_->sidebar_visible && !state_->window->IsMinimized() && !state_->window->IsFullscreen()) {
        const HWND root = state_->window->GetWindowHandle();
        POINT cursor{}; RECT client{};
        if (GetCursorPos(&cursor) && GetClientRect(root, &client) && GetAncestor(WindowFromPoint(cursor), GA_ROOT) == root && ScreenToClient(root, &cursor)) {
          const float scale = state_->window->GetDisplay()->GetDeviceScaleFactor();
          const int width = red::SidebarWidth(state_->sidebar_width, state_->window->GetClientAreaBoundsInScreen().width);
          const int left = static_cast<int>((width + red::kContentInset - dict->GetInt("width")) * scale + .5f);
          hovered = cursor.x >= left && cursor.x < static_cast<int>((width + red::kContentInset) * scale + .5f) &&
              cursor.y >= static_cast<int>(red::kToolbarHeight * scale + .5f) && cursor.y < client.bottom - static_cast<int>(8 * scale + .5f);
        }
      }
      if (state_->input_probe) {
        auto details = CefDictionaryValue::Create(); details->SetBool("hovered", hovered);
        state_->Record("sidebar_resize_pointer_checked", details);
      }
      callback->Success(hovered ? "true" : "false"); return true;
    }
    if (method == "workspace.contextMenu" || method == "workspace.spaceMenu") {
      if (!state_->sidebar_menu || persistent || dict->GetSize() != 4 || dict->GetType("id") != VTYPE_STRING ||
          (dict->GetString("id").length() != 36 && !dict->GetString("id").empty()) || dict->GetType("x") != VTYPE_INT || dict->GetType("y") != VTYPE_INT) {
        callback->Failure(400, "Invalid sidebar menu request"); return true;
      }
      state_->sidebar_menu->Open(dict->GetString("id"), dict->GetInt("x"), dict->GetInt("y"), query_id, callback,nullptr,method=="workspace.spaceMenu"); return true;
    }
    if (method == "probe.contextMenuSelect" && state_->probe && state_->organization_probe && state_->sidebar_menu && !persistent) {
      const auto action=dict->HasKey("action") ? dict->GetString("action").ToString() : "rename";
      if (dict->GetSize()!=(dict->HasKey("action") ? 2U : 1U) || (dict->HasKey("action") && dict->GetType("action")!=VTYPE_STRING) ||
          (action!="rename" && action!="removeFolder" && action!="editTheme")) { callback->Failure(400,"Invalid probe menu action"); return true; }
      if (!state_->sidebar_menu->ProbeSelect(action)) { callback->Failure(409, "Menu is not shown"); return true; }
      auto result = CefValue::Create(); result->SetDictionary(state_->workspace->Snapshot()); callback->Success(CefWriteJSON(result, JSON_WRITER_DEFAULT)); return true;
    }
    if (method == "probe.panelClick" && state_->probe && (state_->organization_probe || state_->downloads_probe) &&
        state_->panel_popup && !persistent && dict->GetSize() == 2 && dict->GetType("text") == VTYPE_STRING && dict->GetString("text").length() <= 200) {
      state_->panel_popup->ProbeClick(dict->GetString("text"));
      auto result = CefValue::Create(); result->SetDictionary(state_->workspace->Snapshot());
      callback->Success(CefWriteJSON(result, JSON_WRITER_DEFAULT)); return true;
    }
    if (method == "probe.commandInput" && state_->probe && (state_->organization_probe || state_->browsing_probe) &&
        state_->command_bar && !persistent && dict->GetSize() == 2 && dict->GetType("url") == VTYPE_STRING && dict->GetString("url").length() <= 4096) {
      state_->command_bar->ProbeInput(dict->GetString("url"));
      auto result = CefValue::Create(); result->SetDictionary(state_->workspace->Snapshot());
      callback->Success(CefWriteJSON(result, JSON_WRITER_DEFAULT)); return true;
    }
    if (method == "probe.commandCancel" && state_->probe && state_->browsing_probe && state_->command_bar &&
        !persistent && dict->GetSize() == 1) {
      state_->command_bar->ProbeCancel();
      auto result = CefValue::Create(); result->SetDictionary(state_->workspace->Snapshot());
      callback->Success(CefWriteJSON(result, JSON_WRITER_DEFAULT)); return true;
    }
    if (state_->workspace && state_->workspace->WindowClosing() && method != "browser.getSnapshot" && method != "browser.subscribe") {
      callback->Failure(409, "Window is closing"); return true;
    }
    if (red::HandleSidebarAction(state_,browser,method,dict,persistent,callback)) return true;
    if (method == "workspace.openDroppedFiles") {
      // Files the OS drag carried into the shell (recorded natively in
      // OnDragEnter; the shell document never supplies paths).
      if (!state_->workspace || persistent || dict->GetSize() != 1) { callback->Failure(400, "Invalid drop command"); return true; }
      auto files = std::move(state_->shell_drop_files); state_->shell_drop_files.clear();
      if (files.empty()) { state_->Record("shell_drop_without_files"); callback->Failure(409, "Only files from this computer can be dropped here"); return true; }
      bool first = true;
      for (const auto& file : files) { state_->workspace->OpenFile(file, first); first = false; }
      auto data = CefDictionaryValue::Create(); data->SetInt("files", static_cast<int>(files.size())); state_->Record("shell_files_dropped", data);
      auto value = CefValue::Create(); value->SetDictionary(state_->workspace->Snapshot());
      callback->Success(CefWriteJSON(value, JSON_WRITER_DEFAULT)); return true;
    }
    if (method == "navigation.copyLink") {
      if (!state_->workspace || persistent || dict->GetSize() != 1) {
        callback->Failure(400, "Invalid copy link command"); return true;
      }
      const auto snapshot = state_->workspace->Snapshot();
      if (!snapshot->GetBool("ready") || snapshot->GetString("activeId").empty() || snapshot->GetString("url").empty()) {
        callback->Failure(409, "No page address to copy"); return true;
      }
      const int code = red::CopyLinkText(state_->window ? state_->window->GetWindowHandle() : nullptr,
          CefString(std::string(red::ShownAddress(snapshot->GetString("url").ToString()))).ToWString());
      if (code) { callback->Failure(code, code == 409 ? "The clipboard is busy. Try again." : "Could not copy the page address"); return true; }
      red::FocusBrowser(state_->content);
      if (state_->embedded) state_->embedded->Notify("copy", L"Copied current URL");
      auto value = CefValue::Create(); value->SetDictionary(snapshot);
      state_->Record("page_link_copied"); callback->Success(CefWriteJSON(value, JSON_WRITER_DEFAULT)); return true;
    }
    if (method == "browser.shortcut") {
      const auto action = dict->GetType("action") == VTYPE_STRING ? red::ShortcutNamed(dict->GetString("action").ToString()) : std::nullopt;
      if (!state_->workspace || persistent || dict->GetSize() != 2 || !action) {
        callback->Failure(400, "Invalid shortcut command"); return true;
      }
      red::ExecuteShortcut(state_, *action);
      auto result = CefValue::Create(); result->SetDictionary(state_->workspace->Snapshot());
      callback->Success(CefWriteJSON(result, JSON_WRITER_DEFAULT)); return true;
    }
    if (method == "extension.open" || method == "extension.menu") {
      // A pinned extension's top-bar button (DIP rect): its popup opens under the button, right
      // edges aligned; its menu (right click) opens under the button.
      const bool numbers = dict->GetType("x") == VTYPE_INT && dict->GetType("y") == VTYPE_INT &&
          dict->GetType("width") == VTYPE_INT && dict->GetType("height") == VTYPE_INT;
      const auto bounds = state_->window ? state_->window->GetClientAreaBoundsInScreen() : CefRect();
      const auto id = dict->GetType("id") == VTYPE_STRING ? dict->GetString("id").ToString() : "";
      if (!state_->workspace || persistent || dict->GetSize() != 6 || !numbers || !red::ValidExtensionId(id) || state_->private_window ||
          !state_->extensions || !state_->extensions->Find(id) || dict->GetInt("x") < 0 || dict->GetInt("y") < 0 ||
          dict->GetInt("width") < 1 || dict->GetInt("height") < 1 ||
          dict->GetInt("x") + dict->GetInt("width") > bounds.width || dict->GetInt("y") + dict->GetInt("height") > bounds.height) {
        callback->Failure(400, "Invalid extension command"); return true;
      }
      const CefPoint below(bounds.x + dict->GetInt("x"), bounds.y + dict->GetInt("y") + dict->GetInt("height") + 6);
      const CefPoint popup(bounds.x + dict->GetInt("x") + dict->GetInt("width"), below.y);
      if (method == "extension.open") red::RunExtension(state_, id, popup);
      else red::ShowExtensionMenu(state_, id, below, popup);
      auto value = CefValue::Create(); value->SetDictionary(state_->workspace->Snapshot());
      callback->Success(CefWriteJSON(value, JSON_WRITER_DEFAULT)); return true;
    }
    // Back/Forward: right click lists the page's history, middle click opens the
    // previous/next entry in a new tab; the address bar's zoom badge resets zoom.
    if (method == "navigation.historyMenu" || method == "navigation.openEntry" || method == "navigation.resetZoom") {
      const auto page = state_->content;
      auto reply = [&] { auto value = CefValue::Create(); value->SetDictionary(state_->workspace->Snapshot()); callback->Success(CefWriteJSON(value, JSON_WRITER_DEFAULT)); };
      if (!state_->workspace || persistent || !page || state_->internal_pages.contains(page->GetIdentifier())) { callback->Failure(409, "No page"); return true; }
      if (method == "navigation.resetZoom" && dict->GetSize() == 1) { ZoomPage(state_, page, CEF_ZOOM_COMMAND_RESET); reply(); return true; }
      if (method == "navigation.openEntry" && dict->GetSize() == 2 && dict->GetType("offset") == VTYPE_INT &&
          (dict->GetInt("offset") == -1 || dict->GetInt("offset") == 1)) {
        red::OpenHistoryEntryInNewTab(state_, page, dict->GetInt("offset")); reply(); return true;
      }
      const auto bounds = state_->window ? state_->window->GetClientAreaBoundsInScreen() : CefRect();
      if (method == "navigation.historyMenu" && dict->GetSize() == 5 && dict->GetType("x") == VTYPE_INT && dict->GetType("y") == VTYPE_INT &&
          dict->GetType("width") == VTYPE_INT && dict->GetType("height") == VTYPE_INT && dict->GetInt("x") >= 0 && dict->GetInt("y") >= 0 &&
          dict->GetInt("width") >= 1 && dict->GetInt("height") >= 1 && dict->GetInt("x") + dict->GetInt("width") <= bounds.width &&
          dict->GetInt("y") + dict->GetInt("height") <= bounds.height) {
        red::ShowTabHistoryMenu(state_, page, CefRect(dict->GetInt("x"), dict->GetInt("y"), dict->GetInt("width"), dict->GetInt("height")));
        reply(); return true;
      }
      callback->Failure(400, "Invalid navigation command"); return true;
    }
    if (method == "menu.main") {
      // The logo button: the browser menu, anchored under the button (DIP).
      const bool numbers = dict->GetType("x") == VTYPE_INT && dict->GetType("y") == VTYPE_INT &&
          dict->GetType("width") == VTYPE_INT && dict->GetType("height") == VTYPE_INT;
      const auto bounds = state_->window ? state_->window->GetClientAreaBoundsInScreen() : CefRect();
      if (!state_->workspace || persistent || dict->GetSize() != 5 || !numbers || dict->GetInt("x") < 0 || dict->GetInt("y") < 0 ||
          dict->GetInt("width") < 1 || dict->GetInt("height") < 1 ||
          dict->GetInt("x") + dict->GetInt("width") > bounds.width || dict->GetInt("y") + dict->GetInt("height") > bounds.height) {
        callback->Failure(400, "Invalid menu command"); return true;
      }
      red::ShowMainMenu(state_, CefRect(dict->GetInt("x"), dict->GetInt("y"), dict->GetInt("width"), dict->GetInt("height")));
      auto value = CefValue::Create(); value->SetDictionary(state_->workspace->Snapshot());
      callback->Success(CefWriteJSON(value, JSON_WRITER_DEFAULT)); return true;
    }
    if (method == "panel.open") {
      // The shell names which popover to open and where its button is (DIP,
      // root client coordinates). Native code positions the popover itself.
      const auto kind = dict->GetType("kind") == VTYPE_STRING ? dict->GetString("kind").ToString() : "";
      const bool numbers = dict->GetType("x") == VTYPE_INT && dict->GetType("y") == VTYPE_INT &&
          dict->GetType("width") == VTYPE_INT && dict->GetType("height") == VTYPE_INT;
      const auto bounds = state_->window ? state_->window->GetClientAreaBoundsInScreen() : CefRect();
      if (!state_->panel_popup || persistent || dict->GetSize() != 6 || (kind != "archive" && kind != "downloads" && kind != "site" && kind != "history" && kind != "space" && kind != "newSpace") || !numbers ||
          dict->GetInt("x") < 0 || dict->GetInt("y") < 0 || dict->GetInt("width") < 1 || dict->GetInt("height") < 1 ||
          dict->GetInt("x") + dict->GetInt("width") > bounds.width || dict->GetInt("y") + dict->GetInt("height") > bounds.height) {
        callback->Failure(400, "Invalid panel command"); return true;
      }
      state_->panel_popup->Toggle(kind, CefRect(dict->GetInt("x"), dict->GetInt("y"), dict->GetInt("width"), dict->GetInt("height")));
      auto value = CefValue::Create(); value->SetDictionary(state_->workspace->Snapshot());
      callback->Success(CefWriteJSON(value, JSON_WRITER_DEFAULT)); return true;
    }
    if (method == "downloads.control" || method == "downloads.clear") {
      if (!state_->workspace || !state_->downloads) { callback->Failure(404, "Unknown method"); return true; }
      if (persistent) { callback->Failure(400, "Only subscriptions may persist"); return true; }
      std::string failure; int code = 0;
      if (method == "downloads.clear") {
        if (dict->GetSize() != 1) { callback->Failure(400, "Invalid downloads command"); return true; }
        state_->downloads->ClearFinished(false);
      } else {
        // IDs come from native snapshots; actions are a closed set.
        const auto action = dict->GetType("action") == VTYPE_STRING ? dict->GetString("action").ToString() : "";
        if (dict->GetSize() != 3 || dict->GetType("id") != VTYPE_INT || dict->GetInt("id") < 0 ||
            (action != "cancel" && action != "pause" && action != "resume" && action != "open" && action != "show" && action != "remove")) {
          callback->Failure(400, "Invalid downloads command"); return true;
        }
        code = state_->downloads->Control(static_cast<uint32_t>(dict->GetInt("id")), action, failure);
      }
      if (code) { callback->Failure(code, failure); return true; }
      auto value = CefValue::Create(); value->SetDictionary(state_->workspace->Snapshot());
      callback->Success(CefWriteJSON(value, JSON_WRITER_DEFAULT)); return true;
    }
    if (method == "window.control") {
      const auto action = dict->GetType("action") == VTYPE_STRING ? dict->GetString("action").ToString() : "";
      if (!state_->workspace || !state_->window || dict->GetSize() != 2 ||
          (action != "minimize" && action != "toggleMaximize" && action != "close")) {
        callback->Failure(400, "Invalid window command"); return true;
      }
      if (action == "minimize") state_->window->Minimize();
      else if (action == "toggleMaximize") { if (state_->window->IsMaximized()) state_->window->Restore(); else state_->window->Maximize(); }
      // Close goes through the normal coordinator (unload prompts, consent).
      auto value = CefValue::Create(); value->SetDictionary(state_->workspace->Snapshot());
      callback->Success(CefWriteJSON(value, JSON_WRITER_DEFAULT));
      if (action == "close") state_->window->Close();
      return true;
    }
    if (method == "workspace.setTheme") {
      // The shell re-sends the current theme when Windows' app mode changes
      // (System mode); the theme popover edits it (panel_popup.cpp).
      red::Theme theme; bool save = false;
      if (!state_->workspace || persistent || !red::WorkspaceController::ParseThemeCommand(dict, theme, save)) {
        callback->Failure(400, "Invalid theme command"); return true;
      }
      state_->workspace->SetTheme(theme, save, [callback](int code, const std::string& error, CefRefPtr<CefDictionaryValue> snapshot) {
        if (code) { callback->Failure(code, error); return; }
        auto value = CefValue::Create(); value->SetDictionary(snapshot);
        callback->Success(CefWriteJSON(value, JSON_WRITER_DEFAULT));
      },dict->GetString("spaceId").ToString());
      return true;
    }
    if (method == "devtools.emulate") {
      auto number = [&](const char* key) { return dict->GetType(key) == VTYPE_INT || dict->GetType(key) == VTYPE_DOUBLE; };
      auto numeric = [&](const char* key) -> double { return dict->GetType(key) == VTYPE_INT ? dict->GetInt(key) : dict->GetDouble(key); };
      if (!state_->devtools_dock || persistent || dict->GetSize() != 9 || dict->GetType("id") != VTYPE_INT ||
          dict->GetType("enabled") != VTYPE_BOOL || dict->GetType("mobile") != VTYPE_BOOL || dict->GetType("touch") != VTYPE_BOOL ||
          dict->GetType("width") != VTYPE_INT || dict->GetInt("width") < 50 || dict->GetInt("width") > 9999 ||
          dict->GetType("height") != VTYPE_INT || dict->GetInt("height") < 50 || dict->GetInt("height") > 9999 ||
          !number("dpr") || numeric("dpr") < 1 || numeric("dpr") > 4 ||
          !number("scale") || numeric("scale") < 0 || numeric("scale") > 2) {
        callback->Failure(400, "Invalid device preview settings"); return true;
      }
      red::DeviceConfig config; config.enabled = dict->GetBool("enabled"); config.mobile = dict->GetBool("mobile"); config.touch = dict->GetBool("touch");
      config.width = dict->GetInt("width"); config.height = dict->GetInt("height"); config.dpr = numeric("dpr"); config.scale = numeric("scale");
      if (!state_->devtools_dock->SetDevice(dict->GetInt("id"), config)) callback->Failure(409, "Device preview target changed");
      else { auto result = CefValue::Create(); result->SetDictionary(state_->workspace->Snapshot()); callback->Success(CefWriteJSON(result, JSON_WRITER_DEFAULT)); }
      return true;
    }
    if (method == "split.resizeHovered") {
      if (!state_->workspace || persistent || dict->GetSize() != 2 || dict->GetType("id") != VTYPE_STRING || dict->GetString("id").length() != 36) {
        callback->Failure(400,"Invalid split hover request"); return true;
      }
      callback->Success(state_->workspace->SplitResizeHovered(dict->GetString("id")) ? "true" : "false"); return true;
    }
    if (method == "devtools.resizeHovered") {
      if (!state_->devtools_dock || persistent || dict->GetSize() != 2 || dict->GetType("id") != VTYPE_INT) {
        callback->Failure(400, "Invalid developer tools hover request"); return true;
      }
      callback->Success(state_->devtools_dock->Hovered(dict->GetInt("id")) ? "true" : "false"); return true;
    }
    if (method == "devtools.control" || method == "devtools.resize" || method == "devtools.resizeLive") {
      const bool live = method == "devtools.resizeLive", resize = live || method == "devtools.resize";
      if (!state_->devtools_dock || persistent || dict->GetSize() != 3 || dict->GetType("id") != VTYPE_INT ||
          (resize ? dict->GetType("extent") != VTYPE_INT || dict->GetInt("extent") < 80 || dict->GetInt("extent") > 4096 :
            dict->GetType("action") != VTYPE_STRING || (dict->GetString("action") != "right" && dict->GetString("action") != "bottom" &&
              dict->GetString("action") != "left" && dict->GetString("action") != "window" && dict->GetString("action") != "close" && dict->GetString("action") != "device" && dict->GetString("action") != "focus"))) {
        callback->Failure(400, "Invalid developer tools command"); return true;
      }
      const bool applied = resize ? state_->devtools_dock->Resize(dict->GetInt("id"), dict->GetInt("extent"), live) :
          state_->devtools_dock->Control(dict->GetInt("id"), dict->GetString("action"));
      if (!applied) callback->Failure(409, "Developer tools changed");
      else { auto result = CefValue::Create(); result->SetDictionary(live ? state_->workspace->LayoutSnapshot() : state_->workspace->Snapshot()); callback->Success(CefWriteJSON(result, JSON_WRITER_DEFAULT)); }
      return true;
    }
    if (method == "workspace.setSidebarWidth") {
      if (!state_->workspace || persistent || dict->GetSize() != 3 || dict->GetType("width") != VTYPE_INT ||
          dict->GetInt("width") < red::kSidebarMinWidth || dict->GetInt("width") > red::kSidebarMaxWidth ||
          dict->GetType("save") != VTYPE_BOOL) {
        callback->Failure(400, "Invalid sidebar width command"); return true;
      }
      state_->workspace->SetSidebarWidth(dict->GetInt("width"), dict->GetBool("save"),
          [callback](int code, const std::string& error, CefRefPtr<CefDictionaryValue> snapshot) {
            if (code) { callback->Failure(code, error); return; }
            auto value = CefValue::Create(); value->SetDictionary(snapshot);
            callback->Success(CefWriteJSON(value, JSON_WRITER_DEFAULT));
          }); return true;
    }
    if (method == "workspace.setSidebarVisible") {
      if (!state_->workspace || dict->GetSize() != 2 || dict->GetType("visible") != VTYPE_BOOL) {
        callback->Failure(400, "Invalid sidebar visibility command"); return true;
      }
      state_->workspace->SetSidebarVisible(dict->GetBool("visible"));
      red::FocusBrowser(state_->content);
      auto value = CefValue::Create(); value->SetDictionary(state_->workspace->Snapshot());
      callback->Success(CefWriteJSON(value, JSON_WRITER_DEFAULT)); return true;
    }
    // The archive of every Space (not in the snapshot); the Archive panel gets it with its own payload.
    if (method == "workspace.archive" && state_->workspace && !persistent && dict->GetSize() == 1) {
      auto archive = CefDictionaryValue::Create(); archive->SetList("archive", state_->workspace->ArchiveSnapshot());
      auto value = CefValue::Create(); value->SetDictionary(archive);
      callback->Success(CefWriteJSON(value, JSON_WRITER_DEFAULT)); return true;
    }
    if (method == "browser.subscribe" && state_->workspace) {
      if (!persistent || dict->GetSize() != 1 || queries_->subscription) {
        callback->Failure(400, "Invalid or duplicate subscription"); return true;
      }
      queries_->subscription = query_id; queries_->pending[query_id] = callback;
      std::weak_ptr<Queries> weak = queries_;
      std::weak_ptr<red::State> weak_state = state_;
      state_->workspace->SetObserver([weak, weak_state, query_id](CefRefPtr<CefDictionaryValue> snapshot) {
        if (auto queries = weak.lock()) {
          const auto pending = queries->pending.find(query_id);
          if (pending != queries->pending.end()) {
            auto value = CefValue::Create(); value->SetDictionary(snapshot);
            const auto json = CefWriteJSON(value, JSON_WRITER_DEFAULT);
            if (const auto state = weak_state.lock()) {
              auto& counters = state->snapshot_counters;
              ++counters.shell_messages; counters.shell_bytes += json.length(); counters.last_bytes = json.length();
            }
            pending->second->Success(json);
          }
        }
      });
      return true;
    }
    if (persistent) { callback->Failure(400, "Only subscriptions may persist"); return true; }
    if (state_->workspace && red::IsTreeMethod(method)) {
      queries_->pending[query_id] = callback;
      state_->workspace->Handle(dict, AsyncReply(query_id)); return true;
    }
    if (!red::IsAllowedMethod(method)) { callback->Failure(404, "Unknown method"); return true; }
    if (dict->GetSize() != (method == "navigation.load" ? 2U : 1U)) {
      callback->Failure(400, "Unexpected command fields"); return true;
    }
    if (method == "browser.getSnapshot" && state_->workspace) {
      auto value = CefValue::Create(); value->SetDictionary(state_->workspace->Snapshot());
      callback->Success(CefWriteJSON(value, JSON_WRITER_DEFAULT)); state_->shell_ready = true;
      return true;
    }
    auto content = state_->content;
    // An empty space has no page: Back/Forward/Reload/Stop do nothing (no error).
    if (!content && method != "navigation.load" && state_->workspace) {
      auto value = CefValue::Create(); value->SetDictionary(state_->workspace->Snapshot());
      callback->Success(CefWriteJSON(value, JSON_WRITER_DEFAULT)); return true;
    }
    if (!content && method != "navigation.load") { callback->Failure(503, "Content browser not ready"); return true; }
    if (method == "navigation.load") {
      if (dict->GetType("url") != VTYPE_STRING || dict->GetString("url").length() > 4096) {
        callback->Failure(400, "Invalid URL"); return true;
      }
      auto url = dict->GetString("url");
      // sol://history or sol://downloads typed on the empty space: their one tab.
      if (const auto page = red::InternalPageForAddress(url.ToString()); !page.empty() && state_->workspace) {
        if (page == red::kHistoryUrl) state_->workspace->OpenHistoryPage(); else state_->workspace->OpenDownloadsPage();
        auto value = CefValue::Create(); value->SetDictionary(state_->workspace->Snapshot());
        callback->Success(CefWriteJSON(value, JSON_WRITER_DEFAULT)); return true;
      }
      CefURLParts parts;
      const bool local_file = state_->workspace && red::IsLocalFileUrl(url.ToString());
      if (!local_file && (!CefParseURL(url, parts) || !parts.host.length || parts.username.length ||
          parts.password.length || (CefString(&parts.scheme) != "https" &&
                                    CefString(&parts.scheme) != "http"))) {
        callback->Failure(400, "Only HTTP(S) URLs without credentials or local files are allowed"); return true;
      }
      if (state_->workspace) {
        queries_->pending[query_id] = callback;
        state_->workspace->Navigate(url, AsyncReply(query_id));
        return true;
      }
      if (!content) { callback->Failure(503, "Content browser not ready"); return true; }
      content->GetMainFrame()->LoadURL(url);
    } else if (method == "navigation.back") { if (state_->workspace) state_->workspace->NavigateHistory(false); else content->GoBack(); }
    else if (method == "navigation.forward") { if (state_->workspace) state_->workspace->NavigateHistory(true); else content->GoForward(); }
    else if (method == "navigation.reload") { if (state_->workspace) state_->workspace->Reload(); else content->Reload(); }
    else if (method == "navigation.stop") content->StopLoad();
    if (state_->workspace && method != "browser.getSnapshot") red::FocusBrowser(content);
    if (method != "browser.getSnapshot") {
      ++state_->revision;
      state_->navigation_command = true;
    }
    if (state_->workspace) {
      auto value = CefValue::Create(); value->SetDictionary(state_->workspace->Snapshot());
      callback->Success(CefWriteJSON(value, JSON_WRITER_DEFAULT)); return true;
    }
    auto result = CefDictionaryValue::Create();
    result->SetInt("revision", state_->revision);
    result->SetBool("canGoBack", content->CanGoBack());
    result->SetBool("canGoForward", content->CanGoForward());
    result->SetBool("probeMode", state_->probe);
    auto response = CefValue::Create(); response->SetDictionary(result);
    callback->Success(CefWriteJSON(response, JSON_WRITER_DEFAULT));
    state_->shell_ready = true;
    auto log = CefDictionaryValue::Create(); log->SetString("method", method);
    state_->Record("trusted_query", log);
    return true;
  }
  void OnQueryCanceled(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, int64_t query_id) override {
    if (shell_ && state_->sidebar_menu) state_->sidebar_menu->Cancel(query_id, false);
    queries_->pending.erase(query_id);
    if (queries_->subscription == query_id) {
      queries_->subscription.reset();
      if (state_->workspace) state_->workspace->SetObserver({});
    }
  }
  // Prompts stay Chrome's; see DecideMediaAccess for camera/microphone.
  bool OnShowPermissionPrompt(CefRefPtr<CefBrowser> browser, uint64_t prompt_id, const CefString& origin,
                              uint32_t permissions, CefRefPtr<CefPermissionPromptCallback>) override {
    red::RecordPermissionRequest(state_, browser, "prompt", origin.ToString(), permissions);
    return false;
  }
  bool OnRequestMediaAccessPermission(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame>, const CefString& origin,
                                      uint32_t permissions, CefRefPtr<CefMediaAccessCallback> callback) override {
    red::RecordPermissionRequest(state_, browser, "media", origin.ToString(), permissions);
    if (!shell_ && state_->workspace) state_->workspace->ProtectMediaRequest(browser->GetIdentifier());
    return !shell_ && red::DecideMediaAccess(browser, origin.ToString(), permissions, callback);
  }
#if CEF_API_ADDED(CEF_EXPERIMENTAL)
  // Chromium's own audible state (tab speaker icon): media elements, Web
  // Audio and calls. Red engine patch 04; replaces the renderer heuristic.
  void OnAudibleStateChange(CefRefPtr<CefBrowser> browser, bool audible) override {
    if (shell_ || popup_ || foreign_ || internal_page_) return;
    red::SetFrameAudible(state_, browser->GetIdentifier(), "chromium", audible);
    if (audible && state_->workspace) state_->workspace->TouchAudibleActivity(browser->GetIdentifier());
  }
  // The page's Media Session (engine patch 09): previous/next and artwork for the sidebar player.
  void OnMediaSessionChanged(CefRefPtr<CefBrowser> browser, CefRefPtr<CefDictionaryValue> info) override {
    if (shell_ || popup_ || foreign_ || internal_page_ || !state_->workspace) return;
    state_->workspace->OnMediaSession(browser, info);
  }
#endif
  void OnMediaAccessChange(CefRefPtr<CefBrowser> browser, bool video, bool audio) override {
    if (shell_ || foreign_) return;
    red::SetMediaAccess(state_, browser->GetIdentifier(), video, audio);
    red::OnSiteProbeMedia(state_, video, audio);
  }
  void OnFullscreenModeChange(CefRefPtr<CefBrowser> browser, bool fullscreen) override {
    // Chrome style starts its own transition; the page is a child of our
    // window, so we make the window fullscreen and give the page all of it.
    if (shell_ || popup_ || foreign_ || !state_->embedded) return;
    state_->embedded->SetFullscreen(browser, fullscreen);
    if (state_->workspace && (state_->embedded->HasSplit() || state_->embedded->HasLinkPeek())) state_->workspace->Refresh();
    red::OnFullscreenProbeChange(state_, browser, fullscreen);
  }
  void OnStatusMessage(CefRefPtr<CefBrowser> browser, const CefString& value) override {
    if (!shell_ && !popup_ && state_->embedded) state_->embedded->SetLinkStatus(browser, value);
  }
  void OnLoadStart(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, TransitionType) override {
    if (!shell_ && !popup_ && !foreign_ && !internal_page_ && frame->IsMain() && state_->workspace)
      state_->workspace->OnDocumentStarted(browser->GetIdentifier(), frame->GetURL());
  }
  void OnAddressChange(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, const CefString& url) override {
    if (!shell_ && !popup_ && frame->IsMain() && state_->embedded) state_->embedded->SetLinkStatus(browser, "");
    if (foreign_) { if (frame->IsMain()) ResolveForeign(browser, url.ToString()); return; }
    if (!shell_ && !popup_ && !internal_page_ && frame->IsMain()) red::ScheduleIntegrationLiveCheck(state_,browser);
    if (!shell_ && !popup_ && frame->IsMain() && state_->workspace) {
      state_->workspace->OnTitleChanged(browser);
      state_->workspace->OnPageChanged(browser);
    }
    if (state_->probe && !shell_ && !popup_ && frame->IsMain()) {
      auto data = CefDictionaryValue::Create(); data->SetString("url", url.ToString().substr(0, 200));
      data->SetBool("can_go_back", browser->CanGoBack()); state_->Record("address_change", data);
    }
  }
  void OnTitleChange(CefRefPtr<CefBrowser> browser, const CefString& title) override {
    // A website popup's own window shows its page title (taskbar previews, Alt+Tab).
    if (popup_host_) red::SetWebsitePopupTitle(popup_host_, title.ToWString());
    if (!shell_ && !popup_ && !foreign_ && state_->workspace) {
      state_->workspace->OnTitleChanged(browser);
      if (const auto previews = state_->workspace->integration_previews()) previews->ObserveMailTitle(browser, title.ToString());
    }
    const auto profile = red::ProfileForBrowser(state_,browser);
    const auto history = profile ? profile->history : state_->history;
    if (!shell_ && !popup_ && !foreign_ && history && !state_->private_window) {
      // Titles usually arrive before the load ends; remember it for the visit.
      title_url_ = browser->GetMainFrame()->GetURL().ToString(); title_ = title.ToString();
      history->UpdateTitle(title_url_, title_);
      // The page changed its address in place before setting the title (history/title_target.h).
      if (red::TitleBelongsToVisit(visit_url_, title_url_)) history->UpdateTitle(visit_url_, title_);
    }
  }
  void OnFaviconURLChange(CefRefPtr<CefBrowser> browser, const std::vector<CefString>& icon_urls) override {
    if (!shell_ && !popup_ && !foreign_ && state_->workspace) state_->workspace->OnFaviconChanged(browser, icon_urls);
  }
  void OnLoadingStateChange(CefRefPtr<CefBrowser> browser, bool loading, bool, bool) override {
    if (foreign_) { ResolveForeign(browser, MainUrl(browser)); return; }
    RestoreContentStack(state_);
    if (!shell_ && !popup_ && state_->workspace) {
      if (state_->embedded) state_->embedded->OnPresentationLoading(browser, loading);
      state_->workspace->OnPageChanged(browser);
    }
  }
  bool OnProcessMessageReceived(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
      CefProcessId source, CefRefPtr<CefProcessMessage> message) override {
    if (source == PID_RENDERER && message->GetName() == "red.audible") {
      // One boolean per website frame of a workspace page; anything else is dropped.
      const auto args = message->GetArgumentList();
      if (!shell_ && !popup_ && !foreign_ && !internal_page_ && state_->workspace && frame && args->GetSize() == 1 && args->GetType(0) == VTYPE_BOOL)
        red::SetFrameAudible(state_, browser->GetIdentifier(), frame->GetIdentifier().ToString(), args->GetBool(0));
      return true;
    }
    if (state_->probe && source == PID_RENDERER && frame->IsMain() &&
        message->GetName() == "red.probe.token") {
      auto received = message->GetArgumentList()->GetDictionary(0);
      auto token = received ? received->Copy(false) : nullptr;
      if (token) {
        const bool restricted = token->GetBool("token_query_ok") &&
            (token->GetBool("restricted") || token->GetBool("app_container")) &&
            token->GetInt("integrity_rid") <= SECURITY_MANDATORY_LOW_RID;
        if (shell_) state_->shell_sandbox = restricted;
        else if (!popup_) state_->content_sandbox = restricted;
        token->SetString("role", shell_ ? "shell" : popup_ ? "popup" : "content");
        state_->Record("renderer_token", token);
      }
      return true;
    }
    return router_->OnProcessMessageReceived(browser, frame, source, message);
  }
  void OnLoadEnd(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, int status) override {
    if (!shell_ && !popup_ && !foreign_ && !internal_page_) red::OnProtocolsProbeFrame(state_, browser, frame->GetURL().ToString(), frame->IsMain());
    if (!frame->IsMain()) return;
    if (page_load_trace_) CefPostDelayedTask(TID_UI, base::BindOnce([](std::weak_ptr<State> weak, CefRefPtr<CefBrowser> page) {
      if (auto state = weak.lock()) red::SamplePageLoad(state, page);
    }, std::weak_ptr<State>(state_), browser), 200);
    if (foreign_) { ResolveForeign(browser, frame->GetURL().ToString()); return; }
    // Red's UI never zooms (the UI blocks Ctrl+wheel); undo a zoom level saved
    // for its origin before that, which every Red UI surface shares. Only when
    // it differs: resetting on every load broke the shell's navigation requests.
    if (shell_) {
      const auto host = browser->GetHost();
      auto zoom = CefDictionaryValue::Create(); zoom->SetDouble("level", host->GetZoomLevel()); zoom->SetDouble("default", host->GetDefaultZoomLevel());
      zoom->SetString("url", frame->GetURL().ToString()); state_->Record("shell_zoom", zoom);
      if (std::abs(host->GetZoomLevel() - host->GetDefaultZoomLevel()) > 1e-6) host->SetZoomLevel(host->GetDefaultZoomLevel());
    }
    RestoreContentStack(state_);
    if (!shell_ && !popup_ && state_->workspace) state_->workspace->OnLoadFinished(browser);
    if (!shell_ && !popup_ && !foreign_ && !internal_page_) red::OnGoogleIntegrationsProbeLoad(state_,browser);
    if (!shell_ && !popup_ && !foreign_ && !internal_page_) red::ScheduleIntegrationLiveCheck(state_,browser);
    auto details = CefDictionaryValue::Create();
    details->SetInt("http_status", status);
    details->SetString("role", shell_ ? "shell" : popup_ ? "popup" : "content");
    details->SetInt("browser_id", browser->GetIdentifier());
    state_->Record("load_end", details);
    if (!shell_ && !popup_ && state_->tabs) state_->tabs->OnLoad(browser, status);
    if (!shell_ && !popup_ && !internal_page_) red::InstallVideoPipButton(state_, browser);
    if (!shell_ && !popup_ && !internal_page_) red::InstallBoosts(state_, browser);
    if (!shell_ && !popup_ && status >= 200 && status < 400) {
      if (state_->history && !foreign_) {
        const auto url = frame->GetURL().ToString();
        const auto profile = red::ProfileForBrowser(state_,browser);
        const auto history = profile ? profile->history : state_->history;
        if (!state_->private_window) { history->RecordVisit(url, url == title_url_ ? title_ : ""); visit_url_ = url; }
        red::OnPrivateProbeLoad(state_, url);
        red::OnHistoryProbeLoad(state_, browser, url);
        red::OnTabActionsProbeLoad(state_, url);
        red::OnAudibleProbeLoad(state_, browser, url);
        red::OnPipProbeLoad(state_, browser, url);
        red::OnCaptureMuteProbeLoad(state_, browser, url);
        red::OnThemeProbeLoad(state_, url);
        red::OnSpacesProbeLoad(state_, url);
        red::OnUaBrandProbeLoad(state_, browser, url);
        red::OnBoostProbeLoad(state_, browser, url);
        red::OnMenuProbeLoad(state_, url);
        red::OnPinProbeLoad(state_, url);
        red::OnLinkHeadersProbeLoad(state_, browser, url);
        red::OnDownloadsPageProbeLoad(state_, browser, url);
        red::OnExtensionPopupProbeLoad(state_, url);
        red::OnCloseMotionProbeLoad(state_, url);
        red::OnSidebarPeekProbeLoad(state_, url);
        red::OnSidebarPerfProbeLoad(state_, url);
      }
      state_->content_loaded = true;
      if (state_->content && state_->content->GetIdentifier() == browser->GetIdentifier()) {
        red::StartFullscreenProbe(state_, browser);
        red::StartPermissionsProbe(state_, browser);
        red::StartSiteProbe(state_, browser);
      }
      ++state_->content_loads;
      if (state_->probe_navigation && state_->content_loads == 1) {
        auto shell_browser = state_->browsers.find(state_->shell_id.load());
        if (shell_browser != state_->browsers.end())
          shell_browser->second->GetMainFrame()->ExecuteJavaScript(
              "document.querySelector('form').requestSubmit()", std::string(red::kUiUrl), 0);
      }
    }
    if (!shell_ && state_->probe) {
      // Fixed diagnostic code only; never evaluate an IPC-supplied script.
      frame->ExecuteJavaScript("console.info('RED_PROBE_BRIDGE:' + typeof window.cefQuery)", frame->GetURL(), 0);
    }
  }
  void OnLoadError(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame> frame, ErrorCode code,
                   const CefString&, const CefString&) override {
    if (!frame->IsMain() || code == ERR_ABORTED) return;
    auto details = CefDictionaryValue::Create();
    details->SetString("role", shell_ ? "shell" : "content");
    details->SetInt("code", code);
    state_->Record("load_error", details);
  }
  bool OnConsoleMessage(CefRefPtr<CefBrowser> browser, cef_log_severity_t severity,
      const CefString& message, const CefString&, int) override {
    if (state_->probe && state_->sidebar_peek_probe && shell_ && message.ToString().starts_with("RED_PEEK:")) {
      auto data = CefDictionaryValue::Create(); data->SetString("result", message.ToString().substr(9, 1000)); state_->Record("peek_document", data);
    }
    if(state_->probe&&state_->link_headers_minimize&&shell_&&message.ToString().starts_with("RED_SIDEBAR_RESTORE:")){
      const auto value=CefParseJSON(message.ToString().substr(20),JSON_PARSER_RFC);
      if(value&&value->GetType()==VTYPE_DICTIONARY)state_->Record("sidebar_restore_dom",value->GetDictionary()->Copy(false));
    }
    if(state_->theme_preview_probe&&shell_&&message.ToString().starts_with("RED_THEME_PREVIEW:")) {
      const auto value=CefParseJSON(message.ToString().substr(18),JSON_PARSER_RFC);
      const bool passed=value&&value->GetType()==VTYPE_DICTIONARY&&value->GetDictionary()->GetBool("passed");
      if(value&&value->GetType()==VTYPE_DICTIONARY)state_->Record("theme_preview_shell",value->GetDictionary()->Copy(false));
      state_->theme_preview_checks=passed;state_->theme_done=true;
    }
    if (state_->probe && shell_ && severity >= LOGSEVERITY_ERROR) {
      auto data = CefDictionaryValue::Create(); data->SetString("message", message.ToString().substr(0, 1024));
      state_->Record("probe_shell_error", data);
    }
    if (state_->probe && !shell_ && message.ToString().find("chrome-extension://") != std::string::npos) {
      // Diagnostic for extension resource loading; fixed-size excerpt only.
      auto data = CefDictionaryValue::Create(); data->SetString("message", message.ToString().substr(0, 400));
      data->SetInt("severity", severity); state_->Record("page_extension_console", data);
    }
    red::ObserveExtensionProbe(state_, browser, message.ToString());
    if (!shell_) red::ObserveExtensionActionProbe(state_, message.ToString());
    if (!shell_) red::ObserveUaBrandProbe(state_, message.ToString());
    if (!shell_) red::ObserveBoostProbe(state_, message.ToString());
    red::ObserveOrganizationProbe(state_, browser, message.ToString());
    red::ObserveBrowsingProbe(state_, browser, message.ToString());
    if (!shell_ && !popup_) red::ObserveFullscreenProbe(state_, browser, message.ToString());
    if (!shell_ && !popup_) red::ObservePermissionsProbe(state_, browser, message.ToString());
    if (!shell_ && !popup_) red::ObserveSiteProbe(state_, browser, message.ToString());
    red::ObserveHistoryProbe(state_, message.ToString());
    red::ObserveAudibleProbe(state_, message.ToString());
    if (!shell_) red::ObservePipProbe(state_, message.ToString());
    red::ObserveDownloadsPageProbe(state_, message.ToString());
    if (shell_) red::ObserveCloseMotionProbe(state_, message.ToString());
    if (shell_) red::ObserveSidebarPerfProbe(state_, message.ToString());
    red::ObserveLifecycleProbe(state_, browser, message.ToString());
    if (state_->lifecycle_probe && shell_ && message == "RED_LIFE:capture-crash")
      lifecycle_capture_ = red::CaptureShell(browser, state_->evidence_directory / "crash.png");
    if (state_->tabs) state_->tabs->OnConsole(browser, message.ToString());
    if (state_->probe && !shell_ && message == "RED_PROBE_BRIDGE:undefined") {
      if (!popup_) state_->content_bridge_absent = true;
      state_->Record(popup_ ? "popup_bridge_absent" : "content_bridge_absent");
    }
    if (state_->probe && shell_ && message == "RED_PROBE_IPC_REJECTIONS:pass") {
      state_->ipc_rejections = true;
      state_->Record("ipc_rejections_pass");
    }
    if (state_->downloads_probe && shell_ && message == "RED_DOWNLOADS:pass") { state_->downloads_checks = true; state_->Record("downloads_checks_pass"); }
    if (state_->workspace_probe && shell_ && message.ToString().starts_with("RED_SIDEBAR_RESIZE_COST:")) {
      auto data = CefDictionaryValue::Create(); data->SetString("result", message.ToString().substr(24, 2000));
      state_->Record("sidebar_resize_cost", data);
    }
    if (state_->workspace_probe && shell_ && message == "RED_WORKSPACE_CHECKS:pass") {
      state_->workspace_checks = true; state_->Record("workspace_checks_pass");
      red::SignalWorkspaceProbeReady(state_);
    }
    return false;
  }
 private:
  static LRESULT CALLBACK PageWindowMessage(HWND window, UINT message, WPARAM wparam, LPARAM lparam,
                                             UINT_PTR id, DWORD_PTR) {
    if (message == WM_NCDESTROY) RemoveWindowSubclass(window, PageWindowMessage, id);
    if (message == WM_WINDOWPOSCHANGING) {
      const auto& position = *reinterpret_cast<const WINDOWPOS*>(lparam);
      // Chrome's Activate() requests this exact z-order change. Video PiP uses
      // it to return to its opener. Sol's layout changes all use SWP_NOACTIVATE.
      if (position.hwndInsertAfter == HWND_TOP && position.flags == (SWP_NOMOVE | SWP_NOSIZE)) {
        const auto client = PageClients().find(static_cast<int>(id));
        if (client == PageClients().end() || !client->second->state_->workspace ||
            client->second->state_->workspace->NativePageActivationPending())
          return DefSubclassProc(window, message, wparam, lparam);
        const auto& content = client->second->state_->content;
        const bool was_selected = content && content->GetIdentifier() == static_cast<int>(id);
        const bool visible = IsWindowVisible(window);
        const bool enabled = IsWindowEnabled(window);
        CefPostTask(TID_UI, base::BindOnce([](int id, HWND window, bool was_selected, bool visible, bool enabled) {
          const auto client = PageClients().find(id);
          if (client == PageClients().end()) return;
          // Resolve the current owner after native activation unwinds. The
          // same page and client can move live to another Sol window.
          const auto state = client->second->state_;
          const auto page = state->browsers.find(id);
          if (!state->workspace || !state->window || !state->embedded || page == state->browsers.end() || !page->second->IsValid() ||
              page->second->GetHost()->GetWindowHandle() != window ||
              GetAncestor(window, GA_ROOT) != state->window->GetWindowHandle()) return;
          // A departing tab can stay visible as a backdrop. Chrome's focus
          // changes during that handoff must not select it again.
          const bool selected = state->content && state->content->GetIdentifier() == id;
          if (was_selected && !selected) return;
          if (visible && !selected && (!enabled || !state->embedded->IsSplitPage(id))) return;
          state->workspace->OnPageActivationRequested(id);
        }, static_cast<int>(id), window, was_selected, visible, enabled));
      }
    }
    return DefSubclassProc(window, message, wparam, lparam);
  }
  struct Queries {
    std::map<int64_t, CefRefPtr<CefMessageRouterBrowserSide::Callback>> pending;
    std::optional<int64_t> subscription;
  };
  void OnForeignCreated(CefRefPtr<CefBrowser> browser) {
    // Chrome put the page in a separate normal window. Keep it out of sight;
    // never reparent it. Its address is reopened as a workspace page instead.
    foreign_root_ = GetAncestor(browser->GetHost()->GetWindowHandle(), GA_ROOT);
    if (foreign_root_) {
      // Off-screen as well as hidden: if Chrome shows it again before our
      // re-hide runs, nothing appears on any monitor.
      state_->suppressed_windows.insert(foreign_root_);
      // Tool-window style: a later show creates no taskbar button either.
      SetWindowLongPtrW(foreign_root_, GWL_EXSTYLE, (GetWindowLongPtrW(foreign_root_, GWL_EXSTYLE) | WS_EX_TOOLWINDOW) & ~WS_EX_APPWINDOW);
      SetWindowPos(foreign_root_, nullptr, -32000, -32000, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
      ShowWindow(foreign_root_, SW_HIDE);
    }
    const bool content_profile = browser->GetHost()->GetRequestContext()->IsSharingWith(state_->WebContext()) ||
        red::ProfileForBrowser(state_,browser) != nullptr;
    auto details = CefDictionaryValue::Create();
    details->SetInt("browser_id", browser->GetIdentifier()); details->SetBool("content_profile", content_profile);
    details->SetString("root", std::to_string(reinterpret_cast<uintptr_t>(foreign_root_)));
    state_->Record("foreign_browser_created", details);
    if (!content_profile) { foreign_profile_rejected_ = true; ResolveForeign(browser, "", true); return; }
    ResolveForeign(browser, MainUrl(browser));
    CefPostDelayedTask(TID_UI, base::BindOnce([](CefRefPtr<Client> self, CefRefPtr<CefBrowser> browser) {
      // The browser may already be closed; then it was resolved before.
      if (!self->foreign_resolved_) self->ResolveForeign(browser, MainUrl(browser), true);
    }, CefRefPtr<Client>(this), browser), 5000);
  }
  // GetMainFrame() is null once a browser is closing.
  static std::string MainUrl(CefRefPtr<CefBrowser> browser) {
    const auto frame = browser->GetMainFrame();
    return frame ? frame->GetURL().ToString() : "";
  }
  // Decides once per foreign browser, then closes it. |final| turns a still
  // blank page into a visible rejection instead of waiting forever.
  void ResolveForeign(CefRefPtr<CefBrowser> browser, const std::string& url, bool final = false) {
    if (foreign_resolved_) return;
    auto kind = foreign_profile_rejected_ ? red::ForeignPage::Reject : red::ClassifyForeignPage(url);
    if (kind == red::ForeignPage::Wait) { if (!final) return; kind = red::ForeignPage::Reject; }
    foreign_resolved_ = true;
    CefPostTask(TID_UI, base::BindOnce([](std::shared_ptr<State> state, CefRefPtr<CefBrowser> browser,
                                          red::ForeignPage kind, std::string url, bool wrong_profile) {
      std::string outcome = "rejected";
      if (state->workspace && !wrong_profile) {
        if (kind == red::ForeignPage::NewTab) {
          // After a store install Chrome opens a new tab page only to host its
          // "added" bubble; our own install notice replaces it. The preference
          // update can trail the window, so decide shortly afterwards.
          outcome = "new_tab";
          CefPostDelayedTask(TID_UI, base::BindOnce([](std::weak_ptr<State> weak) {
            auto state = weak.lock();
            if (!state || !state->workspace) return;
            const bool after_install = state->last_extension_install_ms && GetTickCount64() - state->last_extension_install_ms < 15000;
            state->Record(after_install ? "install_new_tab_dropped" : "foreign_new_tab_opened");
            if (!after_install) state->workspace->OpenNewTab();
          }, std::weak_ptr<State>(state)), 1500);
        }
        else if (kind == red::ForeignPage::Passwords) {
          // Chrome's save-bubble footer navigates directly, bypassing Chrome
          // command callbacks. Reopen only the manager in the actual source
          // profile; never adopt chrome:// into a workspace or fall back to
          // whichever Space happens to be active now.
          if(const auto profile=red::ProfileForBrowser(state,browser))
            if(red::ShowPasswords(state,profile->id,url.starts_with("chrome://password-manager/settings")))outcome="password_manager";
        }
        else if (kind == red::ForeignPage::Adopt) {
          const auto profile = red::ProfileForBrowser(state,browser);
          if (state->workspace->AdoptExtensionPage(url,profile ? profile->id : state->active_profile)) outcome = "adopted";
        }
      }
      // A page from the Shell profile is closed silently: the user cannot act
      // on it, and the Shell profile guard handles the underlying violation.
      if (state->workspace && outcome == "rejected" && !wrong_profile)
        state->workspace->Notify(kind == red::ForeignPage::Passwords ? "Could not open the password manager for this profile." :
            kind == red::ForeignPage::Adopt ? "An extension tried to open a page while tabs were busy. Try again." :
            "An extension tried to open a page Sol does not support, so it was blocked.");
      auto details = CefDictionaryValue::Create();
      details->SetInt("browser_id", browser->GetIdentifier()); details->SetString("outcome", outcome);
      CefURLParts parts; details->SetString("scheme", CefParseURL(url, parts) ? CefString(&parts.scheme).ToString() : "");
      if (state->probe) details->SetString("url", url.substr(0, 1024));
      state->Record("foreign_browser_resolved", details);
      if (auto host = browser->GetHost()) host->CloseBrowser(true);
    }, state_, browser, kind, url, foreign_profile_rejected_));
  }
  red::WorkspaceController::Reply AsyncReply(int64_t id) {
    std::weak_ptr<Queries> weak = queries_;
    return [weak, id](int code, const std::string& error, CefRefPtr<CefDictionaryValue> data) {
      if (auto queries = weak.lock()) {
        auto pending = queries->pending.find(id);
        if (pending == queries->pending.end()) return;
        if (code) pending->second->Failure(code, error);
        else { auto value = CefValue::Create(); value->SetDictionary(data); pending->second->Success(CefWriteJSON(value, JSON_WRITER_DEFAULT)); }
        queries->pending.erase(pending);
      }
    };
  }
  std::shared_ptr<Queries> queries_ = std::make_shared<Queries>();
  std::string node_id_;
  std::shared_ptr<State> state_;
  bool shell_;
  bool popup_;
  bool foreign_;
  bool internal_page_ = false;
  bool foreign_resolved_ = false, foreign_profile_rejected_ = false;
  HWND foreign_root_ = nullptr;
  std::string title_, title_url_;  // Latest page title and the URL it belongs to.
  std::string visit_url_;  // URL of the last visit recorded for this tab.
  std::pair<int, int> reservation_;
  HWND popup_host_ = nullptr;  // A website popup's own window (windows/website_popup_host.h).
  std::string named_tab_url_;
  bool named_tab_foreground_ = false;
  bool popup_created_ = false;
  CefRefPtr<CefMessageRouterBrowserSide> router_;
  CefRefPtr<CefRegistration> lifecycle_capture_;
  CefRefPtr<CefRegistration> page_load_trace_;
  IMPLEMENT_REFCOUNTING(Client);
};

class ContentBoundsDelegate final : public CefPanelDelegate {
 public:
  explicit ContentBoundsDelegate(std::weak_ptr<State> state) : state_(std::move(state)) {}
  void OnLayoutChanged(CefRefPtr<CefView>, const CefRect& bounds) override {
    if (auto state = state_.lock(); state && state->embedded) state->embedded->Resize(bounds);
  }
 private:
  std::weak_ptr<State> state_;
  IMPLEMENT_REFCOUNTING(ContentBoundsDelegate);
};

class WindowDelegate final : public CefWindowDelegate {
 public:
  WindowDelegate(std::shared_ptr<State> state, CefRefPtr<CefBrowserView> shell,
                 CefRefPtr<CefBrowserView> content) : state_(state), shell_(shell), content_(content) {
    // Ordinary workspace sessions remember the window; probes keep fixed sizes.
    if (state_->workspace_mode && !state_->probe && !state_->secondary_window) {
      placement_file_ = state_->profile / "BrowserData" / "window.json";
      placement_ = red::LoadWindowPlacement(placement_file_);
    }
  }
  CefRect GetInitialBounds(CefRefPtr<CefWindow>) override {
    if (!state_->initial_window_bounds.IsEmpty()) return state_->initial_window_bounds;
    // Test runs only: RED_PROBE_DISPLAY=secondary opens the window on a
    // non-primary monitor, out of the way of whoever uses the main one.
    if (state_->probe) {
      wchar_t display[16] = {};
      if (GetEnvironmentVariableW(L"RED_PROBE_DISPLAY", display, 16) && std::wstring(display) == L"secondary") {
        std::vector<CefRefPtr<CefDisplay>> displays; CefDisplay::GetAllDisplays(displays);
        const auto primary = CefDisplay::GetPrimaryDisplay();
        for (const auto& candidate : displays) if (candidate->GetID() != primary->GetID()) {
          const auto area = candidate->GetWorkArea();
          return CefRect(area.x + 40, area.y + 40, std::min(1280, area.width - 80), std::min(860, area.height - 80));
        }
      }
    }
    const auto centered = [this] {
      if (!state_->workspace_mode || state_->probe) return CefRect();
      POINT cursor{};
      const auto display = GetCursorPos(&cursor) ? CefDisplay::GetDisplayNearestPoint(CefPoint(cursor.x, cursor.y), true) : CefDisplay::GetPrimaryDisplay();
      if (!display) return CefRect();
      last_normal_ = red::CenteredWindowBounds(display->GetWorkArea(), CefSize(1200, 820));
      return last_normal_;
    };
    if (!placement_) return centered();
    // Only if a connected display still shows a usable part of it.
    const auto display = CefDisplay::GetDisplayMatchingBounds(placement_->bounds, false);
    const auto area = display ? display->GetWorkArea() : CefRect();
    const int visible_w = std::min(placement_->bounds.x + placement_->bounds.width, area.x + area.width) - std::max(placement_->bounds.x, area.x);
    const int visible_h = std::min(placement_->bounds.y + placement_->bounds.height, area.y + area.height) - std::max(placement_->bounds.y, area.y);
    if (visible_w < 200 || visible_h < 120) { placement_.reset(); return centered(); }
    last_normal_ = placement_->bounds;
    return placement_->bounds;
  }
  cef_show_state_t GetInitialShowState(CefRefPtr<CefWindow>) override {
    return placement_ && placement_->maximized ? CEF_SHOW_STATE_MAXIMIZED : CEF_SHOW_STATE_NORMAL;
  }
  // The workspace draws its own window controls and drag areas (Arc-style).
  bool IsFrameless(CefRefPtr<CefWindow>) override { return state_->workspace_mode; }
#if defined(RED_CEF_TRANSLUCENT)
  // Acrylic: the window composites with alpha so the system backdrop shows behind the UI.
  bool IsTranslucent(CefRefPtr<CefWindow>) override { return state_->translucent; }
#endif
  // CEF resets view backgrounds to the native theme's (dark: #1F1F1F) on theme
  // changes, which would cover the system backdrop: keep the window's own.
  void OnThemeChanged(CefRefPtr<CefView> view) override {
    if (state_->translucent) view->SetBackgroundColor(red::WorkspaceController::WindowBackground(state_));
  }
  cef_runtime_style_t GetWindowRuntimeStyle() override {
    return state_->native_tabs_reference || state_->embedded_tabs_probe || state_->workspace_mode ? CEF_RUNTIME_STYLE_ALLOY : CEF_RUNTIME_STYLE_CHROME;
  }
  CefSize GetPreferredSize(CefRefPtr<CefView>) override {
    return state_->native_tabs_reference ? CefSize(1200, 160) : CefSize(1200, 820);
  }
  void OnWindowCreated(CefRefPtr<CefWindow> window) override {
    state_->window = window;
    background_root_ = window->GetWindowHandle();
    window->SetTitle("Sol Browser — architecture probe");
    if (state_->workspace_mode) {
      state_->window_close = std::make_shared<red::WindowClose>(state_);
      state_->command_bar = std::make_shared<red::CommandBar>(state_);
      state_->panel_popup = std::make_shared<red::PanelPopup>(state_);
      state_->extension_popup = std::make_shared<red::ExtensionPopup>(state_);
      state_->sidebar_peek = std::make_shared<red::SidebarPeek>(state_);
      if (!state_->extensions) state_->extensions = std::make_shared<red::ExtensionCatalog>(state_->profile / "Default");
      state_->sidebar_menu = std::make_shared<red::SidebarMenu>(state_);
      window->SetTitle("Sol");
      const auto icon = red::RedWindowIcon();
      window->SetWindowIcon(icon); window->SetWindowAppIcon(icon);
      // Start in the last theme (window, caption, page corners and covers) so
      // nothing flashes light before the workspace database has loaded.
      if (auto cached = red::ReadThemeCache(state_->profile / "BrowserData" / "theme-cache"); cached && state_->workspace_mode && !state_->secondary_window) state_->theme = *cached;
      red::WorkspaceController::ApplyTheme(state_);
      // Frameless windows keep Windows 11 rounded corners; ignored on Windows 10.
      red::ApplyWindowCorners(window);
      std::weak_ptr<State> weak = state_;
      state_->embedded = std::make_shared<red::EmbeddedContent>(weak, [weak](const std::string& id, bool history) -> CefRefPtr<CefClient> {
        auto state = weak.lock(); return state ? new Client(state, false, id, false, {-1, -1}, false, history) : nullptr;
      });
      window->SetToFillLayout(); window->AddChildView(shell_);
      // A first run from an OS link opens the requested page immediately.
      if (state_->fresh_profile && state_->external_urls.empty() && !state_->secondary_window &&
          !state_->private_window && !state_->standalone_window) red::ShowWelcome(state_);
      else window->Show();
      red::RefreshShortcuts(state_);
      ResizeWorkspace(window);
      state_->workspace->AttachContent();
      state_->Record("window_shown");
      if (state_->probe && !state_->input_probe && state_->lifecycle_mode != "hold") CefPostDelayedTask(TID_UI, base::BindOnce(&WindowDelegate::FinishProbe, CefRefPtr<WindowDelegate>(this)), state_->sidebar_perf_probe ? 150000 : state_->google_integrations_probe || state_->organization_probe || state_->browsing_probe || state_->lifecycle_probe || state_->downloads_probe || state_->fullscreen_probe || state_->site_probe || !state_->history_probe.empty() || state_->tab_actions_probe || state_->audible_probe || state_->downloads_page_probe || state_->extension_popup_probe || !state_->protocols_probe.empty() || state_->close_motion_probe || state_->sidebar_peek_probe || state_->sidebar_perf_probe || state_->pip_probe || state_->capture_mute_probe || state_->theme_probe || state_->spaces_probe || !state_->ua_brand_probe.empty() || state_->boost_probe || state_->menu_probe || state_->private_probe || state_->pin_probe || !state_->import_probe.empty() || state_->link_headers_tabmenu || state_->link_headers_inspect || state_->link_headers_transfer || state_->link_headers_cycles || state_->passwords_probe ? 30000 : 15000);
      if (state_->input_probe) red::ObserveNativeInput(state_);
      if (!state_->import_probe.empty()) CefPostDelayedTask(TID_UI, base::BindOnce([](std::weak_ptr<State> weak) {
        if (auto s = weak.lock()) red::RunImportProbe(s);
      }, std::weak_ptr<State>(state_)), 4000);
      if (state_->workspace_probe) CefPostDelayedTask(TID_UI, base::BindOnce(&WindowDelegate::CapturePreview, CefRefPtr<WindowDelegate>(this)), state_->organization_probe ? 25000 : 11000);
      return;
    }
    CefBoxLayoutSettings settings;
    settings.horizontal = false;
    auto layout = window->SetToBoxLayout(settings);
    if (state_->embedded_tabs_probe) {
      std::weak_ptr<State> weak = state_;
      state_->embedded = std::make_shared<red::EmbeddedContent>(weak, [weak](const std::string&, bool) -> CefRefPtr<CefClient> {
        auto state = weak.lock();
        return state ? new Client(state, false) : nullptr;
      });
      content_panel_ = CefPanel::CreatePanel(new ContentBoundsDelegate(state_));
      window->AddChildView(content_panel_);
      layout->SetFlexForView(content_panel_, 1);
    }
    // The first associated profile initializes the Chrome widget's theme.
    // Attach Chrome content before Alloy shell, then order shell above content.
    if (content_) window->AddChildView(content_);
    window->AddChildViewAt(shell_, 0);
    if (content_) layout->SetFlexForView(content_, 1);
    window->Show();
    if (state_->embedded) {
      window->Layout();
      state_->embedded->Resize(content_panel_->GetBounds());
      if (!state_->embedded->Create(state_->initial_url)) state_->exit_code = 2;
    }
    if (content_) content_->RequestFocus();
    state_->Record("window_shown");
    if (state_->toolbar_probe) {
      const auto& url = state_->initial_url;
      state_->toolbar_probe->Start(window, content_, url + (url.find('?') == std::string::npos ? "?second" : "&second"));
      return;  // The experiment closes the window itself.
    }
    if (state_->probe) {
      CefPostDelayedTask(TID_UI, base::BindOnce(&WindowDelegate::FinishProbe, CefRefPtr<WindowDelegate>(this)), 15000);
    }
  }
  void FinishProbe() {
    if (!state_->window) return;
    // Interactive palette probes may wait for the external test driver to focus
    // their isolated window. Keep the normal foreground checks intact.
    if(state_->palette_probe&&!state_->palette_checks&&state_->theme_phase<5){
      ++state_->theme_phase;CefPostDelayedTask(TID_UI,base::BindOnce(&WindowDelegate::FinishProbe,CefRefPtr<WindowDelegate>(this)),15000);return;
    }
    const auto shell = shell_->GetBounds();
    const auto content = state_->workspace_mode ? WorkspaceBounds(state_->window) :
        content_ ? content_->GetBounds() : content_panel_ ? content_panel_->GetBounds() : CefRect();
    auto details = CefDictionaryValue::Create();
    details->SetInt("shell_height", shell.height);
    details->SetInt("content_height", content.height);
    details->SetInt("content_y", content.y);
    details->SetInt("shell_style", shell_->GetRuntimeStyle());
    details->SetInt("content_style", content_ ? content_->GetRuntimeStyle() :
        state_->content ? state_->content->GetHost()->GetRuntimeStyle() : CEF_RUNTIME_STYLE_DEFAULT);
    details->SetBool("native_reference_separate_shell", state_->native_tabs_reference);
    details->SetBool("embedded_child_windows", state_->embedded_tabs_probe);
    const bool input_area_ok = !state_->embedded ||
        (state_->embedded->ActiveOwnsInputArea() && state_->embedded->ActiveMatchesBounds());
    details->SetBool("native_input_area", input_area_ok);
    state_->layout_ok = state_->workspace_mode ? state_->embedded->AllContained() && state_->embedded->VisiblePagesMatchPresentation() && content.x == (state_->sidebar_visible ? red::SidebarWidth(state_->sidebar_width, state_->window->GetClientAreaBoundsInScreen().width) : 0) + red::kContentInset && content.y == red::kToolbarHeight && content.width > 0 :
        state_->embedded ? state_->embedded->AllContained() &&
        shell.height > 0 && content.height > 0 && content.y >= shell.y + shell.height :
        state_->native_tabs_reference ? shell.height > 0 && state_->content &&
        state_->content->GetHost()->GetRuntimeStyle() == CEF_RUNTIME_STYLE_CHROME :
        shell.height > 0 && content.height > 0 &&
        content.y >= shell.y + shell.height &&
        shell_->GetRuntimeStyle() == CEF_RUNTIME_STYLE_ALLOY &&
        content_->GetRuntimeStyle() == CEF_RUNTIME_STYLE_CHROME;
    state_->Record("layout", details);
    const bool corners_ok = !state_->workspace_mode || red::WindowCornersMatch(state_->window);
    auto corner_details = CefDictionaryValue::Create(); corner_details->SetBool("pass", corners_ok);
    corner_details->SetString("method", "system_dwm"); state_->Record("window_corners", corner_details);
    const bool pass = corners_ok && input_area_ok && state_->layout_ok && state_->shell_ready && state_->content_loaded &&
        state_->content_bridge_absent && state_->shell_sandbox && state_->content_sandbox && state_->ipc_rejections &&
        (!state_->tabs || state_->tabs->passed()) &&
        (!state_->workspace_probe || state_->workspace_checks) &&
        (!state_->organization_probe || state_->organization_checks) &&
        (!state_->browsing_probe || state_->browsing_checks) &&
        (!state_->lifecycle_probe || state_->lifecycle_checks) &&
        (!state_->downloads_probe || state_->downloads_checks) &&
        (!state_->fullscreen_probe || state_->fullscreen_checks) &&
        (state_->permissions_probe.empty() || state_->permissions_observed) &&
        (!state_->site_probe || state_->site_checks) &&
        (state_->history_probe.empty() || state_->history_checks) &&
        (!state_->tab_actions_probe || state_->tab_actions_checks) &&
        (!state_->audible_probe || state_->audible_checks) &&
        (!state_->downloads_page_probe || state_->downloads_page_checks) &&
        (!state_->extension_popup_probe || state_->extension_popup_done) &&
        (state_->protocols_probe.empty() || state_->protocols_done) &&
        (!state_->close_motion_probe || state_->close_motion_done) &&
        (!state_->sidebar_peek_probe || state_->sidebar_peek_done) &&
        (!state_->google_integrations_probe || state_->google_integrations_checks) &&
        (!state_->sidebar_perf_probe || state_->sidebar_perf_done) &&
        (!state_->pip_probe || state_->pip_done) &&
        (!state_->capture_mute_probe || state_->capture_mute_done) &&
        (!state_->theme_probe || state_->theme_done) &&
        (!state_->theme_preview_probe || state_->theme_preview_checks) &&
        (!state_->palette_probe || state_->palette_checks) &&
        (!state_->spaces_probe || state_->spaces_done) &&
        (state_->ua_brand_probe.empty() || state_->ua_brand_done) &&
        (!state_->boost_probe || state_->boost_done) && (!state_->menu_probe || state_->menu_done) &&
        (!state_->private_probe || state_->private_done) && (!state_->pin_probe || state_->pin_done) &&
        (!state_->link_headers_probe || state_->link_headers_done) &&
        (state_->import_probe.empty() || state_->import_done) &&
        (!state_->probe_navigation || (state_->navigation_command && state_->content_loads >= 2));
    if (!pass) state_->exit_code = 2;
    auto result = CefDictionaryValue::Create();
    result->SetBool("pass", pass);
    result->SetBool("shell_ready", state_->shell_ready);
    result->SetBool("content_loaded", state_->content_loaded);
    result->SetBool("content_bridge_absent", state_->content_bridge_absent);
    result->SetBool("shell_sandbox", state_->shell_sandbox);
    result->SetBool("content_sandbox", state_->content_sandbox);
    result->SetBool("layout_ok", state_->layout_ok);
    result->SetBool("ipc_rejections", state_->ipc_rejections);
    result->SetBool("native_tabs_reference", state_->native_tabs_reference);
    result->SetBool("embedded_tabs_probe", state_->embedded_tabs_probe);
    if (state_->workspace_probe) result->SetBool("workspace", state_->workspace_checks);
    if (state_->organization_probe) result->SetBool("organization", state_->organization_checks);
    if (state_->tabs) {
      state_->tabs->Summarize();
      result->SetBool("tabs", state_->tabs->passed());
    }
    if (state_->probe_navigation) result->SetBool("react_form_navigation",
        state_->navigation_command && state_->content_loads >= 2);
    state_->Record("probe_result", result);
    state_->window->Close();
  }
  void CapturePreview() {
    if (shell_ && shell_->GetBrowser()) capture_ = red::CaptureShell(shell_->GetBrowser(), state_->evidence_directory / "shell.png");
  }
  bool CanClose(CefRefPtr<CefWindow>) override {
    if (state_->window_close) return state_->window_close->CanClose();
    bool ready = true;
    // Check each browser even if one isn't ready; never force-close renderers.
    std::vector<CefRefPtr<CefBrowser>> browsers;
    for (const auto& [id, browser] : state_->browsers) browsers.push_back(browser);
    for (const auto& browser : browsers)
      if (!browser->GetHost()->TryCloseBrowser()) ready = false;
    return ready;
  }
  CefSize GetMinimumSize(CefRefPtr<CefView>) override { return CefSize(700, 420); }
  void OnWindowBoundsChanged(CefRefPtr<CefWindow> window, const CefRect&) override {
    // Minimized, the client area is empty: laying out would shrink the pages
    // (to 14x14), and restoring would make them repaint from scratch, showing
    // a gray flash (e.g. a playing video). Keep everything as it is.
    if (window->IsMinimized()) {
      // Background title/media updates may publish a sidebar width clamped to
      // the empty minimized client. Restore must republish real geometry even
      // when its dimensions match the last visible window exactly.
      client_geometry_.reset();
      return;
    }
    LARGE_INTEGER started{};
    const bool trace = state_->tab_trace.is_open();
    if (trace) QueryPerformanceCounter(&started);
    const bool changed = UpdateClientGeometry(window);
    RememberPlacement(window);
    // Owned popups use screen coordinates; native child pages move with their
    // parent. Moving the root must not resize pages or publish a workspace.
    if (state_->command_bar && state_->command_bar->is_open()) state_->command_bar->Reposition();
    if (state_->panel_popup && state_->panel_popup->is_open()) state_->panel_popup->Reposition();
    if (state_->embedded) state_->embedded->NotifyWindowMoved();
    int layouts = 0;
    if (changed) {
      if (state_->workspace_mode) red::ApplyWindowCorners(window);
      if (state_->workspace_mode && state_->embedded) {
        // SetSidebarVisible already performs the content layout and publishes
        // the new client size. Do not also call ResizeWorkspace here.
        state_->workspace->SetSidebarVisible(state_->sidebar_visible);
        layouts = 1;
      }
      RestoreContentStack(state_);
    }
    TraceGeometry(started, changed, layouts);
  }
  void OnWindowActivationChanged(CefRefPtr<CefWindow>, bool active) override {
    if (active && state_->session) state_->session->active_window = state_;
    if (active && state_->workspace) CefPostTask(TID_UI, base::BindOnce([](std::weak_ptr<State> weak) {
      const auto state = weak.lock();
      if (!state || !state->window || GetForegroundWindow() != state->window->GetWindowHandle() || state->workspace->WindowClosing()) return;
      CefRefPtr<CefBrowser> target;
      for (const auto& [id, browser] : state->browsers) {
        if (id != state->shell_id && red::BrowserIsForeground(browser) && red::BrowserOwnsKeyboard(state, browser)) {
          target = browser;
          if (id != state->peek_id) state->ui_editing = false;
          state->keyboard_target = id; break;
        }
      }
      if (!target) {
        const int id = state->ui_editing ? state->shell_id.load() : state->keyboard_target;
        const auto remembered = state->browsers.find(id);
        if (remembered != state->browsers.end()) target = remembered->second;
        else target = state->content;
      }
      if (target) red::FocusBrowser(target); else red::FocusShell(state);
    }, std::weak_ptr<State>(state_)));
    RestoreContentStack(state_);
  }
  void OnWindowFullscreenTransition(CefRefPtr<CefWindow> window, bool) override {
    client_geometry_.reset();
    if (state_->workspace_mode) red::ApplyWindowCorners(window);
  }
  bool OnAccelerator(CefRefPtr<CefWindow>, int id) override {
    return red::ExecuteAccelerator(state_, id);
  }
  void OnWindowDestroyed(CefRefPtr<CefWindow>) override {
    placement_closed_ = true;
    if (!placement_file_.empty() && !last_normal_.IsEmpty()) red::SaveWindowPlacement(placement_file_, {last_normal_, last_maximized_});
    state_->window = nullptr;
    shell_ = nullptr;
    content_ = nullptr;
    content_panel_ = nullptr;
    state_->embedded = nullptr;
    capture_ = nullptr;
    state_->Record("window_destroyed");
    red::ForgetWorkspaceBackgroundTheme(background_root_);
    if (state_->browsers.empty()) red::QuitIfLastWindow(state_);
  }
 private:
  // Screen x/y does not change child geometry. Include both CEF DIP geometry
  // and native pixels/DPI, so crossing monitors or restoring still relayouts.
  struct ClientGeometry {
    int width, height, physical_width, physical_height;
    UINT dpi;
    int64_t display;
    float scale;
    bool minimized, maximized, fullscreen;
    bool operator==(const ClientGeometry&) const = default;
  };
  HWND background_root_ = nullptr;
  bool UpdateClientGeometry(CefRefPtr<CefWindow> window) {
    const auto client = window->GetClientAreaBoundsInScreen();
    const auto display = window->GetDisplay();
    RECT pixels{};
    const auto handle = window->GetWindowHandle();
    if (!GetClientRect(handle, &pixels) || !display) {
      client_geometry_.reset(); return true;
    }
    const ClientGeometry next{client.width, client.height, pixels.right - pixels.left,
        pixels.bottom - pixels.top, GetDpiForWindow(handle), display->GetID(),
        display->GetDeviceScaleFactor(), window->IsMinimized(), window->IsMaximized(), window->IsFullscreen()};
    const bool changed = !client_geometry_ || *client_geometry_ != next;
    client_geometry_ = next;
    return changed;
  }
  void TraceGeometry(LARGE_INTEGER started, bool changed, int layouts) {
    if (!state_->tab_trace.is_open()) return;
    LARGE_INTEGER ended{}, frequency{};
    QueryPerformanceCounter(&ended); QueryPerformanceFrequency(&frequency);
    auto details = CefDictionaryValue::Create();
    details->SetBool("client_changed", changed);
    details->SetInt("layout_passes", layouts);
    details->SetDouble("callback_ms", static_cast<double>(ended.QuadPart - started.QuadPart) * 1000.0 / frequency.QuadPart);
    if (client_geometry_) {
      details->SetInt("client_width", client_geometry_->width);
      details->SetInt("client_height", client_geometry_->height);
      details->SetInt("dpi", client_geometry_->dpi);
      details->SetBool("maximized", client_geometry_->maximized);
      details->SetBool("fullscreen", client_geometry_->fullscreen);
    }
    state_->Record("window_geometry", details);
  }
  CefRect WorkspaceBounds(CefRefPtr<CefWindow> window) {
    const auto bounds = window->GetClientAreaBoundsInScreen();
    const auto area = red::WorkspaceContentArea(bounds.width, bounds.height, state_->sidebar_visible, state_->sidebar_width);
    return CefRect(area.x, area.y, area.width, area.height);
  }
  void ResizeWorkspace(CefRefPtr<CefWindow> window) { state_->embedded->Resize(WorkspaceBounds(window)); }
  // Normal (restored) bounds and maximized state; fullscreen and minimized
  // states are not remembered. Saved shortly after changes settle.
  void RememberPlacement(CefRefPtr<CefWindow> window) {
    if (placement_file_.empty() || window->IsMinimized() || window->IsFullscreen()) return;
    const bool maximized = window->IsMaximized();
    const auto normal = maximized ? last_normal_ : window->GetBounds();
    if (maximized == last_maximized_ && normal == last_normal_) return;
    last_maximized_ = maximized;
    last_normal_ = normal;
    last_placement_change_ = GetTickCount64();
    if (save_pending_ || last_normal_.IsEmpty()) return;
    save_pending_ = true;
    SchedulePlacementSave(600);
  }
  void SchedulePlacementSave(int64_t delay) {
    CefPostDelayedTask(TID_UI, base::BindOnce(&WindowDelegate::SaveSettledPlacement,
        CefRefPtr<WindowDelegate>(this)), delay);
  }
  void SaveSettledPlacement() {
    if (placement_closed_) { save_pending_ = false; return; }
    const auto elapsed = GetTickCount64() - last_placement_change_;
    // One pending timer follows the last actual change, rather than writing
    // every 600ms during a long native move/resize loop.
    if (elapsed < 600) { SchedulePlacementSave(static_cast<int64_t>(600 - elapsed)); return; }
    save_pending_ = false;
    red::SaveWindowPlacement(placement_file_, {last_normal_, last_maximized_});
  }
  std::filesystem::path placement_file_;
  std::optional<red::WindowPlacement> placement_;
  std::optional<ClientGeometry> client_geometry_;
  CefRect last_normal_;
  unsigned long long last_placement_change_ = 0;
  bool last_maximized_ = false, save_pending_ = false, placement_closed_ = false;
  std::shared_ptr<State> state_;
  CefRefPtr<CefBrowserView> shell_, content_;
  CefRefPtr<CefPanel> content_panel_;
  CefRefPtr<CefRegistration> capture_;
  IMPLEMENT_REFCOUNTING(WindowDelegate);
};

// Live tab transfer: the same CefBrowser (DOM, scroll, form input, history,
// playing media) moves into another window instead of reloading there. Its
// child HWND is reparented while hidden, the page's client is retargeted so
// every callback serves the new window, and per-page window state follows it.
// Measured feasibility: docs/multi-window.md (window_transfer experiment).
bool TransferPage(const std::shared_ptr<State>& from, const std::shared_ptr<State>& to, const std::string& source_id,
                  const std::string& node_id, bool remove_entry) {
  if (from == to || !from->workspace || !from->embedded || !to->workspace || !to->embedded || !to->window ||
      to->workspace->WindowClosing() || !from->workspace->CanTransfer(source_id)) return false;
  // A page lives in its window's web profile: never between a private window and any other window.
  if (from->private_window || to->private_window) return false;
  const auto browser = from->workspace->LiveBrowser(source_id);
  if (!browser || !PageClients().contains(browser->GetIdentifier())) return false;
  const auto destination_profile = to->workspace->ProfileForNode(node_id);
  if (destination_profile && !destination_profile->context->IsSharingWith(browser->GetHost()->GetRequestContext())) return false;
  // Its Link Peek (open or parked) goes along; parked, it reopens over the
  // tab in the new window. One that cannot move now keeps the old reload path.
  std::optional<red::WorkspaceController::MovingPeek> peek;
  if (!from->workspace->ReleasePeekForTransfer(source_id, peek)) return false;
  if (peek && !PageClients().contains(peek->browser->GetIdentifier())) { from->workspace->AdoptPeek(source_id, *peek); return false; }
  const int id = browser->GetIdentifier();
  red::ClosePageDevTools(from, id);  // A docked frontend belongs to the source window.
  if (!from->workspace->ReleaseForTransfer(source_id, remove_entry)) return false;
  // Hands one page's window-side state and child HWND to |to| (hidden).
  auto hand_over = [&](CefRefPtr<CefBrowser> page, const std::string& node) {
    const int page_id = page->GetIdentifier();
    from->embedded->OnClosed(page_id);
    if (from->tabs) from->tabs->OnClosed(page_id);
    auto move = [&](auto& from_map, auto& to_map) { if (auto at = from_map.find(page_id); at != from_map.end()) { to_map[page_id] = std::move(at->second); from_map.erase(at); } };
    move(from->audible_frames, to->audible_frames); move(from->media_access, to->media_access); move(from->pip_buttons, to->pip_buttons);
    move(from->browser_profiles, to->browser_profiles);
    if (from->auto_pip_pages.erase(page_id)) to->auto_pip_pages.insert(page_id);
    std::erase_if(from->popup_pending, [&](const auto& key) { return key.first == page_id; });
    from->browsers.erase(page_id);
    if (from->content && from->content->IsSame(page)) from->content = nullptr;
    const HWND handle = page->GetHost()->GetWindowHandle();
    ShowWindow(handle, SW_HIDE);
    SetParent(handle, to->window->GetWindowHandle());
    PageClients()[page_id]->Retarget(to, node);
    to->browsers[page_id] = page;
    to->embedded->OnCreated(page);
    if (to->tabs) to->tabs->OnCreated(page);
    page->GetHost()->NotifyScreenInfoChanged();
    page->GetHost()->WasResized();
  };
  hand_over(browser, node_id);
  if (peek) {
    hand_over(peek->browser, peek->id);
    // The preview returns to its link's place, now in the destination window.
    RECT a{}, b{};
    if (from->window && GetWindowRect(from->window->GetWindowHandle(), &a) && GetWindowRect(to->window->GetWindowHandle(), &b)) {
      peek->origin_x += b.left - a.left; peek->origin_y += b.top - a.top;
    }
    to->workspace->AdoptPeek(node_id, *peek);
  }
  if (!to->workspace->AdoptPage(browser, node_id)) {
    // The entry vanished meanwhile: nothing can show this page; close it.
    browser->GetHost()->CloseBrowser(false); return true;
  }
  from->workspace->Refresh(); to->workspace->Refresh();
  auto details = CefDictionaryValue::Create(); details->SetInt("browser_id", id); details->SetBool("removed", remove_entry);
  details->SetInt("peek_id", peek ? peek->browser->GetIdentifier() : -1);
  from->Record("tab_transferred_out", details); to->Record("tab_transferred_in", details);
  return true;
}

bool CreateWorkspaceWindow(const std::shared_ptr<State>& source, const fs::path& directory, const std::string& tab, bool private_window = false,
                           const std::string& link = "") {
  if (!source->session || !source->ui_context) return false;
  // A private page never moves into a normal window (its cookies and storage live in the private context).
  if (source->private_window && !tab.empty()) return false;
  const auto seed = !tab.empty() && source->workspace ? source->workspace->DetachedSeed(tab) : std::optional<red::TreeNode>{};
  if (!tab.empty() && !seed) return false;
  auto state = std::make_shared<State>();
  state->session = source->session; state->secondary_window = true;
  state->standalone_window = seed.has_value();
  state->workspace_data = source->session->workspace;
  if (seed) {
    GUID guid{}; wchar_t identifier[40]{};
    if (FAILED(CoCreateGuid(&guid)) || !StringFromGUID2(guid, identifier, 40)) return false;
    state->workspace_data = std::make_shared<red::WorkspaceData>();
    state->workspace_data->path = source->profile / "BrowserData" / "windows" / (std::wstring(identifier) + L".sqlite3");
    state->session->AddWorkspace(state->workspace_data);
    state->initial_url = seed->url; state->initial_title = seed->title;
    state->initial_custom_title = seed->custom_title;
    state->sidebar_visible = false;
    const bool today = !seed->pinned && !seed->favorite;
    auto ready = [weak = std::weak_ptr<red::WorkspaceController>(source->workspace), id = seed->id] {
      if (auto workspace = weak.lock()) workspace->DetachedReady(id);
    };
    if (source->workspace->CanTransfer(seed->id)) {
      // The live page moves in when the window would load its entry; if it
      // cannot by then, the window loads it fresh and the source unloads (as before).
      state->adopt_page = [from = std::weak_ptr<State>(source), to = std::weak_ptr<State>(state), id = seed->id, today, ready](const std::string& node_id) {
        const auto source = from.lock(); const auto destination = to.lock();
        if (source && destination && TransferPage(source, destination, id, node_id, today)) return true;
        if (destination && today) destination->detached_ready = ready;
        return false;
      };
    } else if (today) state->detached_ready = ready;
  } else if (private_window) {
    // Local tabs only, kept in memory; an empty cache path makes CEF create an isolated
    // off-the-record context (Chrome's OffTheRecordProfileImpl), destroyed with this window.
    state->private_window = state->standalone_window = true;
    state->workspace_data = std::make_shared<red::WorkspaceData>();
    state->workspace_data->path = ":memory:";
    state->session->AddWorkspace(state->workspace_data);
    state->web_context = CefRequestContext::CreateContext(CefRequestContextSettings(), nullptr);
    if (!state->web_context) return false;
  } else if (source->workspace) state->initial_space = source->workspace->Snapshot()->GetString("activeSpace").ToString();
  if (!tab.empty()) {
    POINT cursor{};
    if (GetCursorPos(&cursor)) {
      const auto display = CefDisplay::GetDisplayNearestPoint(CefPoint(cursor.x, cursor.y), true);
      if (display) {
        const auto point = CefDisplay::ConvertScreenPointFromPixels(CefPoint(cursor.x, cursor.y));
        const auto area = display->GetWorkArea();
        const auto previous = source->window ? source->window->GetBounds() : CefRect(0, 0, 1100, 780);
        const int width = std::min({1100, std::max(700, previous.width), area.width});
        const int height = std::min({780, std::max(420, previous.height), area.height});
        state->initial_window_bounds = CefRect(std::clamp(point.x - 180, area.x, area.x + area.width - width),
            std::clamp(point.y - 15, area.y, area.y + area.height - height), width, height);
      }
    }
  }
  state->workspace_mode = true; state->profile = source->profile;
  if (!state->private_window) { state->active_profile = source->active_profile; state->web_context = source->web_context; }
  state->ui_context = source->ui_context;
  state->theme = source->theme; state->sidebar_width = source->sidebar_width;
  state->backdrop_switch = source->backdrop_switch;
  state->downloads = source->downloads; state->history = source->history;
  state->favicons = source->favicons; state->extensions = source->extensions;
  if (seed) if (const auto profile=source->workspace->ProfileForNode(seed->id)) {
    state->active_profile=profile->id; state->web_context=profile->context;
    state->history=profile->history; state->downloads=profile->downloads;
    state->favicons=profile->favicons; state->extensions=profile->extensions;
  }
  if (state->private_window) {
    // Dark violet so a private window is never mistaken for a normal one.
    state->theme = red::Theme{278, 100, red::ThemeMode::Dark, 0};
    // Its own downloads list (not recorded), favicons kept in memory only; no extensions run in
    // an off-the-record profile. History is only read (address suggestions), never written.
    state->downloads = std::make_shared<red::DownloadManager>(state, red::DownloadManager::DefaultDirectory());
    state->favicons = std::make_shared<red::FaviconCache>(); state->extensions = nullptr;
  }
  state->session->Register(state);
  state->workspace = std::make_shared<red::WorkspaceController>(state);
  // Opened once the window is ready, like a link from another app (no seed or startup tab first).
  if (!link.empty()) state->workspace->QueueExternalUrls({link});
  state->workspace->Start();
  ChooseBackdrop(state);
  CefBrowserSettings settings;
  const auto background = red::Themed({231, 222, 237}, state->theme, state->theme_dark, true);
  settings.background_color = CefColorSetARGB(255, background.r, background.g, background.b);
  // A system backdrop: a transparent shell (alpha 0 with a non-zero colour, engine patch 12).
  if (state->translucent) settings.background_color = CefColorSetARGB(0, background.r | 1, background.g, background.b);
  auto extra = CefDictionaryValue::Create(); extra->SetBool("trusted_ui", true);
  auto shell = CefBrowserView::CreateBrowserView(new Client(state, true), std::string(red::kUiUrl),
      settings, extra, state->ui_context, new StyleDelegate(CEF_RUNTIME_STYLE_ALLOY, state->translucent ? settings.background_color : 0));
  shell->SetBackgroundColor(settings.background_color);
  const auto window = CefWindow::CreateTopLevelWindow(new WindowDelegate(state, shell, nullptr));
  return window != nullptr;
}

class UiContextReady final : public CefRequestContextHandler {
 public:
  UiContextReady(std::shared_ptr<State> state, fs::path directory, std::string url)
      : state_(state), directory_(std::move(directory)), url_(std::move(url)) {}
  void OnRequestContextInitialized(CefRefPtr<CefRequestContext> ui_context) override {
    if (!red::GuardShellProfile(state_, ui_context)) { CefQuitMessageLoop(); return; }
    ui_context->RegisterSchemeHandlerFactory("sol", "ui", new UiResources(state_, directory_ / "ui"));
    state_->ui_context = ui_context;  // History page tabs use this trusted profile.
    if (state_->session)
      state_->session->new_window = [directory = directory_](const std::shared_ptr<State>& from, const std::string& tab) { return CreateWorkspaceWindow(from, directory, tab); };
    if (state_->session)
      state_->session->new_private_window = [directory = directory_](const std::shared_ptr<State>& from) { return CreateWorkspaceWindow(from, directory, "", true); };
    if (state_->session)
      state_->session->new_window_with_link = [directory = directory_](const std::shared_ptr<State>& from, const std::string& url, bool private_window) {
        return CreateWorkspaceWindow(from, directory, "", private_window, url);
      };
    if (state_->session) state_->session->transfer_page = TransferPage;
    ChooseBackdrop(state_);
    CefBrowserSettings settings;
    // Until the shell document paints, its view shows this color (CEF's
    // default is white): the themed window background, as style.css :root.
    const auto background = red::Themed({231, 222, 237}, state_->theme, state_->theme_dark, true);
    settings.background_color = CefColorSetARGB(255, background.r, background.g, background.b);
    if (state_->translucent) settings.background_color = CefColorSetARGB(0, background.r | 1, background.g, background.b);
    auto extra = CefDictionaryValue::Create(); extra->SetBool("trusted_ui", true);
    auto shell = CefBrowserView::CreateBrowserView(new Client(state_, true), std::string(red::kUiUrl),
        settings, extra, ui_context, new StyleDelegate(CEF_RUNTIME_STYLE_ALLOY, state_->translucent ? settings.background_color : 0));
    shell->SetBackgroundColor(settings.background_color);
    if (state_->embedded_tabs_probe || state_->workspace_mode) {
      CefWindow::CreateTopLevelWindow(new WindowDelegate(state_, shell, nullptr));
      return;
    }
    if (state_->toolbar_probe) {
      CefWindow::CreateTopLevelWindow(new WindowDelegate(state_, shell, state_->toolbar_probe->CreateView(url_)));
      return;
    }
    if (state_->native_tabs_reference) {
      CefWindowInfo info;
      info.SetAsPopup(nullptr, "Sol native Chrome tabs reference");
      info.runtime_style = CEF_RUNTIME_STYLE_CHROME;
      CefBrowserHost::CreateBrowser(info, new Client(state_, false), url_, settings,
                                   nullptr, CefRequestContext::GetGlobalContext());
      CefWindow::CreateTopLevelWindow(new WindowDelegate(state_, shell, nullptr));
      return;
    }
    auto content = CefBrowserView::CreateBrowserView(new Client(state_, false), url_,
        settings, nullptr, CefRequestContext::GetGlobalContext(), new StyleDelegate(CEF_RUNTIME_STYLE_CHROME));
    CefWindow::CreateTopLevelWindow(new WindowDelegate(state_, shell, content));
  }
 private:
  std::shared_ptr<State> state_;
  fs::path directory_;
  std::string url_;
  IMPLEMENT_REFCOUNTING(UiContextReady);
};

class App final : public CefApp, public CefBrowserProcessHandler, public CefRenderProcessHandler {
 public:
  App(std::shared_ptr<State> state, fs::path directory, std::string url, std::string accept_languages)
      : state_(state), directory_(std::move(directory)), url_(std::move(url)),
        accept_languages_(std::move(accept_languages)) {}
  CefRefPtr<CefBrowserProcessHandler> GetBrowserProcessHandler() override { return this; }
  CefRefPtr<CefRenderProcessHandler> GetRenderProcessHandler() override { return this; }
  void OnRegisterCustomPreferences(cef_preferences_type_t type,
      CefRawPtr<CefPreferenceRegistrar> registrar) override {
    if (type != CEF_PREFERENCES_TYPE_REQUEST_CONTEXT || accept_languages_.empty()) return;
    // Chromium initializes an empty selected-language list from this default.
    // CefSettings.accept_language_list would override saved profile choices.
    auto value = CefValue::Create();
    value->SetString(accept_languages_);
    CefString error;
    if (!registrar->SetDefaultPreference("intl.accept_languages", value, error))
      LOG(WARNING) << "Could not set the default web languages: " << error.ToString();
  }
  // Our host passes its own client to every browser it creates. In the
  // workspace, anything arriving here was created by Chrome itself.
  CefRefPtr<CefClient> GetDefaultClient() override {
    if (state_->session) if (auto active = state_->session->active_window.lock(); active && active->window && active->workspace && !active->workspace->WindowClosing())
      return new Client(active, false, "", false, {-1, -1}, active->workspace_mode);
    return new Client(state_, false, "", false, {-1, -1}, state_->workspace_mode);
  }
  void OnBeforeCommandLineProcessing(const CefString& type, CefRefPtr<CefCommandLine> command) override {
    // Negative codec experiment: leave the GPU sandbox intact, disable video decode only.
    if (type.empty() && state_->probe && state_->platform_codecs_probe &&
        command->GetSwitchValue("probe-link-headers") == "codecs-no-gpu")
      command->AppendSwitch("disable-accelerated-video-decode");
#if defined(RED_CEF_PAGE_CONTAINER)
    // Native Chrome children choose their compositor opacity at creation.
    if (type.empty()) command->AppendSwitch("red-translucent-child-windows");
#endif
    // Websites and extensions live in <profile>\Default. Chrome otherwise starts
    // in Local State's "last used" profile, which can become Shell (the trusted
    // UI profile), and CEF's global context follows it: web tabs, Web Store
    // installs and extensions then landed in Shell (user report 2026-11-06).
    if (type.empty()) command->AppendSwitchWithValue("profile-directory", "Default");
    if (type.empty()) {
      // Pinned Chromium 154 uses OverlayScrollbar's mode parameter; Fluent is
      // already the Windows theme. Keep other caller-supplied feature overrides.
      const auto existing = command->GetSwitchValue("enable-features").ToString();
      if (existing.find("OverlayScrollbar") == std::string::npos)
        command->AppendSwitchWithValue("enable-features", existing.empty() ?
            "OverlayScrollbar:mode/overlay" : existing + ",OverlayScrollbar:mode/overlay");
    }
    // Probe only: fake camera/microphone so tests never open real devices.
    if (type.empty() && state_->site_probe) command->AppendSwitch("use-fake-device-for-media-stream");
    // Probe only: audio tests never play through the speakers.
#if !CEF_API_ADDED(CEF_EXPERIMENTAL)
    // The renderer detector reads element state. Chromium's own audible state
    // (engine patch 04) measures output levels, which --mute-audio zeroes: the
    // probe mutes the tab instead (OnAudibleProbeLoad).
    if (type.empty() && state_->audible_probe) command->AppendSwitch("mute-audio");
#endif
    // Probe only: a probe window covered by another app (unattended runs) must
    // not count as hidden, which stops frames and requestAnimationFrame.
    if (type.empty() && state_->probe && !command->HasSwitch("probe-window-transfer")) {
      const auto existing = command->GetSwitchValue("disable-features").ToString();
      command->AppendSwitchWithValue("disable-features", existing.empty() ? "CalculateNativeWinOcclusion" : existing + ",CalculateNativeWinOcclusion");
    }
  }
  void OnBeforeChildProcessLaunch(CefRefPtr<CefCommandLine> command) override {
    // Arbitrary browser CLI switches aren't automatically inherited by renderers.
    if (state_->probe) command->AppendSwitch("probe-report");
  }
  void OnRegisterCustomSchemes(CefRawPtr<CefSchemeRegistrar> registrar) override {
    // FETCH_ENABLED: the welcome reads its music with fetch(). UiResources serves
    // only Sol's own UI browsers, so websites still get nothing from sol://.
    registrar->AddCustomScheme("sol", CEF_SCHEME_OPTION_STANDARD | CEF_SCHEME_OPTION_SECURE |
        CEF_SCHEME_OPTION_CORS_ENABLED | CEF_SCHEME_OPTION_DISPLAY_ISOLATED | CEF_SCHEME_OPTION_FETCH_ENABLED);
  }
  void OnContextInitialized() override {
    CEF_REQUIRE_UI_THREAD();
    // A memory report request reaches a running Sol through OnAlreadyRunningAppRelaunch. When no
    // Sol was running, this launch became the first instance: there is nothing to report, so quit.
    if (CefCommandLine::GetGlobalCommandLine()->HasSwitch("memory-report")) { CefQuitMessageLoop(); return; }
    // Websites must never run in the trusted Shell profile: the global context
    // has to be <profile>\Default (--profile-directory, OnBeforeCommandLineProcessing).
    {
      const fs::path web = CefRequestContext::GetGlobalContext()->GetCachePath().ToWString();
      std::error_code error;
      if (!fs::equivalent(web, state_->profile / "Default", error)) {
        LOG(ERROR) << "Web profile is not Default: " << web.u8string().size() << " chars";
        MessageBoxW(nullptr, L"Sol could not open its website profile safely and will close.", L"Sol", MB_OK | MB_ICONERROR);
        state_->exit_code = 18; CefQuitMessageLoop(); return;
      }
    }
    red::EnsureWidevine(state_);
    // Before any page exists: Chrome must not restore its own session into Sol
    // (app/chrome_session_restore.h). BrowsingProfiles::Stop sets it back on exit.
    if (state_->workspace_mode && !state_->private_window) {
      auto details = CefDictionaryValue::Create();
      details->SetBool("suppressed", red::SuppressChromeSessionRestore(CefRequestContext::GetGlobalContext()));
      state_->Record("chrome_session_restore", details);
    }
    if (state_->probe && CefCommandLine::GetGlobalCommandLine()->HasSwitch("probe-window-transfer")) {
      red::StartWindowTransfer(state_, url_);
      return;
    }
    if (state_->workspace_mode) {
      state_->workspace = std::make_shared<red::WorkspaceController>(state_);
      // After an update, the new version's release notes open once. Queued before OS launch
      // links, so a link the person clicked still ends up as the selected tab.
      if (!state_->probe && !state_->private_window && !state_->standalone_window)
        if (auto notes = red::ReleaseNotesAfterUpdate(state_->profile / "BrowserData", RED_APP_VERSION, state_->fresh_profile))
          state_->workspace->QueueExternalUrls({*notes});
      state_->workspace->QueueExternalUrls(state_->external_urls);
      state_->workspace->Start();
      state_->extension_install_watch = red::WatchExtensionInstalls(state_, CefRequestContext::GetGlobalContext());
      state_->downloads = std::make_shared<red::DownloadManager>(state_,
          state_->downloads_directory.empty() ? red::DownloadManager::DefaultDirectory() : state_->downloads_directory);
      // Browsing history and finished downloads survive restarts. Earlier
      // sessions' downloads stay on the downloads page; the popover starts empty.
      state_->favicons = std::make_shared<red::FaviconCache>();
      state_->history = std::make_shared<red::HistoryService>(state_);
      state_->history->Start(state_->profile / "BrowserData" / "history.sqlite3",
          [weak = std::weak_ptr<State>(state_)](std::vector<red::DownloadRecord> records) {
            auto state = weak.lock(); if (!state || !state->probe) return;
            auto data = CefDictionaryValue::Create(); auto names = CefListValue::Create();
            for (const auto& record : records) names->SetString(names->GetSize(), record.name);
            data->SetList("names", names); state->Record("downloads_saved_at_start", data);
          });
      state_->history->LoadFavicons([weak = std::weak_ptr<State>(state_), favicons=state_->favicons](std::vector<std::pair<std::string, std::string>> icons) {
        auto state = weak.lock(); if (!state) return;
        for (const auto& [host, png] : icons) favicons->Seed(host, png);
        if (state->workspace) state->workspace->Refresh();
      });
      // Chrome's own download bubble ("Don't show when downloads finish") has
      // no place in this host; our popover replaces it. Public preference API.
      auto content = CefRequestContext::GetGlobalContext();
      auto off = CefValue::Create(); off->SetBool(false); CefString error;
      const bool bubble_off = content->HasPreference("download_bubble.partial_view_enabled") &&
          content->CanSetPreference("download_bubble.partial_view_enabled") &&
          content->SetPreference("download_bubble.partial_view_enabled", off, error);
      auto details = CefDictionaryValue::Create(); details->SetBool("disabled", bubble_off);
      if (!bubble_off) details->SetString("error", error.ToString().substr(0, 200));
      state_->Record("chrome_download_bubble", details);
    }
    if (state_->workspace_mode) {
      state_->session->profiles = std::make_shared<red::BrowsingProfiles>();
      state_->session->profiles->AdoptDefault(state_);
      red::SelectBrowsingProfile(state_, "default");
    }
    // A separate regular profile also avoids sharing the theme service between
    // an off-the-record profile and its parent in the same Chrome window.
    CefRequestContextSettings ui_settings;
    CefString(&ui_settings.cache_path) = (state_->profile / "Shell").wstring();
    ui_context_ = CefRequestContext::CreateContext(ui_settings, new UiContextReady(state_, directory_, url_));
  }
  void ReleaseContext() { ui_context_ = nullptr; }
  bool OnAlreadyRunningAppRelaunch(CefRefPtr<CefCommandLine> command, const CefString&) override {
    // `sol.exe --memory-report=<file>` while Sol runs: describe its browsers, open nothing.
    if (command->HasSwitch("memory-report")) {
      const fs::path report = command->GetSwitchValue("memory-report").ToWString();
      state_->Record(red::WriteMemoryReport(state_, report) ? "memory_report_written" : "memory_report_refused");
      return true;
    }
    // CEF's profile singleton forwards OS launches to our existing workspace.
    if (!state_->workspace_mode) return false;
    auto target = state_;
    if (state_->session) {
      if (auto active = state_->session->active_window.lock(); active && active->window && active->workspace && !active->workspace->WindowClosing()) target = active;
      else for (const auto& window : state_->session->Windows())
        if (window->window && window->workspace && !window->workspace->WindowClosing()) { target = window; break; }
    }
    if (target->workspace && !target->workspace->WindowClosing()) {
      target->workspace->QueueExternalUrls(red::ExternalLaunchUrls(command));
      if (target->window) {
        // Restore also unmaximizes a visible window. External links should
        // bring Red forward without changing its current size/state.
        if (target->window->IsMinimized()) target->window->Restore();
        target->window->Show(); target->window->Activate();
      }
      state_->Record("external_launch_received");
    }
    return true;
  }
  void OnWebKitInitialized() override {
    renderer_router_ = CefMessageRouterRendererSide::Create(CefMessageRouterConfig());
  }
  void OnBrowserCreated(CefRefPtr<CefBrowser> browser, CefRefPtr<CefDictionaryValue> extra) override {
    trusted_[browser->GetIdentifier()] = extra && extra->GetBool("trusted_ui");
  }
  void OnBrowserDestroyed(CefRefPtr<CefBrowser> browser) override { trusted_.erase(browser->GetIdentifier()); }
  void OnContextCreated(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
      CefRefPtr<CefV8Context> context) override {
    if (trusted_[browser->GetIdentifier()] && frame->IsMain() &&
        (frame->GetURL().ToString() == red::kUiUrl || frame->GetURL().ToString() == red::kCommandUrl ||
         frame->GetURL().ToString() == red::kPanelUrl || frame->GetURL().ToString() == red::kPeekUrl || frame->GetURL().ToString() == red::kSettingsUrl || frame->GetURL().ToString() == red::kWelcomeUrl || frame->GetURL().ToString() == red::kBoostUrl || frame->GetURL().ToString() == red::kMenuUrl || frame->GetURL().ToString() == red::kDevToolsWindowUrl ||
         red::IsInternalPageUrl(frame->GetURL().ToString())))
      renderer_router_->OnContextCreated(browser, frame, context);
    // Website frames only (the function checks http/https); never trusted UI.
    // With Red's engine patch 04 Chromium reports the real audible state
    // (OnAudibleStateChange), so the renderer heuristic is not installed.
#if !CEF_API_ADDED(CEF_EXPERIMENTAL)
    if (!trusted_[browser->GetIdentifier()]) red::InstallAudibleDetector(frame, context);
#endif
    if (state_->probe && frame->IsMain()) {
      auto message = CefProcessMessage::Create("red.probe.token");
      message->GetArgumentList()->SetDictionary(0, TokenEvidence());
      frame->SendProcessMessage(PID_BROWSER, message);
    }
  }
  void OnContextReleased(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
      CefRefPtr<CefV8Context> context) override {
    renderer_router_->OnContextReleased(browser, frame, context);
    red::ReleaseAudibleDetector(frame);
  }
  bool OnProcessMessageReceived(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
      CefProcessId source, CefRefPtr<CefProcessMessage> message) override {
    return renderer_router_->OnProcessMessageReceived(browser, frame, source, message);
  }
 private:
  std::shared_ptr<State> state_;
  fs::path directory_;
  std::string url_;
  std::string accept_languages_;
  std::map<int, bool> trusted_;
  CefRefPtr<CefMessageRouterRendererSide> renderer_router_;
  CefRefPtr<CefRequestContext> ui_context_;
  IMPLEMENT_REFCOUNTING(App);
};
}  // namespace

CEF_BOOTSTRAP_EXPORT int RunWinMain(HINSTANCE instance, LPWSTR, int, void* sandbox_info,
                                    cef_version_info_t*) {
  if (!sandbox_info) return 10;  // Fail closed. No non-sandbox entry point.
  auto command = CefCommandLine::CreateCommandLine();
  command->InitFromString(GetCommandLineW());
  CefCommandLine::SwitchMap switches;
  command->GetSwitches(switches);
  const bool probe = command->HasSwitch("probe-report");
  for (const auto& [name, value] : switches)
    if (red::IsForbiddenSwitch(name.ToString())) return 11;
  if (!command->HasSwitch("type")) {
    // Velopack's install/update/uninstall launches run their hook and exit here.
    if (!probe) red::RunUpdateHooks();
    for (const auto& [name, value] : switches)
      if (!red::IsAllowedBrowserSwitch(name.ToString(), probe)) return 11;
    if (command->HasSwitch("auto-archive-test-clock")) {
      const fs::path profile=command->GetSwitchValue("profile-dir").ToWString();
      if (command->GetSwitchValue("auto-archive-test-clock")!="manual-fixture" || !profile.is_absolute() || profile.filename()!=L"profile" ||
          !profile.parent_path().filename().wstring().starts_with(L"manual-input-") || profile.parent_path().parent_path().filename()!=L"artifacts") return 11;
    }
  }
  wchar_t module[MAX_PATH];
  const auto length = GetModuleFileNameW(nullptr, module, MAX_PATH);
  if (!length || length == MAX_PATH) return 12;
  const auto directory = fs::path(module).parent_path();
  if (!command->HasSwitch("type") && command->HasSwitch("make-default-browser")) {
    std::string error;
    return red::OpenDefaultBrowserSettings(nullptr, error) ? 0 : 20;
  }
  // The installed Sol (Velopack or Inno), not a development build.
  const bool installed = !command->HasSwitch("type") && _wcsicmp(module, red::InstalledBrowserPath().c_str()) == 0;
  if (installed) SetCurrentProcessExplicitAppUserModelID(probe?L"SolBrowser.Probe":red::kBrowserAppId);
  // Start menu shortcut and Default Apps offer for this install (Velopack's
  // own shortcuts are off: they cannot pass --workspace).
  if (installed && !probe) red::EnsureRegistered();
  auto state = std::make_shared<State>();
  state->probe = probe;
  state->probe_navigation = command->HasSwitch("probe-navigation");
  state->probe_tabs = command->HasSwitch("probe-tabs");
  state->native_tabs_reference = command->HasSwitch("probe-native-tabs");
  state->embedded_tabs_probe = command->HasSwitch("probe-embedded-tabs");
  state->organization_probe = command->HasSwitch("probe-organization");
  state->input_probe = command->HasSwitch("probe-input");
  state->browsing_probe = command->HasSwitch("probe-browsing");
  state->downloads_probe = command->HasSwitch("probe-downloads");
  state->fullscreen_probe = command->HasSwitch("probe-fullscreen");
  state->site_probe = command->HasSwitch("probe-site-settings");
  state->tab_actions_probe = command->HasSwitch("probe-tab-actions");
  state->audible_probe = command->HasSwitch("probe-audible");
  state->downloads_page_probe = command->HasSwitch("probe-downloads-page");
  state->extension_popup_probe = command->HasSwitch("probe-extension-popup");
  state->protocols_probe = command->GetSwitchValue("probe-protocols").ToString();
  state->close_motion_probe = command->HasSwitch("probe-close-motion");
  state->sidebar_peek_probe = command->HasSwitch("probe-sidebar-peek");
  if (state->sidebar_peek_probe) state->x_unread_fixture = 4;  // The X counter's answer in that probe.
  state->google_integrations_probe = command->HasSwitch("probe-google-integrations");
  state->sidebar_perf_probe = command->HasSwitch("probe-sidebar-perf");
  state->pip_probe = command->HasSwitch("probe-pip");
  state->capture_mute_probe = command->HasSwitch("probe-capture-mute");
  state->palette_probe=command->HasSwitch("probe-palette");
  state->theme_preview_probe=command->HasSwitch("probe-theme-preview");
  state->theme_probe = command->HasSwitch("probe-theme")||state->palette_probe||state->theme_preview_probe;
  state->spaces_probe = command->HasSwitch("probe-spaces");
  state->boost_probe = command->HasSwitch("probe-boost");
  state->menu_probe = command->HasSwitch("probe-menu");
  state->private_probe = command->HasSwitch("probe-private");
#if defined(RED_CEF_TRANSLUCENT)
  if (const auto value = command->GetSwitchValue("probe-backdrop").ToString(); red::ValidBackdrop(value)) state->backdrop_switch = value;
#endif
  state->pin_probe = command->HasSwitch("probe-pin");
  state->menu_page = command->GetSwitchValue("probe-menu") == "page";
  state->menu_edges = command->GetSwitchValue("probe-menu") == "edges";
  state->boost_store = command->GetSwitchValue("probe-boost") == "store";
  state->boost_pick = command->GetSwitchValue("probe-boost") == "pick";
  state->ua_brand_probe = command->GetSwitchValue("probe-ua-brand").ToString().substr(0, 32);
  state->passwords_probe = command->GetSwitchValue("probe-link-headers") == "passwords";
  state->link_headers_probe = state->passwords_probe || command->HasSwitch("probe-link-headers");
  state->import_probe = command->GetSwitchValue("probe-import").ToString().substr(0, 2048);
  state->devtools_regression = state->probe && command->GetSwitchValue("probe-link-headers") == "devtools";
  if (state->devtools_regression) state->link_headers_cycles = 1;
  state->platform_codecs_probe = state->probe &&
      (command->GetSwitchValue("probe-link-headers") == "codecs" ||
       command->GetSwitchValue("probe-link-headers") == "codecs-no-gpu");
  // Use the existing 30-second probe budget, with a separate codec load branch.
  if (state->platform_codecs_probe) state->link_headers_cycles = 1;
  // cycles: open Link Peek and close it once settled; quick: close it during entry.
  if (const auto mode = command->GetSwitchValue("probe-link-headers").ToString(); mode == "cycles" || mode == "quick") {
    state->link_headers_cycles = 5; state->link_headers_quick = mode == "quick";
  }
  state->link_headers_park = command->GetSwitchValue("probe-link-headers") == "park";
  state->link_headers_minimize = command->GetSwitchValue("probe-link-headers") == "minimize";
  state->link_headers_unload = command->GetSwitchValue("probe-link-headers") == "unload";
  state->link_headers_print = command->GetSwitchValue("probe-link-headers") == "print";
  state->link_headers_menu = command->GetSwitchValue("probe-link-headers") == "peekmenu";
  state->link_headers_tabmenu = command->GetSwitchValue("probe-link-headers") == "tabmenu";
  state->link_headers_inspect = command->GetSwitchValue("probe-link-headers") == "inspect";
  state->link_headers_zoom = command->GetSwitchValue("probe-link-headers") == "zoom";
  state->link_headers_transfer = command->GetSwitchValue("probe-link-headers") == "transfer";
  if (state->sidebar_perf_probe && !command->GetSwitchValue("probe-sidebar-perf").empty())
    state->sidebar_perf_tabs = std::clamp(std::atoi(command->GetSwitchValue("probe-sidebar-perf").ToString().c_str()), 0, 200);
  if (command->HasSwitch("probe-protocols") && !red::IsLocalFileUrl(state->protocols_probe)) return 11;
  state->history_probe = command->GetSwitchValue("probe-history").ToString();
  if (command->HasSwitch("probe-history") && state->history_probe != "record" && state->history_probe != "restart" && state->history_probe != "page") return 11;
  state->permissions_probe = command->GetSwitchValue("probe-permissions").ToString();
  if (command->HasSwitch("probe-permissions") && !red::IsPermissionsProbeType(state->permissions_probe)) return 11;
  state->lifecycle_probe = command->HasSwitch("probe-lifecycle");
  state->lifecycle_mode = command->GetSwitchValue("probe-lifecycle").ToString();
  if (state->lifecycle_probe && state->lifecycle_mode.empty()) state->lifecycle_mode = "full";
  if (state->lifecycle_probe && state->lifecycle_mode != "full" && state->lifecycle_mode != "restore" && state->lifecycle_mode != "hold" && state->lifecycle_mode != "manual") return 11;
  state->workspace_probe = command->HasSwitch("probe-workspace") || state->organization_probe || state->browsing_probe;
  // The installed Sol is always the workspace browser; Velopack starts it with no arguments.
  state->workspace_mode = installed || command->HasSwitch("workspace") || state->workspace_probe || state->lifecycle_probe || state->downloads_probe || state->fullscreen_probe || !state->permissions_probe.empty() || state->site_probe || !state->history_probe.empty() || state->tab_actions_probe || state->audible_probe || state->downloads_page_probe || state->extension_popup_probe || !state->protocols_probe.empty() || state->close_motion_probe || state->sidebar_peek_probe || state->sidebar_perf_probe || state->pip_probe || state->capture_mute_probe || state->theme_probe || state->spaces_probe || !state->ua_brand_probe.empty() || state->boost_probe || state->menu_probe || state->private_probe || state->pin_probe || state->link_headers_probe || !state->import_probe.empty();
  state->probe_tabs = state->probe_tabs || state->native_tabs_reference || state->embedded_tabs_probe;
  auto url = command->HasSwitch("probe-url") ? command->GetSwitchValue("probe-url").ToString() : "https://example.com";
  state->initial_url = url;
  if (!command->HasSwitch("type")) state->external_urls = red::ExternalLaunchUrls(command);
  if (state->probe_tabs) state->tabs = std::make_shared<red::TabProbe>(state, url);
  if (command->HasSwitch("probe-downloads-dir")) {
    state->downloads_directory = fs::path(command->GetSwitchValue("probe-downloads-dir").ToWString());
    if (!state->downloads_directory.is_absolute()) return 11;
  }
  if (command->HasSwitch("probe-chrome-toolbar")) {
    if (state->workspace_mode || state->probe_tabs) return 11;
    const auto type = command->GetSwitchValue("probe-chrome-toolbar").ToString();
    if (type != "normal" && type != "location" && type != "manual") return 11;
    std::weak_ptr<State> weak = state;
    state->toolbar_probe = std::make_shared<red::ToolbarProbe>(weak, type == "location" ? CEF_CTT_LOCATION : CEF_CTT_NORMAL, type == "manual",
        [weak]() -> CefRefPtr<CefClient> { auto current = weak.lock(); return current ? new Client(current, false) : nullptr; });
  }
  auto locale = command->HasSwitch("type") ? red::BrowserLocale{} : red::ReadWindowsBrowserLocale();
  CefRefPtr<App> app = new App(state, directory, url, std::move(locale.accept_languages));
  CefMainArgs args(instance);
  const int child_exit = CefExecuteProcess(args, app, sandbox_info);
  if (child_exit >= 0) return child_exit;
  if (command->HasSwitch("trace-tab-switches")) {
    const fs::path report(command->GetSwitchValue("trace-tab-switches").ToWString());
    if (!report.is_absolute() || report.root_name().wstring().size() != 2) return 13;
    state->tab_trace.open(report, std::ios::trunc);
    if (!state->tab_trace) return 13;
  }
  if (state->probe) {
    state->evidence_directory = fs::path(command->GetSwitchValue("probe-report").ToWString()).parent_path();
    state->evidence.open(fs::path(command->GetSwitchValue("probe-report").ToWString()), std::ios::trunc);
    if (!state->evidence) return 13;
  }
  auto version = CefDictionaryValue::Create();
  version->SetString("cef_version", CEF_VERSION);
  version->SetString("cef_commit", CEF_COMMIT_HASH);
  state->Record("startup", version);
  CefSettings settings;
  settings.no_sandbox = false;
  settings.windowless_rendering_enabled = true;  // Short-lived mailbox preview workers.
  CefString(&settings.locale) = locale.chromium;
  wchar_t* local_data = nullptr;
  size_t env_length = 0;
  _wdupenv_s(&local_data, &env_length, L"LOCALAPPDATA");
  if (!local_data) return 14;
  const fs::path local(local_data);
  free(local_data);
  fs::path profile;
  if (command->HasSwitch("profile-dir")) profile = fs::path(command->GetSwitchValue("profile-dir").ToWString());
  else {
    // Red -> Sol: the profile moves once from RedBrowser to Sol.
    const auto location = red::ResolveProfile(local);
    if (location.move == red::ProfileMove::Busy) {
      MessageBoxW(nullptr, L"Sol needs to move your Red profile to its new folder, but Red is still using it.\n\n"
                  L"Close Red, then start Sol again.", L"Sol", MB_OK | MB_ICONINFORMATION);
      return 18;
    }
    profile = location.profile;
    if (location.move == red::ProfileMove::Moved) red::AdoptRedLinks();
  }
  if (!profile.is_absolute()) return 15;
  std::error_code profile_error;
  state->fresh_profile = !state->probe && state->evidence_directory.empty() && !fs::exists(profile / "Default", profile_error);
  profile_error.clear();
  fs::create_directories(profile, profile_error);
  if (profile_error) return 16;
  if (!fs::is_regular_file(directory / "ui" / "index.html", profile_error) || profile_error) return 17;
  state->profile = profile;
  // Sol up to 0.1.2 could run websites in Shell; move such data to Default first.
  const auto web_repair = red::RepairWebProfile(profile);
  // The last theme, before any window or UI document exists (no light flash).
  if (auto cached = red::ReadThemeCache(profile / "BrowserData" / "theme-cache"); cached && state->workspace_mode) {
    state->theme = *cached;
    red::WorkspaceController::ApplyTheme(state);
  }
  CefString(&settings.root_cache_path) = profile.wstring();
  CefString(&settings.cache_path) = (profile / "Default").wstring();
  // Match the other persistent browsing profiles. Private windows use an
  // independent request context with no cache path.
  settings.persist_session_cookies = true;
  CefString(&settings.log_file) = (profile / "cef.log").wstring();
  settings.log_severity = LOGSEVERITY_WARNING;
  if (!CefInitialize(args, settings, app, sandbox_info)) return CefGetExitCode();
  if (web_repair != red::WebProfileRepair::None)
    LOG(WARNING) << "Web profile repair: " << (web_repair == red::WebProfileRepair::Moved ? "Shell became Default" : "failed");
  red::InstallWindowIcons();
  // A newer version downloads in the background; the sidebar then offers
  // "Restart to update", and otherwise it applies on the next start.
  if (!state->probe) red::StartUpdateCheck([weak = std::weak_ptr<State>(state)](const std::string& version) {
    LOG(WARNING) << "Update ready: " << version;
    CefPostTask(TID_UI, base::BindOnce([](std::weak_ptr<State> weak) {
      if (const auto state = weak.lock(); state && state->session)
        for (const auto& window : state->session->Windows()) if (window->workspace) window->workspace->Refresh();
    }, weak));
  });
  CefRunMessageLoop();
  state->window = nullptr;
  state->shell_extension_watch = nullptr;
  state->extension_install_watch = nullptr;
  if (state->session) {
    for (const auto& window : state->session->Windows()) {
      if (window->workspace) window->workspace->Stop();
      window->ui_context = nullptr;
      window->web_context = nullptr;
      window->browser_profiles.clear();
      window->password_windows.clear();
      window->shell_extension_watch = nullptr; window->extension_install_watch = nullptr;
    }
    state->session->StopWorkspaces();
  } else if (state->workspace) state->workspace->Stop();
  if (state->history) state->history->Stop();
  app->ReleaseContext();
  state->ui_context = nullptr;
  CefShutdown();
  state->Record("shutdown");
  // The Shell profile guard found an extension there: reset that profile now that
  // nothing holds it, then start again (shell_reset.h).
  if (red::ShellResetScheduled()) {
    const bool reset = red::ResetShellProfile(profile) == red::ShellReset::Done;
    LOG(WARNING) << "Shell profile reset: " << (reset ? "done" : "failed");
    if (!reset) MessageBoxW(nullptr, L"Sol could not reset its interface profile. Restart Windows, then start Sol again.", L"Sol", MB_OK | MB_ICONERROR);
    else if (red::Relaunch()) return 0;
  }
  return state->exit_code;
}
