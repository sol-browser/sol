#include "windows/devtools_window.h"
#include <dwmapi.h>
#include <algorithm>
#include <cmath>
#include "app/session.h"
#include "include/base/cef_callback.h"
#include "include/cef_parser.h"
#include "include/views/cef_browser_view.h"
#include "include/views/cef_display.h"
#include "include/views/cef_fill_layout.h"
#include "include/wrapper/cef_closure_task.h"
#include "trust_policy.h"
#include "windows/branding.h"
#include "windows/devtools_dock.h"
#include "windows/focus.h"
#include "windows/ui_context_menu.h"
#include "windows/window_close.h"
#include "workspace/controller.h"

namespace red {
namespace {
constexpr int kHeader = 32;  // DIP: the placement strip above the frontend.
const cef_color_t kBackground = CefColorSetARGB(255, 40, 40, 40);  // .devtools-header

class HeaderClient final : public CefClient, public CefLifeSpanHandler, public CefRequestHandler,
    public CefMessageRouterBrowserSide::Handler {
 public:
  HeaderClient(std::weak_ptr<DevToolsWindow> owner, std::weak_ptr<State> state) : owner_(owner), state_(state) {
    router_ = CefMessageRouterBrowserSide::Create(CefMessageRouterConfig()); router_->AddHandler(this, false);
  }
  ~HeaderClient() override { router_->RemoveHandler(this); }
  CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return this; }
  CefRefPtr<CefContextMenuHandler> GetContextMenuHandler() override { return new UiContextMenu(std::weak_ptr<State>()); }
  CefRefPtr<CefRequestHandler> GetRequestHandler() override { return this; }
  void OnAfterCreated(CefRefPtr<CefBrowser> browser) override {
    if (auto state = state_.lock()) {
      state->browsers[browser->GetIdentifier()] = browser; RegisterSessionUi(state, browser->GetIdentifier());
      state->SetDevToolsWindowUi(browser->GetIdentifier(), true);
    }
    if (auto owner = owner_.lock()) owner->HeaderCreated(browser); else browser->GetHost()->CloseBrowser(true);
  }
  void OnBeforeClose(CefRefPtr<CefBrowser> browser) override {
    router_->OnBeforeClose(browser);
    const int id = browser->GetIdentifier();
    if (auto state = state_.lock()) {
      state->SetDevToolsWindowUi(id, false);
      UnregisterSessionUi(state, id); state->browsers.erase(id);
      if (state->window_close) state->window_close->OnClosed(id);
    }
    if (auto owner = owner_.lock()) owner->HeaderClosed(id);
    if (auto state = state_.lock(); state && !state->window && state->browsers.empty()) QuitIfLastWindow(state);
  }
  bool OnBeforeBrowse(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, CefRefPtr<CefRequest> request, bool, bool) override {
    router_->OnBeforeBrowse(browser, frame);
    return !frame->IsMain() || request->GetURL().ToString() != kDevToolsWindowUrl;
  }
  bool OnBeforePopup(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, int, const CefString&, const CefString&,
      cef_window_open_disposition_t, bool, const CefPopupFeatures&, CefWindowInfo&, CefRefPtr<CefClient>&,
      CefBrowserSettings&, CefRefPtr<CefDictionaryValue>&, bool*) override { return true; }
  void OnRenderProcessTerminated(CefRefPtr<CefBrowser> browser, TerminationStatus, int, const CefString&) override {
    router_->OnRenderProcessTerminated(browser);
    if (auto owner = owner_.lock()) owner->Close();
  }
  bool OnProcessMessageReceived(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, CefProcessId source,
      CefRefPtr<CefProcessMessage> message) override { return router_->OnProcessMessageReceived(browser, frame, source, message); }
  bool OnQuery(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, int64_t query, const CefString& request,
      bool persistent, CefRefPtr<Callback> callback) override {
    if (auto owner = owner_.lock()) return owner->Query(browser, frame, query, request, persistent, callback);
    callback->Failure(410, "Developer tools window closed"); return true;
  }
  void OnQueryCanceled(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, int64_t query) override {
    if (auto owner = owner_.lock()) owner->Cancel(query);
  }
 private:
  std::weak_ptr<DevToolsWindow> owner_;
  std::weak_ptr<State> state_;
  CefRefPtr<CefMessageRouterBrowserSide> router_;
  IMPLEMENT_REFCOUNTING(HeaderClient);
};
class HeaderView final : public CefBrowserViewDelegate {
 public: cef_runtime_style_t GetBrowserRuntimeStyle() override { return CEF_RUNTIME_STYLE_ALLOY; }
 private: IMPLEMENT_REFCOUNTING(HeaderView);
};
class WindowDelegate final : public CefWindowDelegate {
 public:
  WindowDelegate(std::weak_ptr<DevToolsWindow> owner, CefRefPtr<CefBrowserView> view, CefRect bounds)
      : owner_(owner), view_(view), bounds_(bounds) {}
  cef_runtime_style_t GetWindowRuntimeStyle() override { return CEF_RUNTIME_STYLE_ALLOY; }
  CefRect GetInitialBounds(CefRefPtr<CefWindow>) override { return bounds_; }
  CefSize GetMinimumSize(CefRefPtr<CefView>) override { return CefSize(420, 260); }
  // An independent window, like Chrome's own undocked DevTools: not owned by
  // the browser window, so it can sit on another display or behind it.
  CefRefPtr<CefWindow> GetParentWindow(CefRefPtr<CefWindow>, bool* is_menu, bool*) override { *is_menu = false; return nullptr; }
  void OnWindowCreated(CefRefPtr<CefWindow> window) override {
    window->SetBackgroundColor(kBackground);
    const auto icon = RedWindowIcon(); window->SetWindowIcon(icon); window->SetWindowAppIcon(icon);
    const BOOL dark = TRUE;
    DwmSetWindowAttribute(window->GetWindowHandle(), DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
    const COLORREF caption = RGB(40, 40, 40);
    DwmSetWindowAttribute(window->GetWindowHandle(), DWMWA_CAPTION_COLOR, &caption, sizeof(caption));
    window->SetToFillLayout(); window->AddChildView(view_);
    if (auto owner = owner_.lock()) owner->Created(window);
    window->Show();
  }
  void OnWindowBoundsChanged(CefRefPtr<CefWindow>, const CefRect& bounds) override {
    if (auto owner = owner_.lock()) owner->BoundsChanged(bounds);
  }
  bool CanClose(CefRefPtr<CefWindow>) override {
    if (auto owner = owner_.lock()) return owner->CanClose();
    const auto browser = view_ ? view_->GetBrowser() : nullptr;
    return !browser || browser->GetHost()->TryCloseBrowser();
  }
  void OnWindowDestroyed(CefRefPtr<CefWindow>) override { view_ = nullptr; if (auto owner = owner_.lock()) owner->Destroyed(); }
 private:
  std::weak_ptr<DevToolsWindow> owner_; CefRefPtr<CefBrowserView> view_; CefRect bounds_;
  IMPLEMENT_REFCOUNTING(WindowDelegate);
};
std::string HostOf(const std::string& url) {
  CefURLParts parts;
  if (!CefParseURL(url, parts)) return "";
  return CefString(&parts.host).ToString();
}
}  // namespace

void DevToolsWindow::Open() {
  const auto state = state_.lock();
  if (window_ || creating_ || !state || !state->ui_context) return;
  creating_ = true;
  CefBrowserSettings settings; settings.background_color = kBackground;
  auto extra = CefDictionaryValue::Create(); extra->SetBool("trusted_ui", true);
  auto view = CefBrowserView::CreateBrowserView(new HeaderClient(weak_from_this(), state_), std::string(kDevToolsWindowUrl),
      settings, extra, state->ui_context, new HeaderView());
  view->SetBackgroundColor(kBackground);
  CefWindow::CreateTopLevelWindow(new WindowDelegate(weak_from_this(), view, initial_));
}

HWND DevToolsWindow::hwnd() const { return window_ ? window_->GetWindowHandle() : nullptr; }

CefRect DevToolsWindow::FrontendBounds() const {
  const HWND handle = hwnd();
  RECT client{};
  if (!handle || !GetClientRect(handle, &client)) return CefRect(0, 0, 1, 1);
  const float scale = window_->GetDisplay() ? window_->GetDisplay()->GetDeviceScaleFactor() : 1.f;
  const int header = std::min(static_cast<int>(client.bottom), static_cast<int>(std::lround(kHeader * scale)));
  return CefRect(0, header, std::max(1, static_cast<int>(client.right)), std::max(1, static_cast<int>(client.bottom) - header));
}

void DevToolsWindow::Created(CefRefPtr<CefWindow> window) {
  window_ = window; creating_ = false; ready_ = true;
  if (const auto state = state_.lock()) {
    std::string title = "Developer tools";
    if (const auto page = state->browsers.find(target_); page != state->browsers.end())
      if (const auto host = HostOf(page->second->GetMainFrame()->GetURL().ToString()); !host.empty()) title += " — " + host;
    window->SetTitle(title);
    state->Record("devtools_window_created");
  }
  // The frontend is created after this callback returns, never inside window creation.
  if (auto ready = std::move(ready_callback_))
    CefPostTask(TID_UI, base::BindOnce([](std::function<void()> ready) { ready(); }, std::move(ready)));
}

void DevToolsWindow::Attach(CefRefPtr<CefBrowser> frontend) {
  frontend_ = frontend;
  Layout();
  if (window_) { window_->Show(); window_->Activate(); }
  frontend->GetHost()->SetFocus(true);
  Publish();
}

void DevToolsWindow::Layout() {
  if (!frontend_ || !hwnd()) return;
  const HWND child = frontend_->GetHost()->GetWindowHandle();
  if (!child || !IsWindow(child)) return;
  const auto r = FrontendBounds();
  // Above the header document, which fills the window behind it.
  SetWindowPos(child, HWND_TOP, r.x, r.y, r.width, r.height, SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

void DevToolsWindow::BoundsChanged(const CefRect& bounds) {
  Layout();
  if (const auto state = state_.lock(); state && state->devtools_dock && !IsIconic(hwnd()) && !IsZoomed(hwnd()))
    state->devtools_dock->RememberWindowBounds(bounds);
}

bool DevToolsWindow::CanClose() {
  closing_ = true;
  // The frontend closes first; its OnBeforeClose closes this window again.
  if (frontend_) { frontend_->GetHost()->CloseBrowser(false); return false; }
  return !header_ || header_->GetHost()->TryCloseBrowser();
}

void DevToolsWindow::Close() {
  closing_ = true;
  if (window_) window_->Close();
}

void DevToolsWindow::FrontendClosed(int id) {
  if (!frontend_ || frontend_->GetIdentifier() != id) return;
  frontend_ = nullptr;
  if (closing_ && window_) window_->Close();
}

void DevToolsWindow::Destroyed() {
  window_ = nullptr; ready_ = false; header_ = nullptr; observer_ = nullptr;
}

void DevToolsWindow::HeaderCreated(CefRefPtr<CefBrowser> browser) {
  header_ = browser;
  if (const auto state = state_.lock(); state && (!state->window || (state->workspace && state->workspace->WindowClosing()))) {
    browser->GetHost()->CloseBrowser(true); return;
  }
  Layout();  // The header's own surface must not cover a frontend created first.
}

void DevToolsWindow::HeaderClosed(int) {
  header_ = nullptr; observer_ = nullptr; observer_id_ = -1;
  if (window_ && !closing_) Close();
}

std::string DevToolsWindow::Payload() const {
  const auto state = state_.lock();
  auto d = CefDictionaryValue::Create();
  d->SetInt("target", target_);
  d->SetBool("attached", frontend_ != nullptr);
  const bool active = state && state->devtools_dock && state->devtools_dock->IsActive(target_);
  d->SetBool("deviceAvailable", active);
  d->SetBool("device", active && state->devtools_dock->DeviceEnabled(target_));
  auto value = CefValue::Create(); value->SetDictionary(d);
  return CefWriteJSON(value, JSON_WRITER_DEFAULT);
}

void DevToolsWindow::Publish() {
  if (observer_) observer_->Success(Payload());
}

void DevToolsWindow::Cancel(int64_t query) {
  if (query == observer_id_) { observer_ = nullptr; observer_id_ = -1; }
}

bool DevToolsWindow::Query(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, int64_t query, const CefString& raw,
    bool persistent, CefRefPtr<CefMessageRouterBrowserSide::Callback> callback) {
  const auto state = state_.lock();
  if (!state || !header_ || !IsDevToolsWindowUi(browser->GetIdentifier(), header_->GetIdentifier(), frame->IsMain(), frame->GetURL().ToString())) {
    callback->Failure(403, "Untrusted developer tools document"); return true;
  }
  if (raw.length() > 1024) { callback->Failure(413, "Request too large"); return true; }
  const auto value = CefParseJSON(raw, JSON_PARSER_RFC);
  if (!value || value->GetType() != VTYPE_DICTIONARY || value->GetDictionary()->GetType("method") != VTYPE_STRING) {
    callback->Failure(400, "Invalid command"); return true;
  }
  const auto d = value->GetDictionary(); const auto method = d->GetString("method").ToString();
  if (HandleUiFocus(state, browser, method, d->GetSize(), persistent, callback)) return true;
  if (persistent != (method == "devtools.window.subscribe")) { callback->Failure(400, "Only the subscription persists"); return true; }
  if (method == "devtools.window.subscribe" && d->GetSize() == 1) {
    if (observer_) { callback->Failure(409, "Already subscribed"); return true; }
    observer_ = callback; observer_id_ = query; Publish(); return true;
  }
  if (method == "devtools.window.control" && d->GetSize() == 2 && d->GetType("action") == VTYPE_STRING) {
    const auto action = d->GetString("action").ToString();
    if (action != "right" && action != "bottom" && action != "left" && action != "close" && action != "device") {
      callback->Failure(400, "Invalid action"); return true;
    }
    if (!state->devtools_dock || !state->devtools_dock->ControlTarget(target_, action)) {
      callback->Failure(409, "Developer tools changed"); return true;
    }
    callback->Success(Payload()); Publish(); return true;
  }
  callback->Failure(404, "Unknown method"); return true;
}
}  // namespace red
