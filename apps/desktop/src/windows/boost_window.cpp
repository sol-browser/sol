#include "windows/boost_window.h"
#include <dwmapi.h>
#include <algorithm>
#include "app/session.h"
#include "include/cef_parser.h"
#include "include/views/cef_fill_layout.h"
#include "site/site_settings.h"
#include "trust_policy.h"
#include "windows/boosts.h"
#include "windows/branding.h"
#include "windows/focus.h"
#include "windows/ui_context_menu.h"
#include "windows/window_close.h"
#include "workspace/controller.h"

namespace red {
namespace {
constexpr int kWidth = 264, kHeight = 620;
const cef_color_t kBackground = CefColorSetARGB(255, 28, 26, 32);

class BoostClient final : public CefClient, public CefLifeSpanHandler, public CefRequestHandler,
    public CefMessageRouterBrowserSide::Handler {
 public:
  explicit BoostClient(std::weak_ptr<BoostWindow> owner) : owner_(owner) {
    router_ = CefMessageRouterBrowserSide::Create(CefMessageRouterConfig()); router_->AddHandler(this, false);
  }
  ~BoostClient() override { router_->RemoveHandler(this); }
  CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return this; }
  CefRefPtr<CefContextMenuHandler> GetContextMenuHandler() override { return new UiContextMenu(std::weak_ptr<State>()); }
  CefRefPtr<CefRequestHandler> GetRequestHandler() override { return this; }
  void OnAfterCreated(CefRefPtr<CefBrowser> browser) override { if (auto owner = owner_.lock()) owner->BrowserCreated(browser); }
  void OnBeforeClose(CefRefPtr<CefBrowser> browser) override {
    router_->OnBeforeClose(browser); if (auto owner = owner_.lock()) owner->BrowserClosed(browser->GetIdentifier());
  }
  bool OnBeforeBrowse(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, CefRefPtr<CefRequest> request, bool, bool) override {
    router_->OnBeforeBrowse(browser, frame); return !frame->IsMain() || request->GetURL().ToString() != kBoostUrl;
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
    callback->Failure(410, "Boost editor closed"); return true;
  }
 private:
  std::weak_ptr<BoostWindow> owner_;
  CefRefPtr<CefMessageRouterBrowserSide> router_;
  IMPLEMENT_REFCOUNTING(BoostClient);
};
class BoostView final : public CefBrowserViewDelegate {
 public: cef_runtime_style_t GetBrowserRuntimeStyle() override { return CEF_RUNTIME_STYLE_ALLOY; }
 private: IMPLEMENT_REFCOUNTING(BoostView);
};
class BoostDelegate final : public CefWindowDelegate {
 public:
  BoostDelegate(std::weak_ptr<BoostWindow> owner, CefRefPtr<CefBrowserView> view) : owner_(owner), view_(view) {}
  cef_runtime_style_t GetWindowRuntimeStyle() override { return CEF_RUNTIME_STYLE_ALLOY; }
  CefSize GetPreferredSize(CefRefPtr<CefView>) override { return CefSize(kWidth, kHeight); }
  CefSize GetMinimumSize(CefRefPtr<CefView>) override { return CefSize(240, 320); }
  bool CanMaximize(CefRefPtr<CefWindow>) override { return false; }
  bool CanMinimize(CefRefPtr<CefWindow>) override { return false; }
  CefRefPtr<CefWindow> GetParentWindow(CefRefPtr<CefWindow>, bool* is_menu, bool*) override {
    *is_menu = false;  // Owned: stays above the browser window, moves freely.
    if (const auto owner = owner_.lock()) return owner->parent();
    return nullptr;
  }
  void OnWindowCreated(CefRefPtr<CefWindow> window) override {
    window->SetBackgroundColor(kBackground);
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
  std::weak_ptr<BoostWindow> owner_; CefRefPtr<CefBrowserView> view_;
  IMPLEMENT_REFCOUNTING(BoostDelegate);
};
}  // namespace

void ShowBoostEditor(const std::shared_ptr<State>& state, CefRefPtr<CefBrowser> page) {
  if (!state->boost_window) state->boost_window = std::make_shared<BoostWindow>(state);
  state->boost_window->Show(page);
}

void BoostWindow::Show(CefRefPtr<CefBrowser> page) {
  const auto state = state_.lock();
  if (!state || !page || !state->window || !state->ui_context || !state->workspace || state->workspace->WindowClosing()) return;
  const auto key = BoostKeyFromOrigin(SiteOrigin(page));
  if (key.empty()) return;
  const bool retarget = !page_ || !page_->IsSame(page) || key != key_;
  if (retarget) { StopPick(); page_ = page; key_ = key; }
  if (window_) {
    window_->SetTitle(CefString("Boost — " + key_));
    // The document asks for its new target itself.
    if (retarget && browser_) browser_->GetMainFrame()->ExecuteJavaScript("dispatchEvent(new Event('red-boost-target'))", kBoostUrl.data(), 0);
    Present(); return;
  }
  if (creating_) return;
  creating_ = true;
  CefBrowserSettings settings; settings.background_color = kBackground;
  auto extra = CefDictionaryValue::Create(); extra->SetBool("trusted_ui", true);
  auto view = CefBrowserView::CreateBrowserView(new BoostClient(weak_from_this()), std::string(kBoostUrl),
      settings, extra, state->ui_context, new BoostView());
  view->SetBackgroundColor(settings.background_color);
  CefWindow::CreateTopLevelWindow(new BoostDelegate(weak_from_this(), view));
}

CefRefPtr<CefWindow> BoostWindow::parent() const {
  const auto state = state_.lock(); return state ? state->window : nullptr;
}

void BoostWindow::Created(CefRefPtr<CefWindow> window) {
  window_ = window;
  window->SetTitle(CefString("Boost — " + key_));
  // Beside the page: near the right edge of the browser window, below its top bar.
  if (const auto owner = parent()) {
    const auto area = owner->GetBounds();
    const int height = std::max(360, std::min(kHeight, area.height - 96));
    window->SetBounds(CefRect(area.x + std::max(0, area.width - kWidth - 28), area.y + 80, kWidth, height));
  } else window->CenterWindow(CefSize(kWidth, kHeight));
}

void BoostWindow::Present() {
  if (!window_ || !ready_) return;
  window_->Show(); window_->Activate();
  if (browser_) browser_->GetHost()->SetFocus(true);
}

void BoostWindow::BrowserCreated(CefRefPtr<CefBrowser> browser) {
  browser_ = browser; creating_ = false;
  if (const auto state = state_.lock()) {
    state->boost_id = browser->GetIdentifier(); RegisterSessionUi(state, browser->GetIdentifier());
    state->browsers[browser->GetIdentifier()] = browser; state->Record("boost_editor_created");
    if (!state->window || (state->workspace && state->workspace->WindowClosing())) browser->GetHost()->CloseBrowser(true);
  }
}

void BoostWindow::BrowserClosed(int id) {
  StopPick();
  browser_ = nullptr; ready_ = creating_ = false;
  if (const auto state = state_.lock()) {
    UnregisterSessionUi(state, id); state->browsers.erase(id); state->boost_id = -1;
    if (state->window_close) state->window_close->OnClosed(id);
    if (!state->window && state->browsers.empty()) QuitIfLastWindow(state);
  }
}

void BoostWindow::Close() { StopPick(); if (window_) window_->Close(); }

void BoostWindow::StopPick() {
  const auto state = state_.lock();
  if (!picking_.empty() && page_ && state && state->session) StopBoostPick(*state->session, page_->GetIdentifier());
  picking_.clear();
  Resolve();
}

void BoostWindow::Resolve() {
  if (const auto pending = pending_pick_) { pending_pick_ = nullptr; pending->Success(Response()); }
}

int BoostWindow::Save(Boost boost, std::string& error) {
  const auto state = state_.lock();
  if (!state || !state->session || !state->session->boosts || key_.empty()) { error = "Boosts are not available"; return 409; }
  if (const int rejected = state->session->boosts->Set(key_, std::move(boost), error)) return rejected;
  error = SaveBoosts(*state->session);
  if (!error.empty()) return 500;
  RefreshBoosts(*state->session);
  state->Record("boost_saved");
  return 0;
}

void BoostWindow::Picked(CefRefPtr<CefDictionaryValue> pick) {
  const auto state = state_.lock();
  if (!state || !state->session || !state->session->boosts) return;
  if (pick->GetBool("cancel")) { StopPick(); return; }
  const auto* current = state->session->boosts->Find(key_);
  Boost next = current ? *current : Boost{};
  const auto selector = pick->GetString("selector").ToString();
  if (!ValidBoostSelector(selector)) return;
  next.enabled = true;
  if (picking_ == "zap") {
    if (std::find(next.zaps.begin(), next.zaps.end(), selector) == next.zaps.end() && next.zaps.size() < kMaxBoostZaps) next.zaps.push_back(selector);
  } else next.focus = selector;
  std::string error;
  Save(std::move(next), error);
  auto details = CefDictionaryValue::Create(); details->SetString("mode", picking_); details->SetString("error", error);
  state->Record("boost_picked", details);
  if (picking_ == "focus") StopPick(); else Resolve();  // Zap keeps picking until Escape or Done.
}

std::string BoostWindow::Response() const {
  const auto state = state_.lock();
  auto response = CefDictionaryValue::Create();
  const auto* boost = state && state->session && state->session->boosts ? state->session->boosts->Find(key_) : nullptr;
  response->SetString("host", key_);
  response->SetDictionary("boost", WriteBoost(boost ? *boost : Boost{}));
  response->SetString("picking", picking_);
  response->SetBool("page", page_ && state && state->session && state->session->boost_slots.contains(page_->GetIdentifier()));
  auto value = CefValue::Create(); value->SetDictionary(response);
  return CefWriteJSON(value, JSON_WRITER_DEFAULT);
}

bool BoostWindow::Query(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, const CefString& raw,
    bool persistent, CefRefPtr<CefMessageRouterBrowserSide::Callback> callback) {
  const auto state = state_.lock();
  if (!state || !IsBoostUi(browser->GetIdentifier(), state->boost_id, frame->IsMain(), frame->GetURL().ToString())) {
    callback->Failure(403, "Untrusted Boost document"); return true;
  }
  // A Boost carries its CSS (JSON-escaped) and up to kMaxBoostZaps selectors.
  if (persistent || raw.length() > 2 * kMaxBoostCss + 2 * kMaxBoostZaps * kMaxBoostSelector + 4096) { callback->Failure(400, "Invalid Boost request"); return true; }
  const auto value = CefParseJSON(raw, JSON_PARSER_RFC);
  if (!value || value->GetType() != VTYPE_DICTIONARY || value->GetDictionary()->GetType("method") != VTYPE_STRING) {
    callback->Failure(400, "Invalid Boost request"); return true;
  }
  const auto data = value->GetDictionary(); const auto method = data->GetString("method").ToString();
  if (HandleUiFocus(state, browser, method, data->GetSize(), persistent, callback)) return true;
  if (!state->workspace || state->workspace->WindowClosing()) { callback->Failure(409, "Browser is closing"); return true; }
  if (method == "boost.ready" && data->GetSize() == 1 && window_) {
    ready_ = true; callback->Success(Response()); Present(); return true;
  }
  if (method == "boost.get" && data->GetSize() == 1) { callback->Success(Response()); return true; }
  if (method == "boost.set" && data->GetSize() == 3 && data->GetType("host") == VTYPE_STRING && data->GetType("boost") == VTYPE_DICTIONARY) {
    // The host echo keeps an edit made just before a retarget from landing on the new site.
    if (data->GetString("host") != key_) { callback->Failure(409, "The editor moved to another site"); return true; }
    Boost boost; std::string error;
    if (!ReadBoost(data->GetDictionary("boost"), boost)) { callback->Failure(400, "Invalid Boost"); return true; }
    if (const int failed = Save(std::move(boost), error)) { callback->Failure(failed, error); return true; }
    callback->Success(Response()); return true;
  }
  if (method == "boost.pick" && data->GetSize() == 2 && data->GetType("mode") == VTYPE_STRING) {
    const auto mode = data->GetString("mode").ToString();
    if (mode != "zap" && mode != "focus") { callback->Failure(400, "Invalid pick"); return true; }
    Resolve();
    if (picking_ != mode) {
      if (!state->session || !page_ || !StartBoostPick(*state->session, page_->GetIdentifier(), mode,
              [weak = weak_from_this()](CefRefPtr<CefDictionaryValue> pick) { if (auto self = weak.lock()) self->Picked(pick); })) {
        callback->Failure(409, "The page is no longer open"); return true;
      }
      picking_ = mode;
      state->Record("boost_pick_started");
    }
    pending_pick_ = callback;  // Answered on the next pick, on Escape or when picking stops.
    return true;
  }
  if (method == "boost.stopPick" && data->GetSize() == 1) { StopPick(); callback->Success(Response()); return true; }
  callback->Failure(400, "Unsupported Boost command"); return true;
}
}  // namespace red
