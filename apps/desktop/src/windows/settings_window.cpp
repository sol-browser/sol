#include "windows/utility_window.h"
#include "windows/focus.h"
#include "windows/ui_context_menu.h"
#include "app/session.h"
#include "windows/settings_window.h"
#include <fstream>
#include <dwmapi.h>
#include "windows/default_browser.h"
#include "windows/branding.h"
#include "windows/window_close.h"
#include "windows/window_corners.h"
#include "windows/extensions_window.h"
#include "windows/passwords_window.h"
#include "windows/command_bar.h"
#include "downloads/download_manager.h"
#include "navigation/shortcut_settings.h"
#include "workspace/controller.h"
#include "experiments/profile_settings_probe.h"
#include "experiments/site_search_probe.h"
#include "import/browser_import.h"
#include "trust_policy.h"
#include "include/cef_app.h"
#include "include/cef_parser.h"
#include "include/cef_version.h"
#include "red_version.h"
#include "app/release_notes.h"
#include "include/views/cef_fill_layout.h"
namespace red {
namespace {
class FolderPicked final : public CefRunFileDialogCallback {
 public:
  explicit FolderPicked(std::function<void(const std::vector<CefString>&)> done) : done_(std::move(done)) {}
  void OnFileDialogDismissed(const std::vector<CefString>& paths) override { done_(paths); }
 private:
  std::function<void(const std::vector<CefString>&)> done_;
  IMPLEMENT_REFCOUNTING(FolderPicked);
};
class SettingsClient final : public UtilityWindowClient, public CefLifeSpanHandler, public CefRequestHandler, public CefDisplayHandler,
    public CefMessageRouterBrowserSide::Handler {
 public:
  explicit SettingsClient(std::weak_ptr<SettingsWindow> owner) : owner_(owner) {
    router_ = CefMessageRouterBrowserSide::Create(CefMessageRouterConfig()); router_->AddHandler(this, false);
  }
  ~SettingsClient() override { router_->RemoveHandler(this); }
  CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return this; }
  CefRefPtr<CefDisplayHandler> GetDisplayHandler() override { return this; }
  bool OnConsoleMessage(CefRefPtr<CefBrowser> browser,cef_log_severity_t,const CefString& message,const CefString&,int) override {
    if (const auto owner=owner_.lock()) owner->Console(message.ToString());
    if (const auto owner=owner_.lock()) if (const auto window=owner->parent()) {
      ObserveProfileSettingsProbe(browser,message.ToString());
      ObserveSiteSearchProbe(browser,message.ToString());
    }
    return false;
  }
  CefRefPtr<CefContextMenuHandler> GetContextMenuHandler() override { return new UiContextMenu(std::weak_ptr<State>()); }
  CefRefPtr<CefRequestHandler> GetRequestHandler() override { return this; }
  void OnAfterCreated(CefRefPtr<CefBrowser> browser) override { if (auto owner = owner_.lock()) owner->BrowserCreated(browser); }
  void OnBeforeClose(CefRefPtr<CefBrowser> browser) override {
    router_->OnBeforeClose(browser); if (auto owner = owner_.lock()) owner->BrowserClosed(browser->GetIdentifier());
  }
  bool OnBeforeBrowse(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, CefRefPtr<CefRequest> request, bool, bool) override {
    router_->OnBeforeBrowse(browser, frame); return !frame->IsMain() || request->GetURL().ToString() != kSettingsUrl;
  }
  bool OnBeforePopup(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, int, const CefString&, const CefString&,
      cef_window_open_disposition_t, bool, const CefPopupFeatures&, CefWindowInfo&, CefRefPtr<CefClient>&,
      CefBrowserSettings&, CefRefPtr<CefDictionaryValue>&, bool*) override { return true; }
  void OnRenderProcessTerminated(CefRefPtr<CefBrowser> browser, TerminationStatus, int, const CefString&) override {
    router_->OnRenderProcessTerminated(browser); if (auto owner = owner_.lock()) owner->Close();
  }
  bool OnProcessMessageReceived(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, CefProcessId source,
      CefRefPtr<CefProcessMessage> message) override { return router_->OnProcessMessageReceived(browser, frame, source, message); }
  bool OnQuery(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, int64_t, const CefString& request,
      bool persistent, CefRefPtr<Callback> callback) override {
    if (auto owner = owner_.lock()) return owner->Query(browser, frame, request, persistent, callback);
    callback->Failure(410, "Settings closed"); return true;
  }
 private:
  std::weak_ptr<SettingsWindow> owner_;
  CefRefPtr<CefMessageRouterBrowserSide> router_;
  IMPLEMENT_REFCOUNTING(SettingsClient);
};
class SettingsView final : public CefBrowserViewDelegate {
 public: cef_runtime_style_t GetBrowserRuntimeStyle() override { return CEF_RUNTIME_STYLE_ALLOY; }
 private: IMPLEMENT_REFCOUNTING(SettingsView);
};
class SettingsDelegate final : public UtilityWindowDelegate {
 public:
  SettingsDelegate(std::weak_ptr<State> state, std::weak_ptr<SettingsWindow> owner, CefRefPtr<CefBrowserView> view)
      : UtilityWindowDelegate(std::move(state), CefSize(940, 700)), owner_(owner), view_(view) {}
  cef_runtime_style_t GetWindowRuntimeStyle() override { return CEF_RUNTIME_STYLE_ALLOY; }
  CefSize GetMinimumSize(CefRefPtr<CefView>) override { return CefSize(720, 520); }
  void OnWindowCreated(CefRefPtr<CefWindow> window) override {
    UtilityWindowDelegate::OnWindowCreated(window);
    window->SetTitle("Settings — Sol"); window->SetBackgroundColor(CefColorSetARGB(255, 28, 26, 32));
    const auto icon = RedWindowIcon(); window->SetWindowIcon(icon); window->SetWindowAppIcon(icon);
    const BOOL dark = TRUE;
    DwmSetWindowAttribute(window->GetWindowHandle(), DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
    const COLORREF caption = RGB(28, 26, 32);
    DwmSetWindowAttribute(window->GetWindowHandle(), DWMWA_CAPTION_COLOR, &caption, sizeof(caption));
    window->SetToFillLayout(); window->AddChildView(view_);
    if (auto owner = owner_.lock()) owner->Created(window);
  }
  bool CanClose(CefRefPtr<CefWindow>) override {
    const auto browser = view_ ? view_->GetBrowser() : nullptr;
    return !browser || browser->GetHost()->TryCloseBrowser();
  }
  void OnWindowDestroyed(CefRefPtr<CefWindow>) override { view_ = nullptr; if (auto owner = owner_.lock()) owner->Destroyed(); }
 private:
  std::weak_ptr<SettingsWindow> owner_; CefRefPtr<CefBrowserView> view_;
  IMPLEMENT_REFCOUNTING(SettingsDelegate);
};
}  // namespace
void ShowSettings(const std::shared_ptr<State>& state) {
  if (!state->settings_window) state->settings_window = std::make_shared<SettingsWindow>(state);
  state->settings_window->Show();
}
void ShowProfileSettings(const std::shared_ptr<State>& state, bool create) {
  ShowSettings(state); state->settings_window->ShowSection(create?"newProfile":"profiles");
}
void SettingsWindow::Console(const std::string& message) {
  const auto state = state_.lock();
  if (!state || !state->probe) return;
  auto details = CefDictionaryValue::Create(); details->SetString("message", message.substr(0, 400));
  state->Record("settings_console", details);
  if (!state->evidence_directory.empty()) {
    std::ofstream out(state->evidence_directory / "settings-console.txt", std::ios::app | std::ios::binary);
    out << message << "\n";
  }
}
void SettingsWindow::Show() {
  const auto state = state_.lock();
  if (!state || !state->window || !state->ui_context || !state->workspace || state->workspace->WindowClosing()) return;
  if (window_) { Present(); return; }
  if (creating_) return;
  creating_ = true;
  CefBrowserSettings settings; settings.background_color = CefColorSetARGB(255, 28, 26, 32);
  auto extra = CefDictionaryValue::Create(); extra->SetBool("trusted_ui", true);
  auto view = CefBrowserView::CreateBrowserView(new SettingsClient(weak_from_this()), std::string(kSettingsUrl),
      settings, extra, state->ui_context, new SettingsView());
  view->SetBackgroundColor(settings.background_color);
  CefWindow::CreateTopLevelWindow(new SettingsDelegate(state_, weak_from_this(), view));
}
void SettingsWindow::Destroyed() {
  window_ = nullptr; ready_ = false;
  // Closing Settings discards anything read from another browser, passwords included.
  CancelBrowserImport(import_); import_.reset();
}
void SettingsWindow::ShowSection(const std::string& section) {
  if (section.empty()) return;
  const auto requested=section;  // The caller may pass section_ itself.
  section_ = requested;
  if (!ready_ || !browser_) return;
  section_.clear();
  // Fixed identifiers only; never page-controlled text.
  if (requested == "import") browser_->GetMainFrame()->ExecuteJavaScript("window.dispatchEvent(new CustomEvent('red-settings-section',{detail:'import'}))", std::string(kSettingsUrl), 0);
  if (requested == "search") browser_->GetMainFrame()->ExecuteJavaScript("window.dispatchEvent(new CustomEvent('red-settings-section',{detail:'search'}))", std::string(kSettingsUrl), 0);
  if (requested == "siteSearch") browser_->GetMainFrame()->ExecuteJavaScript("window.dispatchEvent(new CustomEvent('red-settings-section',{detail:'siteSearch'}))", std::string(kSettingsUrl), 0);
  if (requested == "profiles" || requested == "newProfile") {
    const auto script=requested=="profiles" ? "window.dispatchEvent(new CustomEvent('red-settings-section',{detail:'profiles'}))" :
      "window.dispatchEvent(new CustomEvent('red-settings-section',{detail:'newProfile'}))";
    browser_->GetMainFrame()->ExecuteJavaScript(script,std::string(kSettingsUrl),0);
  }
}
CefRefPtr<CefWindow> SettingsWindow::parent() const {
  const auto state = state_.lock(); return state ? state->window : nullptr;
}
void SettingsWindow::Present() {
  if (!window_ || !ready_) return;
  PresentUtilityWindow(window_, parent());
  if (browser_) browser_->GetHost()->SetFocus(true);
  if (const auto state = state_.lock()) {
    const HWND handle = window_->GetWindowHandle();
    auto details = CefDictionaryValue::Create();
    details->SetBool("owned", state->window && GetWindow(handle, GW_OWNER) == state->window->GetWindowHandle());
    details->SetBool("foreground", GetForegroundWindow() == handle);
    state->Record("settings_presented", details);
  }
}
void SettingsWindow::BrowserCreated(CefRefPtr<CefBrowser> browser) {
  browser_ = browser; creating_ = false;
  if (const auto state = state_.lock()) {
    state->settings_id = browser->GetIdentifier(); RegisterSessionUi(state, browser->GetIdentifier()); state->browsers[browser->GetIdentifier()] = browser; state->Record("settings_created");
    if (!state->window || (state->workspace && state->workspace->WindowClosing())) browser->GetHost()->CloseBrowser(true);
  }
}
void SettingsWindow::BrowserClosed(int id) {
  CloseProfileSettingsProbe(id);
  browser_ = nullptr; ready_ = creating_ = false;
  if (const auto state = state_.lock()) {
    UnregisterSessionUi(state, id); state->browsers.erase(id); state->settings_id = -1;
    if (state->window_close) state->window_close->OnClosed(id);
    if (!state->window && state->browsers.empty()) QuitIfLastWindow(state);
  }
}
void SettingsWindow::Close() { if (window_) window_->Close(); }
void SettingsWindow::SavePreferences(Preferences next, CefRefPtr<CefMessageRouterBrowserSide::Callback> callback) {
  const auto state = state_.lock();
  if (!state || !state->session || !state->session->preferences) { callback->Failure(409,"Settings are not ready"); return; }
  state->session->preferences->Save(std::move(next), [weak = weak_from_this(), callback](const std::string& error) {
    const auto owner = weak.lock(); if (!owner) { callback->Failure(410,"Settings closed"); return; }
    if (!error.empty()) { callback->Failure(409,error); return; }
    if (const auto state = owner->state_.lock(); state && state->session) {
      const auto backdrop = state->session->preferences->Get().backdrop;
      for (const auto& window : state->session->Windows()) {
        // A translucent window switches its backdrop live (turning it on needs a new window).
        if (window->translucent && window->backdrop_switch.empty() && window->backdrop != backdrop) {
          window->backdrop = backdrop; WorkspaceController::ApplyTheme(window);
        }
        RefreshShortcuts(window);
        if (window->workspace) window->workspace->Refresh();
        if (window->command_bar) window->command_bar->Refresh();
      }
      if (state->session->apply_background) state->session->apply_background();
      state->Record("settings_preferences_saved");
    }
    callback->Success(owner->Response());
  });
}
std::string SettingsWindow::Response(bool refresh_default) const {
  auto response = CefDictionaryValue::Create();
  const auto state = state_.lock(); if (!state || !state->workspace) return "{}";
  const auto snapshot = state->workspace->ThemeSnapshot();
  response->SetDictionary("theme", snapshot->GetDictionary("theme"));
  response->SetBool("ready", state->workspace->ready());
  response->SetBool("standalone", state->standalone_window);
  response->SetString("version", RED_APP_VERSION);
  response->SetString("chromiumVersion", std::to_string(CHROME_VERSION_MAJOR) + "." + std::to_string(CHROME_VERSION_MINOR) + "." +
      std::to_string(CHROME_VERSION_BUILD) + "." + std::to_string(CHROME_VERSION_PATCH));
  response->SetString("spaceName", state->workspace->ActiveSpaceName()); response->SetString("spaceId", snapshot->GetString("activeSpace"));
  const auto workspace = state->workspace->Snapshot();
  if (workspace->HasKey("profiles")) response->SetList("profiles",workspace->GetList("profiles")->Copy());
  response->SetList("spaces",workspace->GetList("spaces")->Copy());
  response->SetString("activeProfile",state->active_profile);
  if (refresh_default || !default_status_) {
    default_status_ = DefaultBrowserStatus();
    auto details = CefDictionaryValue::Create();
    details->SetBool("installed", default_status_->GetBool("installed"));
    details->SetBool("registered", default_status_->GetBool("registered"));
    details->SetBool("known", default_status_->GetType("isDefault") == VTYPE_BOOL);
    if (default_status_->GetType("isDefault") == VTYPE_BOOL) details->SetBool("is_default", default_status_->GetBool("isDefault"));
    state->Record("settings_default_status", details);
  }
  response->SetDictionary("defaultBrowser", default_status_->Copy(false));
  const auto preferences = state->session ? state->session->preferences : nullptr;
  const auto current = preferences ? preferences->Get() : Preferences{};
  response->SetDictionary("preferences",BrowserPreferences::ToDictionary(current));
  response->SetString("preferencesError",preferences ? preferences->error() : "Settings are not ready");
  response->SetList("keyboard",DescribeShortcuts(current));
  response->SetBool("systemHour12", SystemTwelveHourClock());
  auto backdrop = CefDictionaryValue::Create();
#if defined(RED_CEF_TRANSLUCENT)
  backdrop->SetBool("available", SystemBackdropSupported());
#else
  backdrop->SetBool("available", false);
#endif
  backdrop->SetBool("active", state->translucent);
  response->SetDictionary("backdrop", backdrop);
  auto value = CefValue::Create(); value->SetDictionary(response); return CefWriteJSON(value, JSON_WRITER_DEFAULT);
}
bool SettingsWindow::Query(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, const CefString& raw,
    bool persistent, CefRefPtr<CefMessageRouterBrowserSide::Callback> callback) {
  const auto state = state_.lock();
  if (!state || !IsSettingsUi(browser->GetIdentifier(), state->settings_id, frame->IsMain(), frame->GetURL().ToString())) {
    callback->Failure(403, "Untrusted settings document"); return true;
  }
  if (persistent || raw.length() > 65536) { callback->Failure(400, "Invalid settings request"); return true; }
  const auto value = CefParseJSON(raw, JSON_PARSER_RFC);
  if (!value || value->GetType() != VTYPE_DICTIONARY || value->GetDictionary()->GetType("method") != VTYPE_STRING) {
    callback->Failure(400, "Invalid settings request"); return true;
  }
  const auto data = value->GetDictionary(); const auto method = data->GetString("method").ToString();
  if (raw.length() > 8192 && !(method == "settings.set" && data->GetType("key") == VTYPE_STRING && data->GetString("key") == "siteSearches")) {
    callback->Failure(400, "Settings request is too large"); return true;
  }
  if (HandleUiFocus(state, browser, method, data->GetSize(), persistent, callback)) return true;
  if (!state->workspace || state->workspace->WindowClosing()) { callback->Failure(409, "Browser is closing"); return true; }
  if (method == "settings.passwords" && data->GetSize() == 3 && data->GetType("profile") == VTYPE_STRING && data->GetType("settings") == VTYPE_BOOL) {
    if (!ShowPasswords(state, data->GetString("profile"), data->GetBool("settings"))) callback->Failure(409, "Choose an available browsing profile in a regular window");
    else callback->Success(Response());
    return true;
  }
  if (method == "settings.get" && data->GetSize() == 1) { callback->Success(Response(true)); return true; }
  if (method == "settings.setIntegration") {
    const auto id = data->GetString("provider").ToString();
    if (data->GetSize() != 3 || data->GetType("provider") != VTYPE_STRING || !FindIntegrationSetting(id) || data->GetType("enabled") != VTYPE_BOOL) {
      callback->Failure(400, "Invalid integration setting"); return true;
    }
    SaveIntegrationPreference(state, id, data->GetBool("enabled"), [weak = weak_from_this(), callback](const std::string& error) {
      const auto owner = weak.lock();
      if (!owner) callback->Failure(410, "Settings closed");
      else if (!error.empty()) callback->Failure(409, error);
      else callback->Success(owner->Response());
    });
    return true;
  }
  if (method == "settings.set" && data->GetSize() == 3 && data->GetType("key") == VTYPE_STRING && state->session && state->session->preferences) {
    auto next = state->session->preferences->Get();
    const auto key = data->GetString("key").ToString();
    bool valid = false;
    if (key == "searchEngine" && data->GetType("value") == VTYPE_STRING && ValidSearchEngine(data->GetString("value"))) {
      next.search_engine = data->GetString("value"); valid = true;
    } else if (key == "askDownload" && data->GetType("value") == VTYPE_BOOL) {
      next.ask_download = data->GetBool("value"); valid = true;
    } else if (key == "searchSuggestions" && data->GetType("value") == VTYPE_BOOL) {
      next.search_suggestions = data->GetBool("value"); valid = true;
    } else if (key == "siteSearches" && data->GetType("value") == VTYPE_LIST) {
      valid = ReadSiteSearches(data->GetList("value"), next.site_searches);
    } else if (key == "siteSearchActivation" && data->GetType("value") == VTYPE_STRING && ValidSiteSearchActivation(data->GetString("value"))) {
      next.site_search_activation = data->GetString("value"); valid = true;
      } else if (key == "notesProvider" && data->GetType("value") == VTYPE_STRING &&
          (data->GetString("value") == "googleDocs" || data->GetString("value") == "word")) {
        next.notes_provider = data->GetString("value"); valid = true;
    } else if (key == "unloadMinutes" && data->GetType("value") == VTYPE_INT && ValidUnloadMinutes(data->GetInt("value"))) {
      next.unload_minutes = data->GetInt("value"); valid = true;
    } else if (key == "pipButton" && data->GetType("value") == VTYPE_BOOL) {
      next.pip_button = data->GetBool("value"); valid = true;
    } else if (key == "littleSolLinks" && data->GetType("value") == VTYPE_BOOL) {
      next.little_sol_links = data->GetBool("value"); valid = true;
    } else if (key == "littleSolClick" && data->GetType("value") == VTYPE_BOOL) {
      next.little_sol_click = data->GetBool("value"); valid = true;
    } else if (key == "littleSolHotkey" && data->GetType("value") == VTYPE_BOOL) {
      next.little_sol_hotkey = data->GetBool("value"); valid = true;
    } else if (key == "autoPictureInPicture" && data->GetType("value") == VTYPE_BOOL) {
      next.auto_pip = data->GetBool("value"); valid = true;
    } else if (key == "autoArchiveHours" && data->GetType("value")==VTYPE_INT && ValidAutoArchiveHours(data->GetInt("value"))) {
      next.auto_archive_hours=data->GetInt("value"); valid=true;
    } else if (key == "clock" && data->GetType("value") == VTYPE_STRING && ValidClock(data->GetString("value"))) {
      next.clock = data->GetString("value"); valid = true;
    } else if (key == "backdrop" && data->GetType("value") == VTYPE_STRING && ValidBackdrop(data->GetString("value"))) {
      next.backdrop = data->GetString("value"); valid = true;
    } else if (key == "startup" && data->GetType("value") == VTYPE_STRING) {
      const auto mode = data->GetString("value").ToString();
      if (mode == "restore" || mode == "newTab" || mode == "homepage") { next.startup = mode; valid = true; }
    } else if (key == "homepage" && data->GetType("value") == VTYPE_STRING && data->GetString("value").length() <= 4096) {
      const auto url = data->GetString("value").ToString();
      if (ValidHomepage(url)) { next.homepage = url; valid = true; }
    }
    if (!valid) callback->Failure(400,"Invalid preference value. Homepage must be an HTTP(S) address without credentials.");
    else SavePreferences(std::move(next),callback);
    return true;
  }
  if ((method == "settings.chooseDownloadFolder" || method == "settings.resetDownloadFolder") && data->GetSize() == 1 && state->session && state->session->preferences) {
    if (method == "settings.resetDownloadFolder") {
      auto next = state->session->preferences->Get(); next.download_directory.clear(); SavePreferences(std::move(next),callback);
    } else if (browser_) {
      const auto& directory = state->session->preferences->Get().download_directory;
      browser_->GetHost()->RunFileDialog(FILE_DIALOG_OPEN_FOLDER,"Download location",
          (directory.empty() ? DownloadManager::DefaultDirectory() : directory).wstring(),{},
          new FolderPicked([weak = weak_from_this(),callback](const std::vector<CefString>& paths) {
            const auto owner = weak.lock(); const auto state = owner ? owner->state_.lock() : nullptr;
            if (!owner || !state || !state->session || !state->session->preferences) { callback->Failure(410,"Settings closed"); return; }
            if (paths.empty()) { callback->Success(owner->Response()); return; }
            const std::filesystem::path directory = paths.front().ToWString();
            std::error_code error;
            if (paths.size() != 1 || !directory.is_absolute() || !std::filesystem::is_directory(directory,error)) { callback->Failure(400,"Choose an existing folder"); return; }
            auto next = state->session->preferences->Get(); next.download_directory = directory;
            owner->SavePreferences(std::move(next),callback);
          }));
    } else callback->Failure(409,"Settings browser is not ready");
    return true;
  }
  if ((method == "settings.shortcut" || method == "settings.resetShortcuts") && state->session && state->session->preferences) {
    auto next = state->session->preferences->Get();
    if (method == "settings.resetShortcuts" && data->GetSize() == 1) next.shortcuts.clear();
    else if (method == "settings.shortcut" && data->GetSize() == 3 && data->GetType("action") == VTYPE_STRING) {
      KeyBinding key; const KeyBinding* binding = nullptr;
      if (data->GetType("binding") == VTYPE_DICTIONARY) {
        const auto chord = data->GetDictionary("binding");
        if (chord->GetSize() != 4 || chord->GetType("key") != VTYPE_INT || chord->GetType("ctrl") != VTYPE_BOOL ||
            chord->GetType("shift") != VTYPE_BOOL || chord->GetType("alt") != VTYPE_BOOL) { callback->Failure(400,"Invalid shortcut"); return true; }
        key = {chord->GetInt("key"),chord->GetBool("ctrl"),chord->GetBool("shift"),chord->GetBool("alt")}; binding = &key;
      } else if (data->GetType("binding") != VTYPE_NULL) { callback->Failure(400,"Invalid shortcut"); return true; }
      std::string error;
      if (!ConfigureShortcut(next,data->GetString("action"),binding,error)) { callback->Failure(409,error); return true; }
    } else { callback->Failure(400,"Invalid shortcut request"); return true; }
    SavePreferences(std::move(next),callback); return true;
  }
  if (method == "settings.ready" && data->GetSize() == 1 && window_) {
    ready_ = true; callback->Success(Response()); Present(); ShowSection(section_); return true;
  }
  if (method.starts_with("import.") && window_)
    return HandleImportQuery(state, window_->GetWindowHandle(), import_, method, data, callback);
  if (method == "settings.makeDefault" && data->GetSize() == 1 && window_) {
    std::string error;
    if (!OpenDefaultBrowserSettings(window_->GetWindowHandle(), error)) callback->Failure(500, error);
    else { callback->Success(Response(true)); state->Record("settings_default_apps_opened"); }
    return true;
  }
  if (method == "profile.create" || method == "profile.rename") {
    auto command = data->Copy(false);
    command->SetDouble("expectedRevision",state->workspace->Snapshot()->GetDouble("treeRevision"));
    state->workspace->Handle(command,[weak=weak_from_this(),callback](int code,const std::string& error,CefRefPtr<CefDictionaryValue>) {
      if (code) { callback->Failure(code,error); return; }
      if (const auto owner=weak.lock()) callback->Success(owner->Response());
      else callback->Failure(410,"Settings closed");
    });
    return true;
  }
  if (method == "workspace.setTheme") {
    Theme theme; bool save = false;
    if (!WorkspaceController::ParseThemeCommand(data, theme, save)) { callback->Failure(400, "Invalid theme"); return true; }
    state->workspace->SetTheme(theme, save, [weak = weak_from_this(), callback](int code, const std::string& error, CefRefPtr<CefDictionaryValue>) {
      const auto owner = weak.lock(); if (!owner) { callback->Failure(410, "Settings closed"); return; }
      if (code) callback->Failure(code, error); else callback->Success(owner->Response());
    },data->GetString("spaceId").ToString());
    return true;
  }
  // What's new: the website's notes for this version, in a new tab of this window.
  if (method == "settings.whatsNew" && data->GetSize() == 1) {
    if (!state->workspace || state->workspace->WindowClosing()) { callback->Failure(409, "Browser is closing"); return true; }
    state->workspace->OpenLink(ReleaseNotesUrl(RED_APP_VERSION), true);
    if (state->window) state->window->Activate();
    callback->Success(Response()); return true;
  }
  if (method == "settings.extensions" && data->GetSize() == 1) {
    if (!state->extensions_window) state->extensions_window = std::make_shared<ExtensionsWindow>(state);
    state->extensions_window->Show(); callback->Success(Response()); return true;
  }
  callback->Failure(400, "Unsupported settings command"); return true;
}
}  // namespace red
