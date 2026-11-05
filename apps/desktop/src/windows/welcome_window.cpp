#include "windows/welcome_window.h"
#include <dwmapi.h>
#include "app/session.h"
#include "app/preferences.h"
#include "import/browser_import.h"
#include "trust_policy.h"
#include "windows/branding.h"
#include "windows/browser_icons.h"
#include "windows/default_browser.h"
#include "windows/welcome_music.h"
#include "windows/window_corners.h"
#include "workspace/controller.h"
#include "include/base/cef_logging.h"
#include "include/cef_parser.h"
#include "include/views/cef_display.h"
#include "include/views/cef_fill_layout.h"
#include "include/views/cef_window.h"

namespace red {
namespace {
constexpr int kWidth = 1120, kHeight = 720;

class WelcomeClient final : public CefClient, public CefLifeSpanHandler, public CefRequestHandler, public CefDragHandler,
    public CefMessageRouterBrowserSide::Handler {
 public:
  explicit WelcomeClient(std::weak_ptr<WelcomeWindow> owner) : owner_(owner) {
    router_ = CefMessageRouterBrowserSide::Create(CefMessageRouterConfig()); router_->AddHandler(this, false);
  }
  ~WelcomeClient() override { router_->RemoveHandler(this); }
  CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return this; }
  CefRefPtr<CefRequestHandler> GetRequestHandler() override { return this; }
  CefRefPtr<CefDragHandler> GetDragHandler() override { return this; }
  void OnAfterCreated(CefRefPtr<CefBrowser> browser) override { if (auto owner = owner_.lock()) owner->BrowserCreated(browser); }
  void OnBeforeClose(CefRefPtr<CefBrowser> browser) override {
    router_->OnBeforeClose(browser); if (auto owner = owner_.lock()) owner->BrowserClosed(browser->GetIdentifier());
  }
  bool OnBeforeBrowse(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, CefRefPtr<CefRequest> request, bool, bool) override {
    router_->OnBeforeBrowse(browser, frame); return !frame->IsMain() || request->GetURL().ToString() != kWelcomeUrl;
  }
  bool OnBeforePopup(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, int, const CefString&, const CefString&,
      cef_window_open_disposition_t, bool, const CefPopupFeatures&, CefWindowInfo&, CefRefPtr<CefClient>&,
      CefBrowserSettings&, CefRefPtr<CefDictionaryValue>&, bool*) override { return true; }
  void OnRenderProcessTerminated(CefRefPtr<CefBrowser> browser, TerminationStatus, int, const CefString&) override {
    router_->OnRenderProcessTerminated(browser); if (auto owner = owner_.lock()) owner->Finish();
  }
  bool OnProcessMessageReceived(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, CefProcessId source,
      CefRefPtr<CefProcessMessage> message) override { return router_->OnProcessMessageReceived(browser, frame, source, message); }
  bool OnQuery(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, int64_t, const CefString& request,
      bool persistent, CefRefPtr<Callback> callback) override {
    if (auto owner = owner_.lock()) return owner->Query(browser, frame, request, persistent, callback);
    callback->Failure(410, "Welcome closed"); return true;
  }
  // Frameless: only the welcome page's own main frame says where it can be dragged.
  void OnDraggableRegionsChanged(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame> frame, const std::vector<CefDraggableRegion>& regions) override {
    const auto owner = owner_.lock();
    if (owner && owner->window() && frame->IsMain() && frame->GetURL().ToString() == kWelcomeUrl) owner->window()->SetDraggableRegions(regions);
  }
 private:
  std::weak_ptr<WelcomeWindow> owner_;
  CefRefPtr<CefMessageRouterBrowserSide> router_;
  IMPLEMENT_REFCOUNTING(WelcomeClient);
};

class WelcomeView final : public CefBrowserViewDelegate {
 public: cef_runtime_style_t GetBrowserRuntimeStyle() override { return CEF_RUNTIME_STYLE_ALLOY; }
 private: IMPLEMENT_REFCOUNTING(WelcomeView);
};

class WelcomeDelegate final : public CefWindowDelegate {
 public:
  WelcomeDelegate(std::weak_ptr<WelcomeWindow> owner, CefRefPtr<CefBrowserView> view) : owner_(owner), view_(view) {}
  cef_runtime_style_t GetWindowRuntimeStyle() override { return CEF_RUNTIME_STYLE_ALLOY; }
  bool IsFrameless(CefRefPtr<CefWindow>) override { return true; }
  bool CanResize(CefRefPtr<CefWindow>) override { return false; }
  bool CanMaximize(CefRefPtr<CefWindow>) override { return false; }
  CefSize GetPreferredSize(CefRefPtr<CefView>) override { return CefSize(kWidth, kHeight); }
  void OnWindowCreated(CefRefPtr<CefWindow> window) override {
    window->SetTitle("Welcome to Sol");
    window->SetBackgroundColor(CefColorSetARGB(255, 3, 2, 4));
    const auto icon = RedWindowIcon(); window->SetWindowIcon(icon); window->SetWindowAppIcon(icon);
    const BOOL dark = TRUE;
    DwmSetWindowAttribute(window->GetWindowHandle(), DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
    ApplyWindowCorners(window);
    window->SetToFillLayout(); window->AddChildView(view_);
    // Centred on the screen the person is looking at: the one under the pointer.
    POINT cursor{};
    const auto display = GetCursorPos(&cursor) ? CefDisplay::GetDisplayNearestPoint(CefPoint(cursor.x, cursor.y), true) : CefDisplay::GetPrimaryDisplay();
    if (display) {
      const auto area = display->GetWorkArea();
      window->SetBounds(CefRect(area.x + (area.width - kWidth) / 2, area.y + (area.height - kHeight) / 2, kWidth, kHeight));
    } else {
      window->CenterWindow(CefSize(kWidth, kHeight));
    }
    window->Show(); window->Activate();
    if (auto owner = owner_.lock()) owner->Created(window);
  }
  // Closing the welcome (Alt+F4, the taskbar) opens the browser rather than ending Sol.
  bool CanClose(CefRefPtr<CefWindow>) override {
    if (auto owner = owner_.lock()) owner->Finish();
    const auto browser = view_ ? view_->GetBrowser() : nullptr;
    return !browser || browser->GetHost()->TryCloseBrowser();
  }
  void OnWindowDestroyed(CefRefPtr<CefWindow>) override { view_ = nullptr; if (auto owner = owner_.lock()) owner->Destroyed(); }
 private:
  std::weak_ptr<WelcomeWindow> owner_;
  CefRefPtr<CefBrowserView> view_;
  IMPLEMENT_REFCOUNTING(WelcomeDelegate);
};
}  // namespace

void ShowWelcome(const std::shared_ptr<State>& state) {
  if (!state->welcome_window) state->welcome_window = std::make_shared<WelcomeWindow>(state);
  state->welcome_window->Show();
}

void WelcomeWindow::Show() {
  const auto state = state_.lock();
  if (!state || !state->ui_context || window_) return;
  CefBrowserSettings settings; settings.background_color = CefColorSetARGB(255, 3, 2, 4);
  auto extra = CefDictionaryValue::Create(); extra->SetBool("trusted_ui", true);
  auto view = CefBrowserView::CreateBrowserView(new WelcomeClient(weak_from_this()), std::string(kWelcomeUrl),
      settings, extra, state->ui_context, new WelcomeView());
  view->SetBackgroundColor(settings.background_color);
  CefWindow::CreateTopLevelWindow(new WelcomeDelegate(weak_from_this(), view));
}

void WelcomeWindow::BrowserCreated(CefRefPtr<CefBrowser> browser) {
  browser_ = browser;
  if (const auto state = state_.lock()) {
    state->welcome_id = browser->GetIdentifier(); RegisterSessionUi(state, browser->GetIdentifier());
    state->Record("welcome_created");
  }
}

void WelcomeWindow::BrowserClosed(int id) {
  browser_ = nullptr;
  if (const auto state = state_.lock()) { UnregisterSessionUi(state, id); state->welcome_id = -1; }
}

void WelcomeWindow::Destroyed() {
  window_ = nullptr;
  // Anything read from another browser and not imported is dropped, passwords included.
  CancelBrowserImport(import_); import_.reset();
  Finish();
}

void WelcomeWindow::Finish() {
  if (finished_) return;
  finished_ = true;
  WelcomeMusic::Stop();
  if (window_) window_->Close();
  if (const auto state = state_.lock(); state && state->window) {
    state->window->Show();
    state->window->Activate();
    if (state->workspace) state->workspace->Refresh();
    state->Record("welcome_finished");
  }
}

std::string WelcomeWindow::Response(bool refresh_default) const {
  auto response = CefDictionaryValue::Create();
  const auto state = state_.lock();
  response->SetBool("music", WelcomeMusic::Available());
  if (refresh_default || !default_status_) default_status_ = DefaultBrowserStatus();
  response->SetDictionary("defaultBrowser", default_status_->Copy(false));
  const auto preferences = state && state->session ? state->session->preferences : nullptr;
  response->SetString("searchEngine", preferences ? preferences->Get().search_engine : "google");
  response->SetBool("ready", state && state->workspace && state->workspace->ready());
  auto value = CefValue::Create(); value->SetDictionary(response);
  return CefWriteJSON(value, JSON_WRITER_DEFAULT);
}

bool WelcomeWindow::Query(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, const CefString& raw,
    bool persistent, CefRefPtr<CefMessageRouterBrowserSide::Callback> callback) {
  const auto state = state_.lock();
  if (!state || state->welcome_id.load() != browser->GetIdentifier() || !frame->IsMain() || frame->GetURL().ToString() != kWelcomeUrl) {
    callback->Failure(403, "Untrusted welcome document"); return true;
  }
  if (persistent || raw.length() > 8192) { callback->Failure(400, "Invalid welcome request"); return true; }
  const auto value = CefParseJSON(raw, JSON_PARSER_RFC);
  if (!value || value->GetType() != VTYPE_DICTIONARY || value->GetDictionary()->GetType("method") != VTYPE_STRING) {
    callback->Failure(400, "Invalid welcome request"); return true;
  }
  const auto data = value->GetDictionary();
  const auto method = data->GetString("method").ToString();
  const auto text = [&](const char* key) { return data->GetType(key) == VTYPE_STRING ? data->GetString(key).ToString() : std::string(); };
  if (method == "welcome.state" && data->GetSize() == 1) { callback->Success(Response(true)); return true; }
  if (method == "welcome.music" && data->GetSize() == 2) {
    const auto action = text("action");
    if (action == "play") WelcomeMusic::Play();
    else if (action == "stop") WelcomeMusic::Stop();
    else if (action == "mute" || action == "unmute") WelcomeMusic::SetMuted(action == "mute");
    else { callback->Failure(400, "Invalid music action"); return true; }
    callback->Success(Response(false)); return true;
  }
  if (method == "welcome.window" && data->GetSize() == 2) {
    const auto action = text("action");
    if (action == "minimize" && window_) window_->Minimize();
    else if (action == "close") Finish();
    else { callback->Failure(400, "Invalid window action"); return true; }
    callback->Success("{}"); return true;
  }
  if (method == "welcome.searchEngine" && data->GetSize() == 2 && ValidSearchEngine(text("value")) && state->session && state->session->preferences) {
    auto next = state->session->preferences->Get();
    next.search_engine = text("value");
    state->session->preferences->Save(std::move(next), [weak = weak_from_this(), callback](const std::string& error) {
      const auto owner = weak.lock();
      if (!owner) { callback->Failure(410, "Welcome closed"); return; }
      if (!error.empty()) { callback->Failure(409, error); return; }
      if (const auto state = owner->state_.lock(); state && state->session)
        for (const auto& window : state->session->Windows()) if (window->workspace) window->workspace->Refresh();
      callback->Success(owner->Response(false));
    });
    return true;
  }
  if (method == "welcome.makeDefault" && data->GetSize() == 1 && window_) {
    std::string error;
    if (!OpenDefaultBrowserSettings(window_->GetWindowHandle(), error)) callback->Failure(500, error);
    else callback->Success(Response(true));
    return true;
  }
  if (method == "welcome.browserIcons" && data->GetSize() == 1) {
    auto result = CefDictionaryValue::Create(); result->SetDictionary("icons", InstalledBrowserIcons(96));
    auto json = CefValue::Create(); json->SetDictionary(result);
    callback->Success(CefWriteJSON(json, JSON_WRITER_DEFAULT)); return true;
  }
  if (method == "welcome.finish" && data->GetSize() == 1) { callback->Success("{}"); Finish(); return true; }
  if (method.starts_with("import.") && window_) {
    if (!state->workspace || !state->workspace->ready()) { callback->Failure(409, "Sol is still starting"); return true; }
    return HandleImportQuery(state, window_->GetWindowHandle(), import_, method, data, callback);
  }
  callback->Failure(400, "Unsupported welcome command"); return true;
}
}  // namespace red
