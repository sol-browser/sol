#include "windows/sidebar_actions.h"
#include "app/session.h"
#include "windows/pointer.h"
#include "windows/sidebar_peek.h"
#include <dwmapi.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <thread>
#include "include/base/cef_callback.h"
#include "include/cef_app.h"
#include "include/cef_devtools_message_observer.h"
#include "include/cef_id_mappers.h"
#include "include/cef_parser.h"
#include "include/wrapper/cef_closure_task.h"
#include "navigation/file_url.h"
#include "navigation/shortcuts.h"
#include "trust_policy.h"
#include "windows/focus.h"
#include "windows/embedded_content.h"
#include "windows/panel_popup.h"
#include "windows/sidebar_menu.h"
#include "windows/window_close.h"
#include "windows/workspace_layout.h"
#include "workspace/controller.h"

namespace red {
namespace {
constexpr int kGap = 6;          // Space between the window edge and the floating sidebar (DIP).
constexpr int kOutsideKeepOpenWidth = 144;  // Left-edge overshoot allowance (DIP), never an opening trigger.
constexpr int kPoll = 15;        // Pointer polling interval near the edge/while open (ms).
constexpr int kIdlePoll = 60;    // Far from the edge; edge passes are recovered from mouse history.
constexpr int kBlockedPoll = 120;  // Minimized, fullscreen page or closing.
constexpr int kNearEdge = 220;   // Pointer distance from the edge that polls fast (DIP).
constexpr double kSlideMs = 170;  // Slide duration.
constexpr double kOpeningHoldMs = 200;  // A quick edge pass still gets a chance to open.

bool VisibleEdgeOnPath(POINT from, POINT to, const RECT& strip, HWND root, HWND host) {
  auto visible = [&](POINT point) {
    if (!PtInRect(&strip, point)) return false;
    const HWND under = GetAncestor(WindowFromPoint(point), GA_ROOT);
    return under == root || (host && under == host);
  };
  if (visible(from) || visible(to)) return true;
  if (from.x == to.x) return false;
  const LONG x = strip.left + (strip.right - strip.left) / 2;
  const double fraction = static_cast<double>(x - from.x) / (to.x - from.x);
  if (fraction < 0 || fraction > 1) return false;
  return visible(POINT{x, static_cast<LONG>(std::lround(from.y + fraction * (to.y - from.y)))});
}

bool RecentEdgeEntry(POINT cursor, POINT previous, const RECT& strip, HWND root, HWND host, DWORD since, DWORD now, bool& from_history) {
  if (VisibleEdgeOnPath(previous, cursor, strip, root, host)) return true;
  // Recover a hit-and-return that happened entirely between two polls. Use
  // display points and Microsoft's signed-coordinate normalization for a
  // monitor to the left/above the primary. No hook or input interception.
  const int left = GetSystemMetrics(SM_XVIRTUALSCREEN), top = GetSystemMetrics(SM_YVIRTUALSCREEN);
  if (left < -32768 || top < -32768 || left + GetSystemMetrics(SM_CXVIRTUALSCREEN) > 32768 ||
      top + GetSystemMetrics(SM_CYVIRTUALSCREEN) > 32768) return false;
  MOUSEMOVEPOINT input{cursor.x & 0xffff, cursor.y & 0xffff, 0, 0}, history[64]{};
  const int count = GetMouseMovePointsEx(sizeof(input), &input, history, 64, GMMP_USE_DISPLAY_POINTS);
  POINT newer = cursor;
  const LONG window = static_cast<LONG>(std::min<DWORD>(now - since + kPoll, 120));
  for (int i = 0; i < count; ++i) {
    const LONG age = static_cast<LONG>(now - history[i].time);
    if (age > window) break;
    if (age < -kPoll) continue;
    const POINT older{history[i].x > 32767 ? history[i].x - 65536 : history[i].x,
                      history[i].y > 32767 ? history[i].y - 65536 : history[i].y};
    if (VisibleEdgeOnPath(older, newer, strip, root, host)) { from_history = true; return true; }
    newer = older;
  }
  return false;
}

bool FullscreenBlocksPeek(const std::shared_ptr<State>& state) {
  return (state->embedded && state->embedded->IsPageFullscreen()) ||
      (state->window->IsFullscreen() && (!state->embedded || !state->embedded->IsBrowserFullscreen()));
}

class PeekFrameObserver final : public CefDevToolsMessageObserver {
 public:
  explicit PeekFrameObserver(std::weak_ptr<SidebarPeek> owner) : owner_(std::move(owner)) {}
  void OnDevToolsMethodResult(CefRefPtr<CefBrowser>, int message, bool success, const void*, size_t size) override {
    if (auto owner = owner_.lock()) owner->PreparedFrame(message, success, size);
  }
 private:
  std::weak_ptr<SidebarPeek> owner_;
  IMPLEMENT_REFCOUNTING(PeekFrameObserver);
};

std::string Json(CefRefPtr<CefDictionaryValue> dict) {
  auto value = CefValue::Create(); value->SetDictionary(dict); return CefWriteJSON(value, JSON_WRITER_DEFAULT).ToString();
}

// Trusted UI: Chrome style brings Chrome's page menu (Inspect, View source,
// Save as, Print, Search…). Like the internal pages, offer none of it; keep
// only text editing for the rename fields.
bool EditingCommand(int id) {
  static const std::vector<int> allowed = [] {
    std::vector<int> ids{MENU_ID_UNDO, MENU_ID_REDO, MENU_ID_CUT, MENU_ID_COPY, MENU_ID_PASTE, MENU_ID_PASTE_MATCH_STYLE,
                         MENU_ID_DELETE, MENU_ID_SELECT_ALL};
    for (const char* name : {"IDC_CONTENT_CONTEXT_UNDO", "IDC_CONTENT_CONTEXT_REDO", "IDC_CONTENT_CONTEXT_CUT",
                             "IDC_CONTENT_CONTEXT_COPY", "IDC_CONTENT_CONTEXT_PASTE", "IDC_CONTENT_CONTEXT_PASTE_AND_MATCH_STYLE",
                             "IDC_CONTENT_CONTEXT_DELETE", "IDC_CONTENT_CONTEXT_SELECTALL"})
      if (const int mapped = cef_id_for_command_id_name(name); mapped >= 0) ids.push_back(mapped);
    return ids;
  }();
  return std::find(allowed.begin(), allowed.end(), id) != allowed.end();
}
void KeepEditingCommands(CefRefPtr<CefMenuModel> model) {
  for (size_t i = model->GetCount(); i-- > 0;) {
    const auto type = model->GetTypeAt(i);
    if (type == MENUITEMTYPE_SEPARATOR) continue;
    if (type == MENUITEMTYPE_SUBMENU || !EditingCommand(model->GetCommandIdAt(i))) model->RemoveAt(i);
  }
  // No leading, trailing or doubled separators.
  for (size_t i = model->GetCount(); i-- > 0;) {
    const bool separator = model->GetTypeAt(i) == MENUITEMTYPE_SEPARATOR;
    const bool edge = i == 0 || i + 1 == model->GetCount();
    if (separator && (edge || model->GetTypeAt(i + 1) == MENUITEMTYPE_SEPARATOR)) model->RemoveAt(i);
  }
}
class PeekClient final : public CefClient, public CefLifeSpanHandler, public CefRequestHandler, public CefKeyboardHandler,
    public CefDragHandler, public CefDisplayHandler, public CefContextMenuHandler, public CefMessageRouterBrowserSide::Handler {
 public:
  PeekClient(std::weak_ptr<SidebarPeek> owner, std::weak_ptr<State> state) : owner_(std::move(owner)), state_(std::move(state)) {
    router_ = CefMessageRouterBrowserSide::Create(CefMessageRouterConfig()); router_->AddHandler(this, false);
  }
  ~PeekClient() override { router_->RemoveHandler(this); }
  CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return this; }
  CefRefPtr<CefRequestHandler> GetRequestHandler() override { return this; }
  CefRefPtr<CefKeyboardHandler> GetKeyboardHandler() override { return this; }
  CefRefPtr<CefDragHandler> GetDragHandler() override { return this; }
  CefRefPtr<CefDisplayHandler> GetDisplayHandler() override { return this; }
  CefRefPtr<CefContextMenuHandler> GetContextMenuHandler() override { return this; }
  void OnBeforeContextMenu(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, CefRefPtr<CefContextMenuParams> params,
                           CefRefPtr<CefMenuModel> model) override {
    const int offered = static_cast<int>(model->GetCount());
    if (!(params->GetTypeFlags() & (CM_TYPEFLAG_EDITABLE | CM_TYPEFLAG_SELECTION))) model->Clear();
    else KeepEditingCommands(model);
    if (auto s = state_.lock(); s && s->probe) {
      // Evidence: what Chrome offered and what remains; the probe shows none.
      const int inspect = cef_id_for_command_id_name("IDC_CONTENT_CONTEXT_INSPECTELEMENT");
      auto data = CefDictionaryValue::Create(); data->SetInt("offered", offered);
      data->SetInt("kept", static_cast<int>(model->GetCount()));
      data->SetBool("inspect_kept", inspect >= 0 && model->GetIndexOf(inspect) >= 0);
      data->SetBool("editable", (params->GetTypeFlags() & CM_TYPEFLAG_EDITABLE) != 0);
      s->Record("peek_context_menu", data);
      model->Clear();
    }
  }
  // Probe evidence from the fixed probe script only.
  bool OnConsoleMessage(CefRefPtr<CefBrowser>, cef_log_severity_t, const CefString& message, const CefString&, int) override {
    const auto text = message.ToString();
    if (auto s = state_.lock(); s && s->probe && text.starts_with("RED_PEEK:")) {
      auto data = CefDictionaryValue::Create(); data->SetString("result", text.substr(9, 1000)); s->Record("peek_document", data);
    }
    return false;
  }
  void OnAfterCreated(CefRefPtr<CefBrowser> b) override { if (auto o = owner_.lock()) o->Created(b); }
  void OnBeforeClose(CefRefPtr<CefBrowser> b) override { router_->OnBeforeClose(b); if (auto o = owner_.lock()) o->Closed(b->GetIdentifier()); }
  // The helper owns only its child window; never send WM_CLOSE to the root.
  bool DoClose(CefRefPtr<CefBrowser> b) override {
    const HWND handle = b->GetHost()->GetWindowHandle();
    CefPostTask(TID_UI, base::BindOnce([](HWND window) { if (IsWindow(window)) DestroyWindow(window); }, handle));
    return true;
  }
  bool OnBeforePopup(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, int, const CefString&, const CefString&,
      cef_window_open_disposition_t, bool, const CefPopupFeatures&, CefWindowInfo&, CefRefPtr<CefClient>&,
      CefBrowserSettings&, CefRefPtr<CefDictionaryValue>&, bool*) override { return true; }
  bool OnBeforeBrowse(CefRefPtr<CefBrowser> b, CefRefPtr<CefFrame> f, CefRefPtr<CefRequest> r, bool, bool) override {
    router_->OnBeforeBrowse(b, f); return !f->IsMain() || r->GetURL().ToString() != kPeekUrl;
  }
  void OnRenderProcessTerminated(CefRefPtr<CefBrowser> b, TerminationStatus, int, const CefString&) override {
    router_->OnRenderProcessTerminated(b); if (auto o = owner_.lock()) o->Hide();
  }
  bool OnPreKeyEvent(CefRefPtr<CefBrowser>, const CefKeyEvent& e, CefEventHandle, bool*) override {
    if (e.type != KEYEVENT_RAWKEYDOWN) return false;
    const auto state = state_.lock();
    const auto action = ShortcutKeyFor(state, e.windows_key_code, (e.modifiers & EVENTFLAG_CONTROL_DOWN) != 0,
        (e.modifiers & EVENTFLAG_SHIFT_DOWN) != 0, (e.modifiers & EVENTFLAG_ALT_DOWN) != 0);
    if (action == Shortcut::Fullscreen) {
      if (auto o = owner_.lock()) o->Hide();
      if (auto s = state_.lock()) return ExecuteShortcut(s, Shortcut::Fullscreen);
      return false;
    }
    if (e.windows_key_code != VK_ESCAPE) return action && state ? ExecuteShortcut(state, *action) : false;
    // A tab drag or a rename in the sidebar cancels on Escape in the page;
    // hiding the sidebar instead would leave the drag running unseen.
    if (state && (state->tab_dragging || (state->ui_editing && state->keyboard_target == state->peek_id))) return false;
    if (auto o = owner_.lock()) {
      if (o->is_resizing()) { o->EndResize(true); return true; }
      o->Hide();
    }
    return true;
  }
  // Local files dragged over the peek, from the OS drag data (like the shell).
  bool OnDragEnter(CefRefPtr<CefBrowser>, CefRefPtr<CefDragData> data, DragOperationsMask) override {
    std::vector<std::string> files;
    if (data && data->IsFile()) {
      std::vector<CefString> names; data->GetFileNames(names);
      for (const auto& name : names) {
        const auto url = LocalFileUrl(std::filesystem::path(name.ToWString()));
        if (!url.empty() && files.size() < 10) files.push_back(url);
      }
    }
    if (auto o = owner_.lock()) o->DragEntered(std::move(files));
    return false;
  }
  bool OnProcessMessageReceived(CefRefPtr<CefBrowser> b, CefRefPtr<CefFrame> f, CefProcessId p, CefRefPtr<CefProcessMessage> m) override {
    return router_->OnProcessMessageReceived(b, f, p, m);
  }
  bool OnQuery(CefRefPtr<CefBrowser> b, CefRefPtr<CefFrame> f, int64_t q, const CefString& r, bool p, CefRefPtr<Callback> cb) override {
    if (auto o = owner_.lock()) return o->Query(b, f, q, r, p, cb); return false;
  }
  void OnQueryCanceled(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, int64_t q) override { if (auto o = owner_.lock()) o->Cancel(q); }
 private:
  std::weak_ptr<SidebarPeek> owner_;
  std::weak_ptr<State> state_;
  CefRefPtr<CefMessageRouterBrowserSide> router_;
  IMPLEMENT_REFCOUNTING(PeekClient);
};
// The host draws nothing (no redirection surface, so uncovered parts are
// transparent) and lets the pointer through wherever the sidebar is not.
LRESULT CALLBACK HostProc(HWND window, UINT message, WPARAM w, LPARAM l) {
  switch (message) {
    case WM_NCHITTEST: {
      const POINT point{static_cast<short>(LOWORD(l)), static_cast<short>(HIWORD(l))};
      for (HWND child = GetWindow(window, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT)) {
        RECT box; GetWindowRect(child, &box);
        if (!IsWindowVisible(child) || !PtInRect(&box, point)) continue;
        // Recurse through our wrappers; only the actual Chrome child is an
        // input surface (including while its frame is clipped during motion).
        if (GetClassLongPtrW(child, GCLP_WNDPROC) == reinterpret_cast<ULONG_PTR>(HostProc) &&
            SendMessageW(child, WM_NCHITTEST, w, l) == HTTRANSPARENT) continue;
        return HTCLIENT;
      }
      return HTTRANSPARENT;
    }
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: { PAINTSTRUCT paint; BeginPaint(window, &paint); EndPaint(window, &paint); return 0; }
    case WM_CLOSE: return 0;  // Only SidebarPeek destroys it.
  }
  return DefWindowProcW(window, message, w, l);
}
HWND CreateHost(HWND owner, bool child = false) {
  static const ATOM atom = [] {
    WNDCLASSEXW wc{sizeof(wc)}; wc.lpfnWndProc = HostProc; wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW); wc.lpszClassName = L"RedSidebarPeekHost";
    return RegisterClassExW(&wc);
  }();
  if (!atom) return nullptr;
  return CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP | (child ? 0 : WS_EX_TOOLWINDOW), L"RedSidebarPeekHost", L"Sol sidebar",
                         (child ? WS_CHILD | WS_VISIBLE : WS_POPUP) | WS_CLIPCHILDREN,
                         child ? 0 : -4000, 0, 1, 1, owner, nullptr, GetModuleHandleW(nullptr), nullptr);
}
// Native DWM decoration is deliberately absent. The browser gets an explicit
// rounded region; the main-root compositor paints its outside shadow.
void Style(HWND host) {
  const DWM_WINDOW_CORNER_PREFERENCE corners = DWMWCP_DONOTROUND;
  DwmSetWindowAttribute(host, DWMWA_WINDOW_CORNER_PREFERENCE, &corners, sizeof(corners));
  const DWMNCRENDERINGPOLICY rendering = DWMNCRP_DISABLED;
  DwmSetWindowAttribute(host, DWMWA_NCRENDERING_POLICY, &rendering, sizeof(rendering));
  const COLORREF border = DWMWA_COLOR_NONE;
  DwmSetWindowAttribute(host, DWMWA_BORDER_COLOR, &border, sizeof(border));
  const BOOL no_transitions = TRUE;
  DwmSetWindowAttribute(host, DWMWA_TRANSITIONS_FORCEDISABLED, &no_transitions, sizeof(no_transitions));
}
// Parked hosts stay "shown" off screen: moving one into place shows it with
// no window-show path at all.
constexpr int kParked = -32000;
}  // namespace

// F11 has no toolbar: use the same small inset at the top and bottom.
CefRect SidebarPeek::Target() const {
  auto s = state_.lock(); if (!s || !s->window) return {};
  const auto client = s->window->GetClientAreaBoundsInScreen();
  const int top = s->embedded && s->embedded->IsBrowserFullscreen() ? kGap : kToolbarHeight;
  return CefRect(kGap, top, SidebarWidth(s->sidebar_width, client.width),
                 std::max(120, client.height - top - kContentInset));
}
void SidebarPeek::ConfigureHost() {
  const auto s = state_.lock();
  if (!s || !s->window || !host_) return;
  const bool child = s->embedded && s->embedded->IsBrowserFullscreen() && !s->embedded->IsPageFullscreen();
  if (child == inline_host_) return;
  const HWND root = s->window->GetWindowHandle();
  const auto style = GetWindowLongPtrW(host_, GWL_STYLE);
  if (s->embedded) s->embedded->RestorePopupOwners(host_);
  if (child) {
    SetWindowLongPtrW(host_, GWL_STYLE, (style & ~WS_POPUP) | WS_CHILD);
    SetParent(host_, root);
  } else {
    if (s->embedded) s->embedded->SetSidebarOverlayVisible(false);
    SetParent(host_, nullptr);
    SetWindowLongPtrW(host_, GWL_STYLE, (style & ~WS_CHILD) | WS_POPUP);
    SetWindowLongPtrW(host_, GWLP_HWNDPARENT, reinterpret_cast<LONG_PTR>(root));
  }
  inline_host_ = child;
  SetWindowRgn(host_, nullptr, TRUE);
  SetWindowPos(host_, nullptr, kParked, kParked, 0, 0,
      SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
}
void SidebarPeek::SetEnabled(bool enabled) {
  if (shutdown_) return;
  if (enabled == enabled_) return;
  enabled_ = enabled; hover_ = away_ = 0; resizing_ = false;
  pointer_sample_valid_ = false; opening_hold_until_ = 0;
  pending_show_ = false; frame_request_ = 0; ++prepare_generation_;
  if (enabled) { Watch(++watch_); return; }
  // Docked again: the docked sidebar takes over at once.
  open_ = false; ++motion_; motion_done_ = true; Park();
}
void SidebarPeek::Watch(unsigned generation) {
  auto s = state_.lock();
  if (generation != watch_ || !enabled_ || shutdown_ || !s || !s->window || !s->workspace) return;
  CefPostDelayedTask(TID_UI, base::BindOnce([](std::weak_ptr<SidebarPeek> weak, unsigned generation) {
    if (auto self = weak.lock()) self->Watch(generation);
  }, weak_from_this(), generation), poll_ms_);
  if (s->workspace->WindowClosing() || FullscreenBlocksPeek(s) || s->window->IsMinimized()) {
    pointer_sample_valid_ = false; poll_ms_ = kBlockedPoll;
    if (open_ || pending_show_) Hide(); return;
  }
  POINT cursor;
  if (!GetCursorPos(&cursor)) { pointer_sample_valid_ = false; poll_ms_ = kIdlePoll; return; }
  const HWND root = s->window->GetWindowHandle();
  RECT client; GetClientRect(root, &client);
  POINT origin{0, 0}; ClientToScreen(root, &origin); OffsetRect(&client, origin.x, origin.y);
  const float scale = s->window->GetDisplay()->GetDeviceScaleFactor();
  const int trigger_top = client.top + (s->embedded && s->embedded->IsBrowserFullscreen() ? 0 : std::lround(kToolbarHeight * scale));
  // An inactive Red may still be visible. Hit-test the actual window under
  // the pointer so hovering another app that covers our edge cannot open us.
  const HWND hovered = GetAncestor(WindowFromPoint(cursor), GA_ROOT);
  const bool over_root = hovered == root, over_peek = host_ && hovered == host_;
  // The whole left gutter beside the page (kContentInset) is the trigger.
  const bool edge = (over_root || over_peek) && !s->sidebar_peek_probe && cursor.x >= client.left && cursor.x < client.left + std::lround(kContentInset * scale) &&
      cursor.y >= trigger_top && cursor.y < client.bottom;
  const RECT trigger{client.left, trigger_top, client.left + std::lround(kContentInset * scale), client.bottom};
  const DWORD checked = GetTickCount();
  bool swept = false, from_history = false;
  if (!open_ && !pending_show_ && !edge && !s->sidebar_peek_probe && pointer_sample_valid_ &&
      EqualRect(&trigger, &previous_trigger_) && checked - previous_pointer_check_ <= 120) {
    swept = RecentEdgeEntry(cursor, previous_cursor_, trigger, root, host_, previous_pointer_check_, checked, from_history);
  }
  previous_cursor_ = cursor; previous_trigger_ = trigger;
  previous_pointer_check_ = checked; pointer_sample_valid_ = true;
  // Poll fast only when it can matter: near the left edge, or while the
  // sidebar is opening, open or being resized. The swept-entry check above
  // reads up to 120 ms of mouse history, longer than the idle interval.
  const bool near_edge = cursor.x < client.left + std::lround(kNearEdge * scale) &&
      cursor.x > client.left - std::lround(kOutsideKeepOpenWidth * scale) && cursor.y >= client.top && cursor.y < client.bottom;
  poll_ms_ = open_ || pending_show_ || resizing_ || near_edge ? kPoll : kIdlePoll;
  // An accidental overshoot onto a neighbouring monitor must not dismiss a
  // sidebar that is already opening/open. This screen-space strip accepts the
  // pointer over another window without extending our HWND or stealing input.
  // Both client and cursor are physical pixels; scale only the DIP allowance.
  const RECT outside_left{client.left - std::lround(kOutsideKeepOpenWidth * scale),
      trigger_top, client.left, client.bottom};
  const bool keep_outside = PtInRect(&outside_left, cursor) != FALSE;
  if (!open_) {
    const double now = Now();
    if (edge || swept) opening_hold_until_ = now + kOpeningHoldMs;
    const bool hold = now < opening_hold_until_;
    const auto target = Target();
    const RECT future{client.left + std::lround(target.x * scale), client.top + std::lround(target.y * scale),
        client.left + std::lround((target.x + target.width) * scale), client.top + std::lround((target.y + target.height) * scale)};
    const bool entering_panel = (over_root || over_peek) && PtInRect(&future, cursor);
    if (!edge && !keep_outside && !entering_panel && !hold && pending_show_ && !s->sidebar_peek_probe) Hide();
    if ((edge || swept || hold) && !pending_show_) {
      if (swept) {
        auto details = CefDictionaryValue::Create(); details->SetBool("history", from_history);
        s->Record("sidebar_peek_swept_entry", details);
      }
      Show();
    }
    return;
  }
  if (motion_done_ && !parked_) PlaceHost();  // Follow the browser window if it moved.
  RECT peek{};
  if (host_) GetWindowRect(host_, &peek);
  InflateRect(&peek, std::lround(10 * scale), std::lround(10 * scale));
  // Pointer capture delivers moves over the native page. Hold the floating
  // sidebar open for that drag; a missed release cannot leave the latch stuck.
  if (resizing_) {
    if (GetAncestor(GetForegroundWindow(), GA_ROOTOWNER) != root || (GetAsyncKeyState(VK_ESCAPE) & 0x8000)) EndResize(true);
    else if (!PrimaryButtonDown()) EndResize(false);
    else {
      const int width = SidebarWidth(resize_start_width_ + static_cast<int>(std::lround((cursor.x - resize_start_x_) / scale)),
          s->window->GetClientAreaBoundsInScreen().width);
      if (width != s->sidebar_width) {
        s->workspace->SetSidebarWidth(width, false, [](int, const std::string&, CefRefPtr<CefDictionaryValue>) {});
        PlaceHost();
      }
    }
  }
  const bool editing = s->ui_editing && s->keyboard_target == s->peek_id;
  const bool busy = editing || resizing_ || (s->tab_dragging && PrimaryButtonDown()) || (s->sidebar_menu && s->sidebar_menu->is_open()) || (s->panel_popup && s->panel_popup->is_open());
  const bool inside = edge || keep_outside || ((over_root || over_peek) && PtInRect(&peek, cursor));
  // Probe runs keep it open: the pointer is the person's, not the test's.
  away_ = inside || busy || Now() < opening_hold_until_ || s->sidebar_peek_probe ? 0 : away_ + 1;
  if (away_ * kPoll >= 240) Hide();  // ~240 ms away from it.
}
bool SidebarPeek::Ensure() {
  auto s = state_.lock();
  if (!s || !s->window || shutdown_) return false;
  if (hwnd_) return true;
  if (creating_ || !s->ui_context) return false;
  // A tight clipping host contains only the browser. Passive decoration is
  // composed on the root, never in padded non-layered child HWNDs.
  if (!host_) host_ = CreateHost(s->window->GetWindowHandle());
  if (!host_) return false;
  if (!frame_) frame_ = CreateHost(host_, true);
  if (!frame_) return false;
  Style(host_);
  const auto target = Target();
  const float scale = s->window->GetDisplay()->GetDeviceScaleFactor();
  const int width = std::max(1, static_cast<int>(std::lround(target.width * scale)));
  const int height = std::max(1, static_cast<int>(std::lround(target.height * scale)));
  SetWindowPos(host_, nullptr, kParked, kParked, width, height, SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW);
  SetWindowPos(frame_, nullptr, 0, 0, width, height, SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW);
  creating_ = true;
  CefWindowInfo info;
  info.SetAsChild(frame_, CefRect(0, 0, width, height));
  info.runtime_style = CEF_RUNTIME_STYLE_CHROME;
  const auto background = Themed({233, 224, 238}, s->theme, s->theme_dark);
  CefBrowserSettings settings; settings.background_color = CefColorSetARGB(255, background.r, background.g, background.b);
  // Trusted UI document: the renderer installs the IPC bridge only for these.
  auto extra = CefDictionaryValue::Create(); extra->SetBool("trusted_ui", true);
  if (!CefBrowserHost::CreateBrowser(info, new PeekClient(weak_from_this(), state_), std::string(kPeekUrl), settings, extra, s->ui_context)) {
    creating_ = false; pending_show_ = false; retry_after_ = Now() + 250;
  }
  return false;
}
void SidebarPeek::Preload() { Ensure(); }
void SidebarPeek::Show() {
  auto s = state_.lock();
  if (!s || !s->window || !s->workspace || !enabled_ || open_ || pending_show_ || shutdown_ || Now() < retry_after_) return;
  if (s->workspace->WindowClosing() || s->window->IsMinimized() || FullscreenBlocksPeek(s)) return;
  pending_show_ = true;
  if (!Ensure()) return;
  ConfigureHost();
  // Re-entry during slide-out cancels its ticks before touching the viewport.
  ++motion_; motion_done_ = true; Park();
  // Warm the full-sized viewport while still parked, including after idle/DPI
  // changes. Never reveal a resized 1x1 surface and hope its renderer catches up.
  const auto target = Target();
  const float scale = s->window->GetDisplay()->GetDeviceScaleFactor();
  const int width = std::max(1, static_cast<int>(std::lround(target.width * scale)));
  const int height = std::max(1, static_cast<int>(std::lround(target.height * scale)));
  SizeFrame(width, height, scale);
  prepare_revision_ = s->workspace->Snapshot()->GetInt("revision");
  const auto generation = ++prepare_generation_;
  Publish();
  CefPostDelayedTask(TID_UI, base::BindOnce([](std::weak_ptr<SidebarPeek> weak, unsigned generation) {
    if (auto self = weak.lock(); self && self->prepare_generation_ == generation && self->pending_show_) {
      self->pending_show_ = false; self->frame_request_ = 0; self->retry_after_ = Now() + 250;
      if (auto s = self->state_.lock()) s->Record("sidebar_peek_frame_timeout");
    }
  }, weak_from_this(), generation), 2000);
}
void SidebarPeek::EndResize(bool cancel) {
  if (!resizing_) return;
  resizing_ = false;
  if (const auto s = state_.lock(); s && s->workspace) {
    const int width = cancel ? resize_start_width_ : s->sidebar_width;
    s->workspace->SetSidebarWidth(width, true, [](int, const std::string&, CefRefPtr<CefDictionaryValue>) {});
    Publish(); PlaceHost();
  }
}
void SidebarPeek::Hide() {
  EndResize(true);
  resizing_ = false;
  opening_hold_until_ = 0;
  pending_show_ = false; frame_request_ = 0; ++prepare_generation_;
  if (!open_) return;
  open_ = false; hover_ = 0;
  if (auto s = state_.lock()) {
    if (s->keyboard_target == s->peek_id) s->ui_editing = false;
    s->Record("sidebar_peek_hidden");
    // Give the browser window and its page the focus back if the sidebar had it.
    if (host_ && s->window && (GetForegroundWindow() == host_ ||
        (inline_host_ && browser_ && BrowserOwnsKeyboard(s, browser_)))) {
      if (!inline_host_) s->window->Activate();
      if (s->content) FocusBrowser(s->content);
    }
  }
  Slide(false, false);
}
void SidebarPeek::PrepareFrame() {
  if (!pending_show_ || !browser_ || frame_request_) return;
  // Page.captureScreenshot's capturer handle requests a fresh compositor frame
  // even for a parked/occluded document (pinned PageHandler implementation).
  // Use completion as a paint barrier; discard the PNG, never store/display it.
  auto params = CefDictionaryValue::Create(); params->SetString("format", "png");
  params->SetBool("fromSurface", true); params->SetBool("captureBeyondViewport", false);
  params->SetBool("optimizeForSpeed", true);
  frame_request_ = browser_->GetHost()->ExecuteDevToolsMethod(0, "Page.captureScreenshot", params);
}
void SidebarPeek::PreparedFrame(int message, bool success, size_t size) {
  if (!frame_request_ || message != frame_request_) return;
  frame_request_ = 0;
  if (!pending_show_ || shutdown_) return;
  if (!success || size == 0 || size > 24 * 1024 * 1024) {
    pending_show_ = false; retry_after_ = Now() + 250; return;
  }
  auto s = state_.lock();
  if (!enabled_ || !s || !s->window || !s->workspace || s->window->IsMinimized() || FullscreenBlocksPeek(s) || s->workspace->WindowClosing()) {
    pending_show_ = false; return;
  }
  const auto target = Target();
  const float scale = s->window->GetDisplay()->GetDeviceScaleFactor();
  RECT viewport{}; GetClientRect(hwnd_, &viewport);
  if (viewport.right != std::lround(target.width * scale) || viewport.bottom != std::lround(target.height * scale)) {
    pending_show_ = false; Show(); return;  // Root size/DPI changed while preparing.
  }
  pending_show_ = false; ++prepare_generation_;
  open_ = true; away_ = 0;
  Slide(true, false);
  s->Record("sidebar_peek_frame_ready"); s->Record("sidebar_peek_shown");
}
// The native clipping host has no padding or DWM decoration: those uncovered
// child HWND areas would expose the root shell background over the site in F11.
void SidebarPeek::SizeFrame(int width, int height, float scale) {
  if (!frame_ || !hwnd_) return;
  RECT frame; GetClientRect(frame_, &frame);
  if (frame.right != width || frame.bottom != height)
    SetWindowPos(frame_, nullptr, 0, 0, width, height,
        SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
  RECT child; GetClientRect(hwnd_, &child);
  const bool changed = child.right != width || child.bottom != height || corner_scale_ != scale;
  if (changed) {
    SetWindowPos(hwnd_, nullptr, 0, 0, width, height, SWP_NOZORDER | SWP_NOACTIVATE);
    const int diameter = std::max(2, static_cast<int>(std::lround(12 * scale)));
    const HRGN region = CreateRoundRectRgn(0, 0, width + 1, height + 1, diameter, diameter);
    if (region && !SetWindowRgn(hwnd_, region, FALSE)) DeleteObject(region);
    corner_scale_ = scale;
  }
}
void SidebarPeek::PlaceHost() { PlaceHost(shown_); }
void SidebarPeek::PlaceHost(double progress) {
  auto s = state_.lock(); if (!s || !s->window || !host_ || parked_) return;
  ConfigureHost();
  const auto target = Target();
  const float scale = s->window->GetDisplay()->GetDeviceScaleFactor();
  POINT origin{0, 0}; ClientToScreen(s->window->GetWindowHandle(), &origin);
  const int x = origin.x + static_cast<int>(std::lround(target.x * scale)), y = origin.y + static_cast<int>(std::lround(target.y * scale));
  const int width = static_cast<int>(std::lround(target.width * scale)), height = static_cast<int>(std::lround(target.height * scale));
  const int visible = std::max(1, static_cast<int>(std::lround(width * progress)));
  const int host_width = visible, host_height = height;
  RECT now; GetWindowRect(host_, &now);
  const bool host_changed = now.left != x || now.top != y || now.right - now.left != host_width || now.bottom - now.top != host_height;
  // Prepare the common frame while still parked before bringing the outer
  // host on screen; never expose a full-width intermediate first frame.
  SizeFrame(width, height, scale);
  POINT position{0, 0}; MapWindowPoints(frame_, host_, &position, 1);
  if (position.x != visible - width || position.y != 0)
    SetWindowPos(frame_, nullptr, visible - width, 0, 0, 0, SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOSIZE);
  if (s->embedded) {
    const RECT panel{x - origin.x + visible - width, y - origin.y, x - origin.x + visible, y - origin.y + height};
    const bool ready = s->embedded->ShowSidebarShadow(panel, scale, progress);
    if (!shadow_recorded_) {
      s->Record(ready ? "sidebar_peek_shadow_ready" : "sidebar_peek_shadow_failed"); shadow_recorded_ = true;
    }
  }
  if (host_changed) {
    // Tight explicit region: no native shadow or exposed background padding.
    const int diameter = std::max(2, static_cast<int>(std::lround(12 * scale)));
    const HRGN region = CreateRoundRectRgn(0, 0, host_width + 1, host_height + 1, diameter, diameter);
    if (region && !SetWindowRgn(host_, region, FALSE)) DeleteObject(region);
  }
  if (inline_host_ && s->embedded) s->embedded->SetSidebarOverlayVisible(true);
  if (host_changed || (inline_host_ && GetWindow(host_, GW_HWNDPREV)))
    SetWindowPos(host_, inline_host_ ? HWND_TOP : nullptr, inline_host_ ? x - origin.x : x, inline_host_ ? y - origin.y : y,
        host_width, host_height, SWP_NOACTIVATE | SWP_NOOWNERZORDER | (inline_host_ ? 0 : SWP_NOZORDER));
}
// |progress| 0 = just left of the host, 1 = in place. The viewport stays full
// sized; the root shadow receives the same panel rectangle/progress per tick.
void SidebarPeek::Place(double progress) {
  auto s = state_.lock(); if (!s || !s->window || !hwnd_ || !host_) return;
  shown_ = progress; parked_ = false;
  PlaceHost(progress);
  RECT host; GetWindowRect(host_, &host);
  if (s->sidebar_peek_probe && progress > .3 && progress < .7 && !probe_clip_recorded_) {
    // Mid-slide evidence: the sidebar starts left of its host, which clips it.
    probe_clip_recorded_ = true;
    POINT origin{0, 0}; ClientToScreen(s->window->GetWindowHandle(), &origin);
    RECT frame; GetWindowRect(hwnd_, &frame);
    auto data = CefDictionaryValue::Create();
    data->SetInt("sidebar_left", frame.left); data->SetInt("host_left", host.left); data->SetInt("root_left", origin.x);
    data->SetBool("child_of_host", GetParent(hwnd_) == frame_ && GetParent(frame_) == host_ && (GetWindowLongW(hwnd_, GWL_STYLE) & WS_CHILD) != 0);
    data->SetBool("host_inside_root", host.left >= origin.x);
    data->SetBool("host_transparent", (GetWindowLongW(host_, GWL_EXSTYLE) & WS_EX_NOREDIRECTIONBITMAP) != 0);
    s->Record("peek_mid_slide", data);
  }
}
void SidebarPeek::Present() {
  if (!host_) return;
  if (hwnd_) ShowWindow(hwnd_, SW_SHOWNA);
  parked_ = false;
}
void SidebarPeek::Park() {
  parked_ = true;
  if (const auto s = state_.lock(); s && s->embedded) {
    s->embedded->RestorePopupOwners(host_);
    s->embedded->HideSidebarShadow(); s->embedded->SetSidebarOverlayVisible(false);
  }
  if (host_) SetWindowPos(host_, nullptr, kParked, kParked, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}
void SidebarPeek::Slide(bool in, bool activate) {
  if (!hwnd_) return;
  (void)activate;  // Focus moves to the sidebar only when the person clicks it.
  if (in) { Present(); Place(0.0); }
  motion_done_ = false;
  Animate(++motion_, Now(), in);
}
double SidebarPeek::Now() {
  LARGE_INTEGER counter, frequency; QueryPerformanceCounter(&counter); QueryPerformanceFrequency(&frequency);
  return counter.QuadPart * 1000.0 / frequency.QuadPart;
}
struct SidebarPeek::Pacer { std::atomic<bool> stop{false}, pending{false}; };
void SidebarPeek::StartPacer(unsigned generation, double started, bool in) {
  if (pacer_) pacer_->stop = true;
  auto pacer = std::make_shared<Pacer>();
  pacer_ = pacer; pacer_generation_ = generation;
  std::thread([pacer, weak = weak_from_this(), generation, started, in] {
    while (!pacer->stop) {
      if (FAILED(DwmFlush())) Sleep(4);  // Composition off: still bounded.
      // At most one placement in flight, so a busy UI thread never queues up.
      if (pacer->stop || pacer->pending.exchange(true)) continue;
      const bool posted = CefPostTask(TID_UI, base::BindOnce([](std::weak_ptr<SidebarPeek> weak, std::shared_ptr<Pacer> pacer, unsigned generation, double started, bool in) {
        pacer->pending = false;
        if (auto self = weak.lock()) self->Animate(generation, started, in); else pacer->stop = true;
      }, weak, pacer, generation, started, in));
      if (!posted) pacer->stop = true;  // UI thread gone (shutdown).
    }
  }).detach();
}
// Each frame places the window where it should be at this moment (ease-out
// cubic over kSlideMs), so an occasional late frame never shows as a jump back.
void SidebarPeek::Animate(unsigned generation, double started, bool in) {
  if (generation != motion_ || !hwnd_) { if (pacer_ && pacer_generation_ == generation) pacer_->stop = true; return; }
  const double t = std::min(1.0, (Now() - started) / kSlideMs);
  const double eased = 1 - std::pow(1 - t, 3);
  // Stop the nearly-zero tail at the last physical pixel, parking the browser
  // and clearing its root-composited shadow together.
  if (!in) {
    const auto s = state_.lock();
    if (s && s->window && std::lround(Target().width * s->window->GetDisplay()->GetDeviceScaleFactor() * (1 - eased)) <= 1) {
      if (pacer_) pacer_->stop = true;
      shown_ = 0; motion_done_ = true; Park();
      return;
    }
  }
  Place(in ? eased : 1 - eased);
  if (auto s = state_.lock(); s && s->sidebar_peek_probe) {
    // Frame pacing evidence: when each position was applied.
    auto data = CefDictionaryValue::Create(); data->SetDouble("ms", Now() - started); data->SetBool("in", in);
    s->Record("peek_frame", data);
  }
  if (t < 1) {
    if (!pacer_ || pacer_->stop || pacer_generation_ != generation) StartPacer(generation, started, in);
    return;
  }
  if (pacer_) pacer_->stop = true;
  motion_done_ = true;
  if (!in) Park();
}
void SidebarPeek::Shutdown() {
  EndResize(true);
  if (const auto s = state_.lock(); s && s->embedded) {
    s->embedded->HideSidebarShadow(); s->embedded->SetSidebarOverlayVisible(false);
  }
  shutdown_ = true; enabled_ = false; open_ = false; pending_show_ = false; frame_request_ = 0;
  ++watch_; ++motion_; ++prepare_generation_; observer_ = nullptr;
  if (pacer_) pacer_->stop = true;
  if (browser_) browser_->GetHost()->CloseBrowser(true);
}
void SidebarPeek::Publish(CefRefPtr<CefDictionaryValue> theme) {
  auto s = state_.lock();
  if (observer_ && s && s->workspace) observer_->Success(Json(theme ? theme : s->workspace->Snapshot()));
}
void SidebarPeek::Created(CefRefPtr<CefBrowser> browser) {
  browser_ = browser; creating_ = false;
  hwnd_ = browser->GetHost()->GetWindowHandle();
  frame_observer_ = browser->GetHost()->AddDevToolsMessageObserver(new PeekFrameObserver(weak_from_this()));
  ShowWindow(hwnd_, SW_SHOWNA);  // Full-sized, still parked outside the desktop.
  if (auto s = state_.lock()) {
    s->peek_id = browser->GetIdentifier(); RegisterSessionUi(s, browser->GetIdentifier()); s->browsers[browser->GetIdentifier()] = browser;
    if (s->window && !shutdown_) {
      const auto target = Target();
      const float scale = s->window->GetDisplay()->GetDeviceScaleFactor();
      SizeFrame(static_cast<int>(std::lround(target.width * scale)), static_cast<int>(std::lround(target.height * scale)), scale);
    }
  }
  if (shutdown_) { browser->GetHost()->CloseBrowser(true); return; }
  if (pending_show_) { pending_show_ = false; Show(); }
}
void SidebarPeek::Closed(int id) {
  observer_ = nullptr; frame_observer_ = nullptr; browser_ = nullptr; hwnd_ = nullptr; open_ = false;
  pending_show_ = false; frame_request_ = 0; ++prepare_generation_;
  if (const auto s = state_.lock(); s && s->embedded) {
    s->embedded->RestorePopupOwners(host_); s->embedded->HideSidebarShadow();
  }
  frame_ = nullptr; corner_scale_ = 0; shadow_recorded_ = false;
  if (host_) {
    const HWND closed_host = host_; host_ = nullptr;
    // Chrome UI children finish native destruction after OnBeforeClose.
    CefPostTask(TID_UI, base::BindOnce([](HWND window) { if (IsWindow(window)) DestroyWindow(window); }, closed_host));
  }
  inline_host_ = false;
  if (auto s = state_.lock()) {
    UnregisterSessionUi(s, id); s->browsers.erase(id); s->peek_id = -1;
    // The root's close waits for every helper browser (see WindowClose).
    if (s->window_close) s->window_close->OnClosed(id);
    if (!s->window && s->browsers.empty()) QuitIfLastWindow(s);
  }
}
void SidebarPeek::Cancel(int64_t query) { if (query == observer_id_) { observer_ = nullptr; observer_id_ = -1; } }

bool SidebarPeek::Query(CefRefPtr<CefBrowser> b, CefRefPtr<CefFrame> f, int64_t query, const CefString& raw,
                        bool persistent, CefRefPtr<CefMessageRouterBrowserSide::Callback> cb) {
  auto s = state_.lock();
  if (!s || !IsPeekUi(b->GetIdentifier(), s->peek_id.load(), f->IsMain(), f->GetURL().ToString())) { cb->Failure(403, "Untrusted sidebar document"); return true; }
  if (raw.length() > 8192) { cb->Failure(413, "Request too large"); return true; }
  auto value = CefParseJSON(raw, JSON_PARSER_RFC);
  auto d = value && value->GetType() == VTYPE_DICTIONARY ? value->GetDictionary() : nullptr;
  if (!d || d->GetType("method") != VTYPE_STRING || !s->workspace) { cb->Failure(400, "Invalid command"); return true; }
  const auto method = d->GetString("method").ToString();
  if (HandleUiFocus(s, b, method, d->GetSize(), persistent, cb)) {
    auto details = CefDictionaryValue::Create(); details->SetBool("request_focus", method == "ui.focus");
    details->SetBool("foreground", BrowserIsForeground(b)); details->SetBool("owns_keyboard", BrowserOwnsKeyboard(s, b));
    details->SetBool("editing", s->ui_editing); details->SetBool("inline", inline_host_);
    s->Record("sidebar_peek_focus", details); return true;
  }
  if (persistent != (method == "browser.subscribe")) { cb->Failure(400, "Only the subscription persists"); return true; }
  if (method == "browser.subscribe" && d->GetSize() == 1) {
    if (observer_) { cb->Failure(409, "Already subscribed"); return true; }
    observer_ = cb; observer_id_ = query; Publish(); return true;
  }
  if (method == "browser.getSnapshot" && d->GetSize() == 1) { cb->Success(Json(s->workspace->Snapshot())); return true; }
  if (s->workspace->WindowClosing()) { cb->Failure(409, "Window is closing"); return true; }
  if (HandleSidebarAction(s,b,method,d,persistent,cb)) return true;
  if (method == "workspace.sidebarResizeHovered") {
    if (d->GetSize() != 2 || d->GetType("width") != VTYPE_INT || (d->GetInt("width") != 2 && d->GetInt("width") != 8)) {
      cb->Failure(400, "Invalid sidebar hover request"); return true;
    }
    bool hovered = false;
    if (open_ && motion_done_ && host_ && !parked_) {
      POINT cursor{}; RECT bounds{};
      const HWND expected_root = inline_host_ && s->window ? s->window->GetWindowHandle() : host_;
      if (GetCursorPos(&cursor) && hwnd_ && GetWindowRect(hwnd_, &bounds) && GetAncestor(WindowFromPoint(cursor), GA_ROOT) == expected_root) {
        const float scale = s->window->GetDisplay()->GetDeviceScaleFactor();
        hovered = PtInRect(&bounds, cursor) && cursor.x >= bounds.right - std::lround(d->GetInt("width") * scale);
      }
    }
    cb->Success(hovered ? "true" : "false"); return true;
  }
  if (method == "workspace.beginSidebarResize" && d->GetSize() == 1) {
    if (!open_ || !motion_done_ || !s->window) { cb->Failure(409, "Sidebar is not open"); return true; }
    POINT cursor{};
    if (!GetCursorPos(&cursor) || !PrimaryButtonDown()) {
      cb->Success(Json(s->workspace->Snapshot())); return true;  // Fast click already ended.
    }
    if (GetAncestor(GetForegroundWindow(), GA_ROOTOWNER) != s->window->GetWindowHandle()) {
      cb->Failure(409, "Resize requires the foreground browser"); return true;
    }
    if (!resizing_) { resize_start_x_ = cursor.x; resize_start_width_ = s->sidebar_width; }
    resizing_ = true; away_ = 0;
    auto details = CefDictionaryValue::Create(); details->SetInt("width", s->sidebar_width);
    s->Record("sidebar_peek_resize_began", details);
    Publish(); cb->Success(Json(s->workspace->Snapshot())); return true;
  }
  if (method == "workspace.setSidebarWidth") {
    if (d->GetSize() != 3 || d->GetType("width") != VTYPE_INT || d->GetInt("width") < kSidebarMinWidth ||
        d->GetInt("width") > kSidebarMaxWidth || d->GetType("save") != VTYPE_BOOL) {
      cb->Failure(400, "Invalid sidebar width command"); return true;
    }
    if (!open_ || !motion_done_ || !s->window) { cb->Failure(409, "Sidebar is not open"); return true; }
    // Keep the sidebar open meanwhile; only beginSidebarResize arms the
    // native pointer loop, which needs the start it records.
    away_ = 0;
    const int width = SidebarWidth(d->GetInt("width"), s->window->GetClientAreaBoundsInScreen().width);
    s->workspace->SetSidebarWidth(width, d->GetBool("save"),
      [cb](int code, const std::string& error, CefRefPtr<CefDictionaryValue> snapshot) {
        if (code) cb->Failure(code, error); else cb->Success(Json(snapshot));
      });
    PlaceHost();
    if (s->input_probe) {
      auto data = CefDictionaryValue::Create(); data->SetInt("width", width); data->SetBool("save", d->GetBool("save"));
      s->Record("sidebar_peek_resized", data);
    }
    return true;
  }
  if (method == "sidebar.ready" && d->GetSize() == 4 && d->GetType("revision") == VTYPE_INT &&
      d->GetType("width") == VTYPE_INT && d->GetType("height") == VTYPE_INT) {
    const auto target = Target();
    if (d->GetInt("revision") >= prepare_revision_ && std::abs(d->GetInt("width") - target.width) <= 1 &&
        std::abs(d->GetInt("height") - target.height) <= 1) PrepareFrame();
    cb->Success("{}"); return true;
  }
  if (IsTreeMethod(method)) {
    s->workspace->Handle(d, [cb](int code, const std::string& error, CefRefPtr<CefDictionaryValue> snapshot) {
      if (code) cb->Failure(code, error); else cb->Success(Json(snapshot));
    });
    return true;
  }
  // Coordinates from the peek document become root client coordinates.
  const auto client = s->window ? s->window->GetClientAreaBoundsInScreen() : CefRect();
  // The sidebar sits at its resting place in the host (Target) when used.
  const auto bounds = Target();
  const int dx = bounds.x, dy = bounds.y;
  if ((method == "workspace.contextMenu" || method=="workspace.spaceMenu") && s->sidebar_menu && d->GetSize() == 4 && d->GetType("id") == VTYPE_STRING &&
      (d->GetString("id").length() == 36 || d->GetString("id").empty()) && d->GetType("x") == VTYPE_INT && d->GetType("y") == VTYPE_INT) {
    const int x = std::clamp(d->GetInt("x") + dx, 0, std::max(0, SidebarWidth(s->sidebar_width, client.width) - 1));
    s->sidebar_menu->Open(d->GetString("id"), x, d->GetInt("y") + dy, query, cb, b,method=="workspace.spaceMenu"); return true;
  }
  if (method == "panel.open" && s->panel_popup && d->GetSize() == 6 && d->GetType("kind") == VTYPE_STRING && (d->GetString("kind") == "archive" || d->GetString("kind") == "space" || d->GetString("kind") == "newSpace") &&
      d->GetType("x") == VTYPE_INT && d->GetType("y") == VTYPE_INT && d->GetType("width") == VTYPE_INT && d->GetType("height") == VTYPE_INT) {
    const CefRect anchor(std::max(0, d->GetInt("x") + dx), std::max(0, d->GetInt("y") + dy),
                         std::clamp(d->GetInt("width"), 1, 400), std::clamp(d->GetInt("height"), 1, 400));
    s->panel_popup->Toggle(d->GetString("kind").ToString(), anchor); cb->Success(Json(s->workspace->Snapshot())); return true;
  }
  if (method == "workspace.openDroppedFiles" && d->GetSize() == 1) {
    auto files = std::move(drop_files_); drop_files_.clear();
    if (files.empty()) { cb->Failure(409, "Only files from this computer can be dropped here"); return true; }
    bool first = true;
    for (const auto& file : files) { s->workspace->OpenFile(file, first); first = false; }
    cb->Success(Json(s->workspace->Snapshot())); return true;
  }
  cb->Failure(404, "Not a sidebar command"); return true;
}
}  // namespace red
