#include "windows/update_window.h"
#include <dwmapi.h>
#include "app/session.h"
#include "app/updates.h"
#include "include/cef_parser.h"
#include "include/views/cef_fill_layout.h"
#include "trust_policy.h"
#include "windows/branding.h"
#include "windows/focus.h"
#include "windows/ui_context_menu.h"
#include "windows/window_close.h"
#include "workspace/controller.h"

namespace red {
namespace {
constexpr int kWidth = 436, kHeight = 400;  // The web installer's 420 x 360 mark and text, plus the frame.
const cef_color_t kBackground = CefColorSetARGB(255, 14, 12, 11);

const char* PhaseName(UpdatePhase phase) {
  switch (phase) {
    case UpdatePhase::Idle: return "idle";
    case UpdatePhase::Checking: return "checking";
    case UpdatePhase::Downloading: return "downloading";
    case UpdatePhase::Ready: return "ready";
    case UpdatePhase::UpToDate: return "upToDate";
    case UpdatePhase::Failed: return "failed";
    case UpdatePhase::NotInstalled: return "notInstalled";
  }
  return "idle";
}

class UpdateClient final : public CefClient, public CefLifeSpanHandler, public CefRequestHandler,
    public CefMessageRouterBrowserSide::Handler {
 public:
  explicit UpdateClient(std::weak_ptr<UpdateWindow> owner) : owner_(owner) {
    router_ = CefMessageRouterBrowserSide::Create(CefMessageRouterConfig()); router_->AddHandler(this, false);
  }
  ~UpdateClient() override { router_->RemoveHandler(this); }
  CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return this; }
  CefRefPtr<CefContextMenuHandler> GetContextMenuHandler() override { return new UiContextMenu(std::weak_ptr<State>()); }
  CefRefPtr<CefRequestHandler> GetRequestHandler() override { return this; }
  void OnAfterCreated(CefRefPtr<CefBrowser> browser) override { if (auto owner = owner_.lock()) owner->BrowserCreated(browser); }
  void OnBeforeClose(CefRefPtr<CefBrowser> browser) override {
    router_->OnBeforeClose(browser); if (auto owner = owner_.lock()) owner->BrowserClosed(browser->GetIdentifier());
  }
  bool OnBeforeBrowse(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, CefRefPtr<CefRequest> request, bool, bool) override {
    router_->OnBeforeBrowse(browser, frame); return !frame->IsMain() || request->GetURL().ToString() != kUpdateUrl;
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
    callback->Failure(410, "Update window closed"); return true;
  }
 private:
  std::weak_ptr<UpdateWindow> owner_;
  CefRefPtr<CefMessageRouterBrowserSide> router_;
  IMPLEMENT_REFCOUNTING(UpdateClient);
};
class UpdateView final : public CefBrowserViewDelegate {
 public: cef_runtime_style_t GetBrowserRuntimeStyle() override { return CEF_RUNTIME_STYLE_ALLOY; }
 private: IMPLEMENT_REFCOUNTING(UpdateView);
};
class UpdateDelegate final : public CefWindowDelegate {
 public:
  UpdateDelegate(std::weak_ptr<UpdateWindow> owner, CefRefPtr<CefBrowserView> view) : owner_(owner), view_(view) {}
  cef_runtime_style_t GetWindowRuntimeStyle() override { return CEF_RUNTIME_STYLE_ALLOY; }
  CefSize GetPreferredSize(CefRefPtr<CefView>) override { return CefSize(kWidth, kHeight); }
  CefSize GetMinimumSize(CefRefPtr<CefView>) override { return CefSize(kWidth, kHeight); }
  bool CanResize(CefRefPtr<CefWindow>) override { return false; }
  bool CanMaximize(CefRefPtr<CefWindow>) override { return false; }
  bool CanMinimize(CefRefPtr<CefWindow>) override { return false; }
  CefRefPtr<CefWindow> GetParentWindow(CefRefPtr<CefWindow>, bool* is_menu, bool*) override {
    *is_menu = false;  // Owned: stays above the browser window.
    if (const auto owner = owner_.lock()) return owner->parent();
    return nullptr;
  }
  void OnWindowCreated(CefRefPtr<CefWindow> window) override {
    window->SetBackgroundColor(kBackground);
    const auto icon = RedWindowIcon(); window->SetWindowIcon(icon); window->SetWindowAppIcon(icon);
    const BOOL dark = TRUE;
    DwmSetWindowAttribute(window->GetWindowHandle(), DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
    const COLORREF caption = RGB(27, 23, 20);
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
  std::weak_ptr<UpdateWindow> owner_; CefRefPtr<CefBrowserView> view_;
  IMPLEMENT_REFCOUNTING(UpdateDelegate);
};
}  // namespace

void ShowUpdateWindow(const std::shared_ptr<State>& state) {
  if (!state->update_window) state->update_window = std::make_shared<UpdateWindow>(state);
  state->update_window->Show();
}

void UpdateWindow::Show() {
  const auto state = state_.lock();
  if (!state || !state->window || !state->ui_context || !state->workspace || state->workspace->WindowClosing()) return;
  StartUpdateCheckNow(state);
  if (window_) { Present(); return; }
  if (creating_) return;
  creating_ = true;
  CefBrowserSettings settings; settings.background_color = kBackground;
  auto extra = CefDictionaryValue::Create(); extra->SetBool("trusted_ui", true);
  auto view = CefBrowserView::CreateBrowserView(new UpdateClient(weak_from_this()), std::string(kUpdateUrl),
      settings, extra, state->ui_context, new UpdateView());
  view->SetBackgroundColor(settings.background_color);
  CefWindow::CreateTopLevelWindow(new UpdateDelegate(weak_from_this(), view));
}

CefRefPtr<CefWindow> UpdateWindow::parent() const {
  const auto state = state_.lock(); return state ? state->window : nullptr;
}

void UpdateWindow::Created(CefRefPtr<CefWindow> window) {
  window_ = window;
  window->SetTitle("Check for Updates — Sol");
  if (const auto owner = parent()) {
    const auto area = owner->GetBounds();
    window->SetBounds(CefRect(area.x + (area.width - kWidth) / 2, area.y + (area.height - kHeight) / 3, kWidth, kHeight));
  } else window->CenterWindow(CefSize(kWidth, kHeight));
}

void UpdateWindow::Present() {
  if (!window_ || !ready_) return;
  window_->Show(); window_->Activate();
  if (browser_) browser_->GetHost()->SetFocus(true);
}

void UpdateWindow::BrowserCreated(CefRefPtr<CefBrowser> browser) {
  browser_ = browser; creating_ = false;
  if (const auto state = state_.lock()) {
    state->update_id = browser->GetIdentifier(); RegisterSessionUi(state, browser->GetIdentifier());
    state->browsers[browser->GetIdentifier()] = browser; state->Record("update_window_created");
    if (!state->window || (state->workspace && state->workspace->WindowClosing())) browser->GetHost()->CloseBrowser(true);
  }
}

void UpdateWindow::BrowserClosed(int id) {
  browser_ = nullptr; ready_ = creating_ = false;
  if (const auto state = state_.lock()) {
    UnregisterSessionUi(state, id); state->browsers.erase(id); state->update_id = -1;
    if (state->window_close) state->window_close->OnClosed(id);
    if (!state->window && state->browsers.empty()) QuitIfLastWindow(state);
  }
}

void UpdateWindow::Close() { if (window_) window_->Close(); }

std::string UpdateWindow::Response() const {
  const auto status = CurrentUpdateStatus();
  auto response = CefDictionaryValue::Create();
  response->SetString("phase", PhaseName(status.phase));
  response->SetString("current", status.current);
  response->SetString("version", status.version);
  response->SetInt("percent", status.percent);
  auto value = CefValue::Create(); value->SetDictionary(response);
  return CefWriteJSON(value, JSON_WRITER_DEFAULT);
}

bool UpdateWindow::Query(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, const CefString& raw,
    bool persistent, CefRefPtr<CefMessageRouterBrowserSide::Callback> callback) {
  const auto state = state_.lock();
  if (!state || !IsUpdateUi(browser->GetIdentifier(), state->update_id, frame->IsMain(), frame->GetURL().ToString())) {
    callback->Failure(403, "Untrusted update document"); return true;
  }
  if (persistent || raw.length() > 256) { callback->Failure(400, "Invalid update request"); return true; }
  const auto value = CefParseJSON(raw, JSON_PARSER_RFC);
  if (!value || value->GetType() != VTYPE_DICTIONARY || value->GetDictionary()->GetType("method") != VTYPE_STRING) {
    callback->Failure(400, "Invalid update request"); return true;
  }
  const auto data = value->GetDictionary(); const auto method = data->GetString("method").ToString();
  if (HandleUiFocus(state, browser, method, data->GetSize(), persistent, callback)) return true;
  if (!state->workspace || state->workspace->WindowClosing()) { callback->Failure(409, "Browser is closing"); return true; }
  if (data->GetSize() != 1) { callback->Failure(400, "Invalid update request"); return true; }
  if (method == "update.ready" && window_) { ready_ = true; callback->Success(Response()); Present(); return true; }
  if (method == "update.state") { callback->Success(Response()); return true; }
  if (method == "update.check") { StartUpdateCheckNow(state); callback->Success(Response()); return true; }
  if (method == "update.restart") {
    if (!RequestUpdateRestart(state)) { callback->Failure(409, "No update is waiting"); return true; }
    callback->Success(Response()); return true;
  }
  if (method == "update.close") { callback->Success(Response()); Close(); return true; }
  callback->Failure(400, "Unsupported update command"); return true;
}
}  // namespace red
