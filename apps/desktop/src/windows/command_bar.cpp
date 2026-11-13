#include "app/session.h"
#include "app/preferences.h"
#include "windows/pointer.h"
#include "windows/command_bar.h"
#include "windows/command_actions.h"
#include "windows/settings_window.h"
#include "history/history_service.h"
#include <dwmapi.h>
#include <cmath>
#include <commctrl.h>
#include "navigation/search_suggestions.h"
#include "include/cef_app.h"
#include "trust_policy.h"
#include "workspace/controller.h"
#include "windows/focus.h"
#include "windows/ui_context_menu.h"
#include "windows/window_close.h"
#include "navigation/shortcuts.h"
#include "experiments/shell_capture.h"
#include "experiments/site_search_probe.h"
#include "include/cef_parser.h"
#include "include/views/cef_fill_layout.h"
#include "include/base/cef_callback.h"
#include "include/wrapper/cef_closure_task.h"
#include "include/views/cef_display.h"
namespace red {
namespace {
constexpr int kMaxRows = 6, kBaseHeight = 66, kRowHeight = 48, kErrorHeight = 34;
int AnchorHeight(int parent_height) {
  // Reserve the entire supported list and error slot for positioning only.
  // The native window still shrinks to its actual content below this anchor.
  return std::max(1, std::min(kBaseHeight + kMaxRows * kRowHeight + kErrorHeight, parent_height - 76));
}
class CommandClient final : public CefClient, public CefLifeSpanHandler, public CefLoadHandler, public CefDisplayHandler,
    public CefRequestHandler, public CefKeyboardHandler, public CefMessageRouterBrowserSide::Handler {
 public:
  CommandClient(std::weak_ptr<CommandBar> owner, std::weak_ptr<State> state) : owner_(owner), state_(state) {
    router_ = CefMessageRouterBrowserSide::Create(CefMessageRouterConfig()); router_->AddHandler(this, false);
  }
  ~CommandClient() override { router_->RemoveHandler(this); }
  CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return this; }
  CefRefPtr<CefLoadHandler> GetLoadHandler() override { return this; }
  CefRefPtr<CefDisplayHandler> GetDisplayHandler() override { return this; }
  bool OnConsoleMessage(CefRefPtr<CefBrowser> browser, cef_log_severity_t, const CefString& message, const CefString&, int) override {
    const auto text = message.ToString();
    if (auto s = state_.lock(); s && s->probe && text == "RED_COMMAND_CAPTURE")
      capture_ = CaptureShell(browser, s->evidence_directory / "command.png");
    if (auto s = state_.lock(); s && s->probe && text == "RED_COMMAND_ACTION_CAPTURE")
      capture_ = CaptureShell(browser, s->evidence_directory / "command-actions.png");
    if (auto s = state_.lock(); s && s->probe && text == "RED_SITE_SEARCH_CAPTURE")
      capture_ = CaptureShell(browser, s->evidence_directory / "site-search.png");
    if (auto s = state_.lock(); s && s->probe && (text == "RED_SITE_SCOPE_BEGIN" || text == "RED_SITE_SCOPE_END"))
      s->Record(text == "RED_SITE_SCOPE_BEGIN" ? "site_search_scoped_begin" : "site_search_scoped_end");
    if (auto s = state_.lock(); s && s->probe && text.starts_with("RED_SITE_SEARCH_INPUT:")) {
      const auto value = CefParseJSON(text.substr(22), JSON_PARSER_RFC);
      if (value && value->GetType() == VTYPE_DICTIONARY) s->Record("site_search_input_probe", value->GetDictionary());
    }
    if (auto s = state_.lock(); s && s->probe && text.starts_with("RED_COMMAND_INPUT:")) {
      auto v = CefParseJSON(text.substr(18), JSON_PARSER_RFC);
      if (v && v->GetType() == VTYPE_DICTIONARY) s->Record("command_input_probe", v->GetDictionary());
    }
    return false;
  }
  CefRefPtr<CefRequestHandler> GetRequestHandler() override { return this; }
  CefRefPtr<CefKeyboardHandler> GetKeyboardHandler() override { return this; }
  CefRefPtr<CefContextMenuHandler> GetContextMenuHandler() override {
    const auto owner = owner_;
    return new UiContextMenu(state_, {[owner] { const auto o = owner.lock(); return o ? o->window() : nullptr; },
                                      [owner] { if (const auto o = owner.lock()) o->MenuOpened(); },
                                      [owner] { if (const auto o = owner.lock()) o->MenuClosed(); }});
  }
  void OnAfterCreated(CefRefPtr<CefBrowser> b) override { if (auto o = owner_.lock()) o->Created(b); }
  void OnBeforeClose(CefRefPtr<CefBrowser> b) override {
    capture_ = nullptr;
    router_->OnBeforeClose(b);
    if (auto o = owner_.lock()) { o->Closed(b->GetIdentifier()); return; }
    // A bar replaced while its page was still closing (Little Sol's Ctrl+Alt+N bar): its window
    // must still forget the page, or Sol never sees its last window close.
    if (auto s = state_.lock()) CommandBar::Forget(s, b->GetIdentifier());
  }
  bool OnBeforePopup(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, int, const CefString&, const CefString&,
      cef_window_open_disposition_t, bool, const CefPopupFeatures&, CefWindowInfo&, CefRefPtr<CefClient>&,
      CefBrowserSettings&, CefRefPtr<CefDictionaryValue>&, bool*) override { return true; }
  bool OnBeforeBrowse(CefRefPtr<CefBrowser> b, CefRefPtr<CefFrame> f, CefRefPtr<CefRequest> r, bool, bool) override {
    router_->OnBeforeBrowse(b, f); return !f->IsMain() || r->GetURL().ToString() != kCommandUrl;
  }
  void OnRenderProcessTerminated(CefRefPtr<CefBrowser> b, TerminationStatus status, int, const CefString&) override {
    router_->OnRenderProcessTerminated(b);
    if (auto o = owner_.lock()) o->Shutdown();
    if (auto s = state_.lock()) s->workspace->Notify("Command bar stopped working. Restart Sol to try again.");
  }
  void OnLoadEnd(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame> f, int) override {
    if (f->IsMain()) if (auto o = owner_.lock()) o->Ready();
  }
  bool OnPreKeyEvent(CefRefPtr<CefBrowser>, const CefKeyEvent& e, CefEventHandle, bool*) override {
    if (e.type != KEYEVENT_RAWKEYDOWN) return false;
    // React handles Escape: leave action filtering first, then dismiss.
    if (auto s = state_.lock()) {
      if ((e.modifiers & EVENTFLAG_CONTROL_DOWN) && !(e.modifiers & (EVENTFLAG_ALT_DOWN | EVENTFLAG_SHIFT_DOWN))) {
        if (e.windows_key_code == 'T') { s->workspace->OpenNewTab(); return true; }
        if (e.windows_key_code == 'L') { s->workspace->FocusAddress(); return true; }
      }
      if (e.windows_key_code == VK_F4 && (e.modifiers & EVENTFLAG_ALT_DOWN)) { s->window->Close(); return true; }
    }
    return false;
  }
  bool OnProcessMessageReceived(CefRefPtr<CefBrowser> b, CefRefPtr<CefFrame> f, CefProcessId p, CefRefPtr<CefProcessMessage> m) override {
    if (auto s = state_.lock(); s && s->probe && p == PID_RENDERER && f->IsMain() && m->GetName() == "red.probe.token") {
      auto d = m->GetArgumentList()->GetDictionary(0)->Copy(false); d->SetString("role", "command"); s->Record("renderer_token", d); return true;
    }
    return router_->OnProcessMessageReceived(b, f, p, m);
  }
  bool OnQuery(CefRefPtr<CefBrowser> b, CefRefPtr<CefFrame> f, int64_t q, const CefString& r, bool p,
      CefRefPtr<Callback> cb) override { if (auto o = owner_.lock()) return o->Query(b, f, q, r, p, cb); return false; }
  void OnQueryCanceled(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, int64_t q) override { if (auto o = owner_.lock()) o->Cancel(q); }
 private:
  std::weak_ptr<CommandBar> owner_; std::weak_ptr<State> state_;
  CefRefPtr<CefMessageRouterBrowserSide> router_;
  CefRefPtr<CefRegistration> capture_;
  IMPLEMENT_REFCOUNTING(CommandClient);
};
class CommandView final : public CefBrowserViewDelegate {
 public: cef_runtime_style_t GetBrowserRuntimeStyle() override { return CEF_RUNTIME_STYLE_ALLOY; }
 private: IMPLEMENT_REFCOUNTING(CommandView);
};
class CommandWindow final : public CefWindowDelegate {
 public:
  CommandWindow(std::weak_ptr<CommandBar> owner, CefRefPtr<CefBrowserView> view) : owner_(owner), view_(view) {}
  cef_runtime_style_t GetWindowRuntimeStyle() override { return CEF_RUNTIME_STYLE_ALLOY; }
  bool IsFrameless(CefRefPtr<CefWindow>) override { return true; }
  bool CanResize(CefRefPtr<CefWindow>) override { return false; }
  bool CanMaximize(CefRefPtr<CefWindow>) override { return false; }
  bool CanMinimize(CefRefPtr<CefWindow>) override { return false; }
  CefRefPtr<CefWindow> GetParentWindow(CefRefPtr<CefWindow>, bool* menu, bool*) override {
    *menu = false; if (auto o = owner_.lock()) return o->parent(); return nullptr;
  }
  CefSize GetPreferredSize(CefRefPtr<CefView>) override { return CefSize(720, 360); }
  void OnWindowCreated(CefRefPtr<CefWindow> w) override {
    w->SetTitle("Sol — Command bar"); w->SetBackgroundColor(CefColorSetARGB(255, 16, 16, 18));
    const DWM_WINDOW_CORNER_PREFERENCE corners = DWMWCP_ROUND;
    DwmSetWindowAttribute(w->GetWindowHandle(), DWMWA_WINDOW_CORNER_PREFERENCE, &corners, sizeof(corners));
    // One outline only: the page draws it (command-bar.css, 8px like DWM's rounding). Windows' own
    // border followed another radius, and grey showed in the corners between the two.
    const COLORREF no_border = DWMWA_COLOR_NONE;
    DwmSetWindowAttribute(w->GetWindowHandle(), DWMWA_BORDER_COLOR, &no_border, sizeof(no_border));
    w->SetToFillLayout(); w->AddChildView(view_);
    if (auto o = owner_.lock()) o->Attach(w, view_);
  }
  void OnWindowActivationChanged(CefRefPtr<CefWindow>, bool active) override {
    if (!active) if (auto o = owner_.lock(); o && o->is_presented()) o->Deactivated();
  }
  bool CanClose(CefRefPtr<CefWindow>) override {
    if (auto o = owner_.lock(); o && !o->shutting_down()) { o->Hide(true); return false; } return true;
  }
  void OnWindowDestroyed(CefRefPtr<CefWindow>) override { view_ = nullptr; if (auto o = owner_.lock()) o->Destroyed(); }
 private: std::weak_ptr<CommandBar> owner_; CefRefPtr<CefBrowserView> view_;
  IMPLEMENT_REFCOUNTING(CommandWindow);
};
}
CefRefPtr<CefWindow> CommandBar::parent() const {
  if (detached_) return nullptr;
  auto s = state_.lock(); return s ? s->window : nullptr;
}
void CommandBar::OpenDetached() {
  POINT cursor{};
  GetCursorPos(&cursor);
  const auto display = CefDisplay::GetDisplayNearestPoint(CefPoint(cursor.x, cursor.y), false);
  if (!display) return;
  detached_area_ = display->GetWorkArea();
  detached_ = true; launcher_keep_ = false;
  Open(true);
  if (!open_) detached_ = false;
}
CommandBar::~CommandBar() {
  if (owner_handle_) RemoveWindowSubclass(owner_handle_, OwnerMessage, reinterpret_cast<UINT_PTR>(this));
  if (suggestions_) suggestions_->Cancel();
}
LRESULT CALLBACK CommandBar::OwnerMessage(HWND window, UINT message, WPARAM wparam, LPARAM lparam, UINT_PTR id, DWORD_PTR data) {
  auto* self = reinterpret_cast<CommandBar*>(data);
  if (message == WM_NCDESTROY) {
    self->owner_handle_ = nullptr; RemoveWindowSubclass(window,OwnerMessage,id);
    return DefSubclassProc(window,message,wparam,lparam);
  }
  const bool start = message == WM_ENTERSIZEMOVE ||
      (message == WM_NCLBUTTONDOWN && (wparam == HTCAPTION || (wparam >= HTLEFT && wparam <= HTBOTTOMRIGHT))) ||
      (message == WM_SYSCOMMAND && ((wparam & 0xfff0) == SC_MOVE || (wparam & 0xfff0) == SC_SIZE));
  if (start && self->open_) { self->BeginOwnerGesture(); self->owner_moving_ = true; }
  if (self->open_ && (start || message == WM_EXITSIZEMOVE)) if (const auto state = self->state_.lock()) {
    auto details = CefDictionaryValue::Create(); details->SetInt("message",static_cast<int>(message));
    details->SetBool("begin",start); state->Record("command_owner_gesture",details);
  }
  const auto result = DefSubclassProc(window,message,wparam,lparam);
  if (message == WM_EXITSIZEMOVE || (start && message != WM_ENTERSIZEMOVE)) {
    self->owner_moving_ = false; self->FinishOwnerGesture();
  }
  return result;
}
void CommandBar::RestoreAfterMove() {
  if (!open_ || !presented_ || !window_ || !owner_handle_ || IsIconic(owner_handle_)) return;
  Reposition();
  // Return focus only when this exact owner is still foreground, never Alt+Tab.
  if (GetForegroundWindow() == owner_handle_) { window_->Activate(); FocusBrowser(browser_); }
}
void CommandBar::BeginOwnerGesture() {
  if (gesture_pending_ || !owner_handle_) return;
  gesture_pending_ = true; gesture_dragged_ = false;
  GetWindowRect(owner_handle_,&gesture_bounds_); GetCursorPos(&gesture_cursor_);
}
void CommandBar::TrackOwnerGesture() {
  if (!gesture_pending_) return;
  RECT bounds{}; POINT cursor{};
  if (GetWindowRect(owner_handle_,&bounds) && !EqualRect(&bounds,&gesture_bounds_)) gesture_dragged_ = true;
  if (GetCursorPos(&cursor) && (std::abs(cursor.x - gesture_cursor_.x) > GetSystemMetrics(SM_CXDRAG) ||
      std::abs(cursor.y - gesture_cursor_.y) > GetSystemMetrics(SM_CYDRAG))) gesture_dragged_ = true;
}
void CommandBar::FinishOwnerGesture() {
  if (!gesture_pending_) return;
  TrackOwnerGesture(); gesture_pending_ = false;
  if (gesture_dragged_) RestoreAfterMove();
  else Hide(false);
}
void CommandBar::MenuClosed() {
  // The menu hands activation back to the bar before this runs; a click elsewhere did not.
  CefPostTask(TID_UI, base::BindOnce([](std::weak_ptr<CommandBar> weak) {
    const auto self = weak.lock();
    if (!self || !self->menu_open_) return;
    self->menu_open_ = false;
    if (self->open_ && self->presented_ && self->window_ && GetForegroundWindow() != self->window_->GetWindowHandle()) self->Hide(false);
    else if (self->browser_) FocusBrowser(self->browser_);
  }, weak_from_this()));
}
void CommandBar::Deactivated() {
  if (menu_open_) return;
  // Activation precedes WM_NCLBUTTONDOWN/WM_ENTERSIZEMOVE. Identify that click
  // at deactivation, before the native move loop has set its thread flags.
  POINT cursor{}; LRESULT hit = HTNOWHERE;
  if (owner_handle_ && GetCursorPos(&cursor)) hit = SendMessage(owner_handle_,WM_NCHITTEST,0,MAKELPARAM(cursor.x,cursor.y));
  const bool frame_click = PrimaryButtonDown() &&
      (hit == HTCAPTION || (hit >= HTLEFT && hit <= HTBOTTOMRIGHT));
  if (frame_click) BeginOwnerGesture();
  CefPostTask(TID_UI,base::BindOnce([](std::weak_ptr<CommandBar> weak, bool frame_click) {
    const auto self = weak.lock();
    if (!self || !self->open_ || !self->presented_ || !self->window_ || self->menu_open_) return;
    const auto foreground = GetForegroundWindow();
    if (foreground == self->window_->GetWindowHandle()) return;
    GUITHREADINFO info{sizeof(info)};
    const bool moving = GetGUIThreadInfo(GetCurrentThreadId(),&info) && (info.flags & GUI_INMOVESIZE);
    if (const auto state = self->state_.lock()) {
      auto details = CefDictionaryValue::Create(); details->SetBool("owner_foreground",foreground == self->owner_handle_);
      details->SetBool("move_flag",self->owner_moving_); details->SetBool("native_move",moving); details->SetBool("frame_click",frame_click);
      state->Record("command_deactivated",details);
    }
    if (foreground == self->owner_handle_ && (self->owner_moving_ || moving || frame_click)) {
      self->FollowOwnerGesture(self->request_); return;
    }
    self->Hide(false);
  },weak_from_this(),frame_click));
}
void CommandBar::FollowOwnerGesture(int request) {
  if (!open_ || !presented_ || request != request_ || !window_ || !gesture_pending_) return;
  const auto foreground = GetForegroundWindow();
  if (foreground != owner_handle_ && foreground != window_->GetWindowHandle()) { Hide(false); return; }
  GUITHREADINFO info{sizeof(info)};
  const bool moving = GetGUIThreadInfo(GetCurrentThreadId(),&info) && (info.flags & GUI_INMOVESIZE);
  TrackOwnerGesture();
  if (!owner_moving_ && !moving && !PrimaryButtonDown()) { FinishOwnerGesture(); return; }
  CefPostDelayedTask(TID_UI,base::BindOnce([](std::weak_ptr<CommandBar> weak, int request) {
    if (const auto self = weak.lock()) self->FollowOwnerGesture(request);
  },weak_from_this(),request), 16);
}
void CommandBar::Attach(CefRefPtr<CefWindow> w, CefRefPtr<CefBrowserView> v) {
  window_ = w; view_ = v; presentation_.Attach(w, state_);
  // Without an owner the bar would get a taskbar button of its own.
  if (detached_) if (const HWND handle = w->GetWindowHandle())
    SetWindowLongPtrW(handle, GWL_EXSTYLE, GetWindowLongPtrW(handle, GWL_EXSTYLE) | WS_EX_TOOLWINDOW);
  Reposition();
}
void CommandBar::Created(CefRefPtr<CefBrowser> b) {
  browser_ = b;
  if (auto s = state_.lock()) { s->command_id = b->GetIdentifier(); RegisterSessionUi(s, b->GetIdentifier()); s->browsers[b->GetIdentifier()] = b; s->Record("command_created"); }
}
void CommandBar::Closed(int id) {
  observer_ = nullptr; browser_ = nullptr;
  if (auto s = state_.lock()) Forget(s, id);
}
void CommandBar::Forget(const std::shared_ptr<State>& s, int id) {
  UnregisterSessionUi(s, id); s->browsers.erase(id); if (s->command_id == id) s->command_id = -1;
  s->Record("command_closed");
  if (s->window_close) s->window_close->OnClosed(id);
  if (!s->window && s->browsers.empty()) QuitIfLastWindow(s);
}
void CommandBar::Destroyed() { presentation_.Reset(); window_ = nullptr; view_ = nullptr; ready_ = prepared_ = presented_ = false; }
void CommandBar::Reposition() {
  auto p = parent(); if ((!p && !detached_) || !window_) return;
  if (!detached_) TrackOwnerGesture();
  const auto b = detached_ ? detached_area_ : p->GetClientAreaBoundsInScreen();
  const int reference = AnchorHeight(b.height);
  const int width = std::max(1, std::min(720, b.width - 32)), height = std::clamp(height_, 1, reference);
  const CefRect target(b.x + (b.width - width) / 2, b.y + (b.height - reference) / 2, width, height);
  if (window_->GetBounds() == target) return;
  if (presented_) presentation_.Settle();
  window_->SetBounds(target);
  if (auto s = state_.lock(); s && s->input_probe) {
    auto data = CefDictionaryValue::Create(); data->SetInt("x", target.x); data->SetInt("y", target.y);
    data->SetInt("width", width); data->SetInt("height", height); data->SetInt("referenceHeight", reference);
    s->Record("command_bounds", data);
  }
}
void CommandBar::Open(bool new_tab) {
  auto s = state_.lock(); if (!s || !s->window || !s->workspace || s->workspace->WindowClosing() || shutdown_ || submitting_) return;
  auto snap = s->workspace->Snapshot(); if (!snap->GetBool("ready")) return;
  if (open_ && presented_ && new_tab_ == new_tab) { window_->Activate(); FocusBrowser(browser_); return; }
  if (!owner_handle_ && !detached_) {
    const auto handle = s->window->GetWindowHandle();
    if (SetWindowSubclass(handle,OwnerMessage,reinterpret_cast<UINT_PTR>(this),reinterpret_cast<DWORD_PTR>(this))) owner_handle_ = handle;
    auto details = CefDictionaryValue::Create(); details->SetBool("attached",owner_handle_ != nullptr);
    s->Record("command_owner_hook",details);
  }
  new_tab_ = new_tab; source_id_ = snap->GetString("activeId"); source_url_ = snap->GetString("url"); ++request_; open_ = true;
  gesture_pending_ = false;
  height_ = kBaseHeight + kRowHeight;
  opened_ms_ = GetTickCount64();
  prepared_ = presented_ = false; presentation_.Conceal();
  if (!window_) {
    auto shell = s->browsers.find(s->shell_id); if (shell == s->browsers.end()) return;
    CefBrowserSettings settings; settings.background_color = CefColorSetARGB(255, 16, 16, 18);
    auto extra = CefDictionaryValue::Create(); extra->SetBool("trusted_ui", true);
    auto view = CefBrowserView::CreateBrowserView(new CommandClient(weak_from_this(), s), std::string(kCommandUrl),
        settings, extra, shell->second->GetHost()->GetRequestContext(), new CommandView());
    view->SetBackgroundColor(settings.background_color);
    CefWindow::CreateTopLevelWindow(new CommandWindow(weak_from_this(), view));
  }
  Reposition(); Publish();
  auto d = CefDictionaryValue::Create(); d->SetString("mode", new_tab ? "new" : "navigate"); s->Record("command_open", d);
}
void CommandBar::Ready() {
  ready_ = true; Publish();
}
void CommandBar::Hide(bool restore_focus) {
  if (!open_) return; open_ = false;
  gesture_pending_ = false;
  if (suggestions_) suggestions_->Cancel();
  suggestion_query_ = -1;
  prepared_ = presented_ = false; presentation_.Conceal();
  if (observer_) observer_->Success(Response());
  if (detached_) {
    // The launcher's Little Sol shows with its page, or closes; there is no owner to refocus.
    detached_ = false;
    const bool keep = launcher_keep_; launcher_keep_ = false;
    if (auto s = state_.lock()) CefPostTask(TID_UI, base::BindOnce([](std::weak_ptr<State> weak, bool keep) {
      if (const auto state = weak.lock(); state && state->workspace) state->workspace->FinishLittleSolLauncher(keep);
    }, std::weak_ptr<State>(s), keep));
    return;
  }
  if (auto s = state_.lock()) {
    auto data = CefDictionaryValue::Create(); data->SetBool("restore_focus", restore_focus); s->Record("command_hidden", data);
    if (restore_focus && s->window) {
      s->window->Activate(); if (s->content) FocusBrowser(s->content); else FocusShell(s);
      // Owner activation can finish after Hide and focus Alloy again. Restore
      // once after that native transition, without stealing focus from another
      // app, a newly reopened popup, or a different page.
      const int page_id = s->content ? s->content->GetIdentifier() : -1;
      CefPostTask(TID_UI, base::BindOnce([](std::weak_ptr<CommandBar> weak, int page_id) {
        const auto owner = weak.lock(); if (!owner || owner->open_ || owner->shutdown_) return;
        const auto state = owner->state_.lock();
        if (!state || !state->window || GetForegroundWindow() != state->window->GetWindowHandle() ||
            state->workspace->WindowClosing()) return;
        if (state->content && state->content->GetIdentifier() == page_id) FocusBrowser(state->content);
        else if (page_id == -1 && !state->content) FocusShell(state);
      }, weak_from_this(), page_id));
    }
  }
}
void CommandBar::Shutdown() {
  Hide(false); shutdown_ = true; observer_ = nullptr;
  if (owner_handle_) { RemoveWindowSubclass(owner_handle_,OwnerMessage,reinterpret_cast<UINT_PTR>(this)); owner_handle_ = nullptr; }
  if (window_) window_->Close();
}
std::string CommandBar::Response() const {
  auto s = state_.lock(); auto d = CefDictionaryValue::Create();
  d->SetInt("request", request_); d->SetString("mode", new_tab_ ? "new" : "navigate");
  d->SetBool("open", open_);
  d->SetString("sourceUrl", source_url_); d->SetDictionary("snapshot", s->workspace->Snapshot());
  auto value = CefValue::Create(); value->SetDictionary(d); return CefWriteJSON(value, JSON_WRITER_DEFAULT);
}
void CommandBar::Publish() { if (observer_) observer_->Success(Response()); }
void CommandBar::Cancel(int64_t query) {
  if (query == observer_id_) { observer_ = nullptr; observer_id_ = -1; }
  if (query == suggestion_query_) { suggestion_query_ = -1; if (suggestions_) suggestions_->Cancel(); }
}
void CommandBar::ProbeInput(const std::string& input, int attempts, bool actions_checked) {
  auto s = state_.lock(); if (!s || !s->probe || (!s->organization_probe && !s->browsing_probe) || !open_) return;
  // Explicit native-input fixture: request activation of its own owner before
  // the popup is presented. Production preparation still requires real focus.
  if (attempts == 100 && !presented_ && s->window) s->window->Activate();
  if (!ready_ || !observer_) {
    if (attempts > 0) CefPostDelayedTask(TID_UI, base::BindOnce([](std::weak_ptr<CommandBar> weak, std::string text, int tries, bool checked) {
      if (auto o = weak.lock()) o->ProbeInput(text, tries, checked);
    }, weak_from_this(), input, attempts - 1, actions_checked), 30);
    return;
  }
  if (!actions_checked) {
    if (s->browsing_probe && !site_probe_started_) {
      site_probe_started_ = true; const bool new_tab = new_tab_;
      StartSiteSearchProbe(s, input, [weak = weak_from_this(), input, new_tab] {
        if (const auto owner = weak.lock()) { owner->Open(new_tab); owner->ProbeInput(input); }
      });
      browser_->GetMainFrame()->ExecuteJavaScript(R"JS((async()=>{
        const i=document.querySelector('[role="combobox"]');
        Object.getOwnPropertyDescriptor(HTMLInputElement.prototype,'value').set.call(i,'Site Search Settings');
        i.dispatchEvent(new Event('input',{bubbles:true}));await new Promise(r=>setTimeout(r,80));
        if(document.querySelector('[role="option"]')?.dataset.kind!=='action')throw Error('Missing site-search settings action');
        i.form.requestSubmit();
      })())JS", std::string(kCommandUrl), 0);
      return;
    }
    const bool sidebar = s->sidebar_visible, new_tab = new_tab_;
    const std::string script = R"JS((async()=>{
      const wait=()=>new Promise(r=>setTimeout(r,70));await wait();
      const i=document.querySelector('[role="combobox"]');
      const type=async text=>{Object.getOwnPropertyDescriptor(HTMLInputElement.prototype,'value').set.call(i,text);i.dispatchEvent(new Event('input',{bubbles:true}));await wait();};
      const key=async value=>{document.activeElement.dispatchEvent(new KeyboardEvent('keydown',{key:value,bubbles:true,cancelable:true}));await wait();};
      const check=(ok,label)=>{if(!ok)throw Error('Command actions: '+label);};
      await type('');await key('Tab');
      check(document.querySelector('.command-mode')&&[...document.querySelectorAll('[role="option"]')].every(r=>r.dataset.kind==='action'),'Tab filters actions');
      console.info('RED_COMMAND_ACTION_CAPTURE');await wait();
      await type('no matching action');check(!document.querySelector('[role="option"]'),'empty action results');
      document.querySelector('.command-mode').focus();
      await key('Escape');check(!document.querySelector('.command-mode')&&document.activeElement===i,'Escape from action chip returns focus to navigation');
      const call=request=>new Promise((resolve,reject)=>cefQuery({request:JSON.stringify(request),onSuccess:raw=>resolve(JSON.parse(raw)),onFailure:code=>reject(code)}));
      const state=await call({method:'command.snapshot'});
      const reject=async request=>{let code=0;try{await call(request);}catch(e){code=e;}check(code>=400,'unsafe action rejected');};
      await reject({method:'command.action',action:'address',request:state.request});
      await reject({method:'command.action',action:'sidebar',request:state.request-1});
      await reject({method:'command.action',action:'sidebar',request:state.request,extra:true});
      await reject({method:'command.action',action:'sidebar'});
      await type('toggle sidebar');check(document.querySelector('[role="option"]')?.dataset.kind==='action','exact action selected');
      i.form.requestSubmit();
    })())JS";
    browser_->GetMainFrame()->ExecuteJavaScript(script, std::string(kCommandUrl), 0);
    CefPostDelayedTask(TID_UI, base::BindOnce([](std::weak_ptr<CommandBar> weak, std::string text, bool sidebar, bool new_tab) {
      const auto owner = weak.lock(); if (!owner) return;
      const auto state = owner->state_.lock(); if (!state) return;
      const bool passed = !owner->open_ && state->sidebar_visible != sidebar;
      auto details = CefDictionaryValue::Create(); details->SetBool("pass", passed); details->SetInt("nativeRejected", 4);
      state->Record("command_actions_probe", details);
      if (!passed) return;
      ExecuteShortcut(state, Shortcut::Sidebar);  // Restore the fixture's original state.
      owner->Open(new_tab); owner->ProbeInput(text, 100, true);
    }, weak_from_this(), input, sidebar, new_tab), 1200);
    return;
  }
  auto v = CefValue::Create(); v->SetString(input);
  const auto root = parent()->GetClientAreaBoundsInScreen(); const auto popup = window_->GetBounds();
  auto geometry = CefDictionaryValue::Create();
  geometry->SetBool("centeredX", std::abs((popup.x - root.x) * 2 + popup.width - root.width) <= 1);
  geometry->SetBool("centeredY", std::abs((popup.y - root.y) * 2 + popup.height - root.height) <= 1);
  geometry->SetBool("anchoredY", std::abs((popup.y - root.y) * 2 + AnchorHeight(root.height) - root.height) <= 1);
  geometry->SetInt("width", popup.width); geometry->SetInt("height", popup.height); s->Record("command_layout_probe", geometry);
  const std::string script = std::string(R"JS((async()=>{await new Promise(r=>setTimeout(r,80));const i=document.querySelector('[role="combobox"]');
    if(!i)throw Error('Missing command input');
    const initial={focused:document.activeElement===i,selected:i.selectionStart===0&&i.selectionEnd===i.value.length,initial:i.value};
    const type=async text=>{Object.getOwnPropertyDescriptor(HTMLInputElement.prototype,'value').set.call(i,text);i.dispatchEvent(new Event('input',{bubbles:true}));await new Promise(r=>setTimeout(r,80));};
    const key=async(key,extra={})=>{i.dispatchEvent(new KeyboardEvent('keydown',{key,bubbles:true,cancelable:true,...extra}));await new Promise(r=>setTimeout(r,50));};
    const rows=()=>[...document.querySelectorAll('[role="option"]')];
    const check=(ok,message)=>{if(!ok)throw Error('Prediction probe: '+message);};
    await type('youtu');check(rows()[0]?.dataset.kind==='visit'&&rows()[0]?.textContent.includes('YouTube')&&rows()[0]?.textContent.includes('youtube.com')&&rows()[1]?.dataset.kind==='search','YouTube prefix');
    console.info('RED_COMMAND_CAPTURE');
    await type('yt');check(rows()[0]?.textContent.includes('YouTube')&&rows()[1]?.dataset.kind==='search','brand alias');
    await type('youtube tutorial');check(rows()[0]?.dataset.kind==='search','phrase remains search');
    await type('yotube');check(rows()[0]?.dataset.kind==='search'&&rows().some(r=>r.textContent.includes('YouTube')),'typo preserves search');
    await key('ArrowDown');check(i.value==='https://youtube.com/'&&i.selectionStart===i.value.length&&i.selectionEnd===i.value.length,'selected address is ready to edit');
    await key('ArrowUp');check(i.value==='yotube'&&rows().some(r=>r.textContent.includes('YouTube')),'arrows retain the original result list');
    await key('ArrowDown');document.execCommand('insertText',false,'watch?v=fixture');await new Promise(r=>setTimeout(r,80));
    check(i.value==='https://youtube.com/watch?v=fixture'&&rows()[0]?.dataset.kind==='visit','typing extends selected address');
    await type('first.last+tag@example.org');check(rows()[0]?.dataset.kind==='search'&&rows()[0]?.textContent.includes(i.value),'email is searchable');
    await type('10.10.10.10:304');check(rows()[0]?.dataset.kind==='visit','private IPv4 is an address');
    const reject=(request,persistent=false)=>new Promise((resolve,reject)=>cefQuery({request:JSON.stringify(request),persistent,onSuccess:()=>reject(Error('Unsafe command accepted')),onFailure:code=>code>=400?resolve():reject(Error('Unexpected rejection'))}));
    await reject({method:'navigation.load',url:'https://example.com/'});
    await reject({method:'tree.createFolder',parentId:'',title:'Forbidden'});
    await reject({method:'command.close',extra:true});
    await reject({method:'command.activate',id:'missing'});
    await reject({method:'command.submit',url:'sol://ui/index.html'});
    await reject({method:'command.submit',url:'javascript:alert(1)'});
    await reject({method:'command.submit',url:'https://user:pass@example.com/'});
    await reject({method:'command.snapshot'},true);
    console.info('RED_COMMAND_INPUT:'+JSON.stringify({...initial,nativeRejected:8,predictionChecks:4,inputChecks:5}));
    )JS") + (s->browsing_probe ? R"JS(
    await type('yt');await key('Tab',{ctrlKey:true});check(!document.querySelector('.command-site-mode'),'modified Tab keeps normal mode');
    await key('Tab',{isComposing:true});check(!document.querySelector('.command-site-mode'),'IME Tab keeps normal mode');
    await key('Tab');check(document.querySelector('.command-site-mode')?.textContent.includes('YouTube')&&i.value==='','Tab scopes YouTube');
    console.info('RED_SITE_SCOPE_BEGIN');
    i.form.requestSubmit();await type('кот & hi');check(i.value==='кот & hi'&&rows().length===1&&rows()[0].textContent.includes('YouTube'),'literal scoped query');
    i.dispatchEvent(new CompositionEvent('compositionstart',{bubbles:true}));await new Promise(r=>setTimeout(r,30));i.form.requestSubmit();
    await new Promise(r=>setTimeout(r,30));check(document.querySelector('.command-site-mode'),'IME submit suppressed');
    i.dispatchEvent(new CompositionEvent('compositionend',{bubbles:true}));console.info('RED_SITE_SCOPE_END');await key('Escape');
    check(!document.querySelector('.command-site-mode')&&i.value==='yt кот & hi','Escape retains query in normal mode');
    await type('yt');await key('Tab');await key('Backspace');check(!document.querySelector('.command-site-mode')&&i.value==='yt','empty Backspace restores shortcut');
    await type('wiki');await key('Tab');check(document.querySelector('.command-site-mode')?.textContent.includes('Wikipedia')&&i.value==='','Tab uses typed prefix despite inline suffix');
    await type('query');await key('Tab',{shiftKey:true});check(!document.querySelector('.command-site-mode')&&i.value==='wiki query','Shift Tab retains query');
    await type('yt');await key(' ');check(document.querySelector('.command-site-mode')?.textContent.includes('YouTube'),'Space scopes the configured site');await key('Escape');
    await type('owned.test');await key('Tab');check(document.querySelector('.command-site-mode')?.textContent.includes('Owned search'),'saved domain shortcut');
    await type('typed');console.info('RED_SITE_SEARCH_CAPTURE');await new Promise(r=>setTimeout(r,70));
    console.info('RED_SITE_SEARCH_INPUT:'+JSON.stringify({passed:true,checks:10}));i.form.requestSubmit();return;
    )JS" : "") + R"JS(
    Object.getOwnPropertyDescriptor(HTMLInputElement.prototype,'value').set.call(i,)JS" + CefWriteJSON(v, JSON_WRITER_DEFAULT).ToString() + R"JS();
    i.dispatchEvent(new Event('input',{bubbles:true}));await new Promise(r=>setTimeout(r,80));i.form.requestSubmit();})())JS";
  browser_->GetMainFrame()->ExecuteJavaScript(script, std::string(kCommandUrl), 0);
}
void CommandBar::ProbeLauncher(const std::shared_ptr<State>& prober, const std::string& url) {
  if (!prober || !prober->probe || !prober->little_launcher_probe || !detached_ || !open_ || !ready_ || !browser_) return;
  // The same trusted UI commands the bar sends for Enter and Escape.
  auto request = CefDictionaryValue::Create();
  request->SetString("method", url.empty() ? "command.close" : "command.submit");
  if (!url.empty()) request->SetString("url", url);
  auto value = CefValue::Create(); value->SetDictionary(request);
  auto text = CefValue::Create(); text->SetString(CefWriteJSON(value, JSON_WRITER_DEFAULT));
  browser_->GetMainFrame()->ExecuteJavaScript("cefQuery({request:" + CefWriteJSON(text, JSON_WRITER_DEFAULT).ToString() +
      ",onSuccess:()=>{},onFailure:(code,message)=>console.error(message)})", std::string(kCommandUrl), 0);
}
void CommandBar::ProbeCancel() {
  const auto s = state_.lock();
  if (!s || !s->probe || !s->browsing_probe || !open_ || !ready_ || !browser_) return;
  // Exercise the same trusted UI close command used by Escape.
  browser_->GetMainFrame()->ExecuteJavaScript(
      "cefQuery({request:JSON.stringify({method:'command.close'}),onSuccess:()=>{},onFailure:()=>console.error('Command cancel failed')})",
      std::string(kCommandUrl), 0);
}
bool CommandBar::Query(CefRefPtr<CefBrowser> b, CefRefPtr<CefFrame> f, int64_t query, const CefString& raw,
    bool persistent, CefRefPtr<CefMessageRouterBrowserSide::Callback> cb) {
  auto s = state_.lock();
  if (!s || !IsCommandUi(b->GetIdentifier(), s->command_id, f->IsMain(), f->GetURL().ToString())) { cb->Failure(403, "Untrusted command document"); return true; }
  if (raw.length() > 8192) { cb->Failure(413, "Command too large"); return true; }
  auto value = CefParseJSON(raw, JSON_PARSER_RFC);
  if (!value || value->GetType() != VTYPE_DICTIONARY) { cb->Failure(400, "Invalid command"); return true; }
  auto d = value->GetDictionary(); auto method = d->GetString("method").ToString();
  if (HandleUiFocus(s, b, method, d->GetSize(), persistent, cb)) return true;
  if ((method == "command.resize" || method == "command.present") &&
      (!open_ || !window_ || d->GetType("request") != VTYPE_INT || d->GetInt("request") != request_)) {
    cb->Failure(409, "Command opening changed"); return true;
  }
  if (method == "command.resize" && !persistent && d->GetSize() == 4 && d->GetType("rows") == VTYPE_INT &&
      d->GetType("error") == VTYPE_BOOL && d->GetInt("rows") >= 0 && d->GetInt("rows") <= kMaxRows) {
    if (!presented_ && !detached_ && !PopupOwnerIsForeground(s->window)) { Hide(false); cb->Failure(409, "Command owner is inactive"); return true; }
    const int requested = kBaseHeight + std::max(1, d->GetInt("rows")) * kRowHeight + (d->GetBool("error") ? kErrorHeight : 0);
    // Keep the viewport from shrinking/regrowing between local and async rows.
    height_ = presented_ ? std::max(height_,requested) : requested;
    Reposition(); window_->Layout(); prepared_ = true;
    if (!presented_) presentation_.Stage();
    cb->Success(PopupViewport(window_, request_)); return true;
  }
  if (method == "command.present" && !persistent && d->GetSize() == 3 && d->GetType("reducedMotion") == VTYPE_BOOL && prepared_) {
    if (!detached_ && !PopupOwnerIsForeground(s->window)) { Hide(false); cb->Failure(409, "Command owner is inactive"); return true; }
    if (!presented_) {
      presented_ = true; window_->Show(); window_->Activate(); FocusBrowser(browser_);
      presentation_.Reveal(d->GetBool("reducedMotion"), false);
      auto details = CefDictionaryValue::Create(); details->SetInt("request", request_);
      details->SetDouble("elapsed_ms", static_cast<double>(GetTickCount64() - opened_ms_));
      details->SetInt("height", window_->GetBounds().height); details->SetBool("layered", presentation_.layered());
      details->SetBool("composed", presentation_.composed());
      details->SetBool("foreground", GetForegroundWindow() == window_->GetWindowHandle());
      details->SetBool("keyboard_focus", GetAncestor(GetFocus(), GA_ROOT) == window_->GetWindowHandle());
      details->SetBool("command", true); s->Record("popup_presented", details);
    }
    cb->Success(PopupViewport(window_, request_, presentation_.composed())); return true;
  }
  if (method == "command.history" && !persistent && d->GetSize() == 2 && d->GetType("text") == VTYPE_STRING) {
    if (s->probe) s->Record("command_history_query");
    // Address suggestions from our browsing history (http(s) visits only).
    const auto text = d->GetString("text").ToString();
    if (text.size() > 200 || !s->history) { cb->Failure(text.size() > 200 ? 413 : 503, "History unavailable"); return true; }
    s->history->Suggest(text, 5, [cb, text](std::vector<Visit> visits) {
      auto list = CefListValue::Create();
      for (const auto& visit : visits) {
        auto row = CefDictionaryValue::Create();
        row->SetString("url", visit.url); row->SetString("title", visit.title.substr(0, 400));
        list->SetDictionary(list->GetSize(), row);
      }
      auto result = CefDictionaryValue::Create(); result->SetString("text", text); result->SetList("visits", list);
      auto value = CefValue::Create(); value->SetDictionary(result);
      cb->Success(CefWriteJSON(value, JSON_WRITER_DEFAULT));
    });
    return true;
  }
  if (method == "command.suggest" && !persistent && d->GetSize() == 3 && d->GetType("text") == VTYPE_STRING && d->GetType("request") == VTYPE_INT) {
    if (s->probe) s->Record("command_suggestion_query");
    const auto text = d->GetString("text").ToString();
    if (!open_ || d->GetInt("request") != request_ || !ValidSuggestionQuery(text)) { cb->Failure(400,"Invalid suggestion query"); return true; }
    const auto prefs = s->session ? s->session->preferences : nullptr;
    if (!prefs || !prefs->error().empty() || prefs->Get().search_engine != "google" || !prefs->Get().search_suggestions || s->probe) {
      cb->Success("{\"suggestions\":[]}"); return true;
    }
    if (!suggestions_) suggestions_ = std::make_shared<SearchSuggestions>();
    suggestion_query_ = query;
    suggestions_->Fetch(text,[weak = weak_from_this(), cb](std::vector<std::string> results) {
      const auto owner = weak.lock();
      if (!owner) return;
      auto values = CefListValue::Create();
      for (const auto& result : results) values->SetString(values->GetSize(),result);
      auto response = CefDictionaryValue::Create(); response->SetList("suggestions",values);
      auto json = CefValue::Create(); json->SetDictionary(response); cb->Success(CefWriteJSON(json,JSON_WRITER_DEFAULT));
      if (const auto state = owner->state_.lock()) {
        auto details = CefDictionaryValue::Create(); details->SetInt("count",static_cast<int>(results.size()));
        state->Record("command_suggestions",details);
      }
    });
    return true;
  }
  if (method == "command.action" && !persistent) {
    if (d->GetSize() != 3 || d->GetType("action") != VTYPE_STRING || d->GetType("request") != VTYPE_INT) {
      cb->Failure(400, "Invalid action shape"); return true;
    }
    if (detached_) { cb->Failure(409, "Open Sol to use this command"); return true; }
    const auto action = CommandShortcut(d->GetString("action").ToString());
    if (!action) { cb->Failure(400, "Unsupported command action"); return true; }
    if (!open_ || submitting_ || d->GetInt("request") != request_ || s->workspace->WindowClosing()) {
      cb->Failure(409, "Command bar is not ready"); return true;
    }
    const auto snapshot = s->workspace->Snapshot();
    if (!CommandAvailable(*action, s->content != nullptr, snapshot->GetString("windowKind") == "standalone",
                          snapshot->GetBool("canGoBack"), snapshot->GetBool("canGoForward"))) {
      cb->Failure(409, "Action is unavailable"); return true;
    }
    if (CommandNeedsPage(*action) && snapshot->GetString("activeId").ToString() != source_id_) {
      cb->Failure(409, "The active tab changed. Reopen the command bar."); return true;
    }
    const auto name = d->GetString("action").ToString();
    const auto preferences = s->session ? s->session->preferences : nullptr;
    const auto document_url = preferences ? EnabledDocumentUrl(name, preferences->Get().notes_provider, preferences->Get().integrations)
                                          : CommandDocumentUrl(name);
    if (!CommandDocumentUrl(name).empty() && document_url.empty()) { cb->Failure(409, "Integration is disabled"); return true; }
    cb->Success(Response());
    // Dismiss before opening a dialog/window, so focus restoration cannot cover it.
    Hide(true);
    if (!document_url.empty()) s->workspace->OpenLink(std::string(document_url), true);
    else ExecuteShortcut(s, *action);
    if (d->GetString("action") == "siteSearchSettings" && s->settings_window) {
      s->settings_window->ShowSection("siteSearch");
      if (s->probe) s->Record("site_search_command_route");
    }
    return true;
  }
  const bool argument = method == "command.submit" || method == "command.activate";
  if (d->GetType("method") != VTYPE_STRING || d->GetSize() != (argument ? 2u : 1u) ||
      (persistent != (method == "command.subscribe"))) { cb->Failure(400, "Invalid command shape"); return true; }
  if (method == "command.subscribe") { if (observer_) { cb->Failure(409, "Already subscribed"); return true; } observer_ = cb; observer_id_ = query; Publish(); return true; }
  if (method == "command.snapshot") { cb->Success(Response()); return true; }
  if (method == "command.close") { cb->Success(Response()); Hide(true); return true; }
  if (!open_ || submitting_ || s->workspace->WindowClosing()) { cb->Failure(409, "Command bar is not ready"); return true; }
  auto done = [weak = weak_from_this(), cb](int code, const std::string& error, CefRefPtr<CefDictionaryValue>) {
    if (auto o = weak.lock()) {
      o->submitting_ = false;
      if (code) { cb->Failure(code, error); return; }
      o->launcher_keep_ = o->detached_;  // The launcher's page opened: its Little Sol shows.
      cb->Success(o->Response()); o->Hide(true);
    }
  };
  if (method == "command.submit" && d->GetType("url") == VTYPE_STRING) {
    auto url = d->GetString("url").ToString(); auto current = s->workspace->Snapshot();
    if (const auto page = InternalPageForAddress(url); !page.empty()) url = std::string(page);
    if (!new_tab_ && current->GetString("activeId").ToString() != source_id_) { cb->Failure(409, "The active tab changed. Reopen the address bar."); return true; }
    if (!new_tab_ && url == source_url_ && !current->GetBool("activeCrashed")) { cb->Success(Response()); Hide(true); return true; }
    // History and Downloads keep one tab each: switch to it (or open it).
    if (IsInternalPageUrl(url)) {
      if (url == kHistoryUrl) s->workspace->OpenHistoryPage(); else s->workspace->OpenDownloadsPage();
      launcher_keep_ = detached_;
      cb->Success(Response()); Hide(true); return true;
    }
    submitting_ = true;
    if (new_tab_ || source_id_.empty()) s->workspace->OpenTyped(url, done); else s->workspace->Navigate(url, done);
    return true;
  }
  if (method == "command.activate" && d->GetType("id") == VTYPE_STRING) {
    auto current = s->workspace->Snapshot(); auto nodes = current->GetList("nodes"); bool found = false;
    for (size_t i = 0; i < nodes->GetSize(); ++i) { auto n = nodes->GetDictionary(i); if (n->GetString("id") == d->GetString("id") && n->GetString("kind") == "tab" && !n->GetBool("archived") && !n->GetBool("closing")) found = true; }
    if (!found) { cb->Failure(404, "Tab is no longer available"); return true; }
    auto command = CefDictionaryValue::Create(); command->SetString("method", "tree.activate"); command->SetString("id", d->GetString("id")); command->SetDouble("expectedRevision", current->GetDouble("treeRevision"));
    submitting_ = true; s->workspace->Handle(command, done); return true;
  }
  cb->Failure(400, "Unsupported command"); return true;
}
}  // namespace red
