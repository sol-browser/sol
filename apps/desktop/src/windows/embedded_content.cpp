#include "windows/embedded_content.h"
#include "windows/auto_pip.h"
#include "windows/devtools_dock.h"
#include "windows/focus.h"
#include "trust_policy.h"
#include <windows.h>
#include <dwmapi.h>
#include <cmath>
#include "include/cef_client.h"
#include "include/cef_parser.h"
#include "include/views/cef_display.h"
#include "include/base/cef_callback.h"
#include "include/wrapper/cef_closure_task.h"
#include "windows/command_bar.h"
#include "windows/panel_popup.h"
#include "windows/extension_popup.h"
#include "windows/sidebar_peek.h"
#include "windows/window_corners.h"
#include "workspace/controller.h"
#include "app/browsing_profiles.h"

namespace red {
namespace {
// Accessed only on the CEF UI thread. Observe our own process; never inject a
// hook DLL, subclass CEF internals, or inspect windows belonging to other apps.
std::map<HWINEVENTHOOK, std::weak_ptr<State>> stacking_observers;
void CALLBACK OnStackingEvent(HWINEVENTHOOK hook, DWORD event, HWND window,
                             LONG object, LONG child, DWORD, DWORD) {
  if ((event != EVENT_OBJECT_CREATE && event != EVENT_OBJECT_SHOW && event != EVENT_OBJECT_DESTROY &&
       event != EVENT_OBJECT_REORDER && event != EVENT_OBJECT_LOCATIONCHANGE) ||
      (object != OBJID_WINDOW && object != OBJID_CLIENT) || child != CHILDID_SELF) return;
  const auto found = stacking_observers.find(hook);
  if (found == stacking_observers.end()) return;
  auto state = found->second.lock();
  if (!state || !state->window || !state->embedded) return;
  const auto root = state->window->GetWindowHandle();
  if (event == EVENT_OBJECT_DESTROY && object == OBJID_WINDOW) { state->suppressed_windows.erase(window); return; }
  if (event == EVENT_OBJECT_SHOW && object == OBJID_WINDOW && window != root && GetAncestor(window, GA_ROOT) == window) {
    // Chrome shows a window it created for an extension after we hid it
    // (measured: after our OnAfterCreated and even after OnBeforeClose).
    if (state->suppressed_windows.contains(window)) {
      ShowWindow(window, SW_HIDE);
      RECT rect{}; GetWindowRect(window, &rect);
      auto details = CefDictionaryValue::Create(); details->SetInt("x", rect.left); details->SetInt("y", rect.top);
      state->Record("foreign_window_reshow_hidden", details);
    }
    else if (state->probe) {
      auto details = CefDictionaryValue::Create();
      details->SetString("handle", std::to_string(reinterpret_cast<uintptr_t>(window)));
      wchar_t name[64]{}; GetClassNameW(window, name, 64); details->SetString("class", name);
      details->SetBool("owned", GetWindow(window, GW_OWNER) != nullptr);
      RECT shown{}, ours{}; GetWindowRect(window, &shown); GetWindowRect(root, &ours);
      // Position relative to our window, to see where Chrome anchors bubbles.
      details->SetInt("x", shown.left - ours.left); details->SetInt("y", shown.top - ours.top);
      details->SetInt("width", shown.right - shown.left); details->SetInt("height", shown.bottom - shown.top);
      wchar_t title[128]{}; GetWindowTextW(window, title, 128); details->SetString("title", title);
      state->Record("toplevel_shown", details);
    }
  }
  // A root position change cannot change its children's z-order. Show and
  // reorder events still repair Chrome's late native restacking as before.
  if (event == EVENT_OBJECT_LOCATIONCHANGE && window == root) return;
  // Menus and bubbles are top-level popups owned by the root (GetParent
  // returns the owner): they never change the children's order, and
  // restacking the Link Peek while a page's context menu opens closed it.
  if (window != root && GetAncestor(window, GA_ROOT) == window) return;
  if (window == root || GetParent(window) == root) state->embedded->QueueRaise();
}
}
EmbeddedContent::EmbeddedContent(std::weak_ptr<State> state,
    std::function<CefRefPtr<CefClient>(const std::string&, bool)> client_factory)
    : state_(std::move(state)), client_factory_(std::move(client_factory)) {
  stacking_hook_ = SetWinEventHook(EVENT_OBJECT_CREATE, EVENT_OBJECT_LOCATIONCHANGE,
      nullptr, &OnStackingEvent, GetCurrentProcessId(), GetCurrentThreadId(), WINEVENT_OUTOFCONTEXT);
  if (stacking_hook_) stacking_observers.emplace(stacking_hook_, state_);
  else if (auto current = state_.lock()) current->Record("native_stacking_observer_failed");
}
EmbeddedContent::~EmbeddedContent() {
  if (stacking_hook_) {
    UnhookWinEvent(stacking_hook_);
    stacking_observers.erase(stacking_hook_);
  }
}
void EmbeddedContent::QueueRaise() {
  if (raise_pending_) return;
  raise_pending_ = true;
  CefPostTask(TID_UI, base::BindOnce([](std::weak_ptr<State> weak) {
    if (auto state = weak.lock(); state && state->embedded) {
      state->embedded->raise_pending_ = false;
      state->embedded->RaiseActive();
    }
  }, state_));
}
CefRect EmbeddedContent::WorkspacePhysicalBounds() const {
  auto state = state_.lock();
  const float scale = state && state->window ? state->window->GetDisplay()->GetDeviceScaleFactor() : 1.0F;
  return CefRect(static_cast<int>(std::lround(bounds_.x * scale)),
      static_cast<int>(std::lround(bounds_.y * scale)),
      static_cast<int>(std::lround(bounds_.width * scale)),
      static_cast<int>(std::lround(bounds_.height * scale)));
}
CefRect EmbeddedContent::PagePhysicalBounds(int id) const {
  if (fullscreen_id_ != -1 && id == fullscreen_id_) return FullClientBounds();
  const auto area = LayoutArea(id);
  const auto state = state_.lock();
  return state && state->devtools_dock ? state->devtools_dock->PageBounds(id, area) : area;
}
CefRect EmbeddedContent::PhysicalBounds() const {
  const auto state = state_.lock();
  return PagePhysicalBounds(state && state->content ? state->content->GetIdentifier() : -1);
}
void EmbeddedContent::Relayout() {
  if (!cold_cover_) FinishPresentation(presentation_generation_, false);
  Resize(bounds_);
}
bool EmbeddedContent::BeginPopupAnimation(HWND popup, bool bottom, float origin_x, bool closing, float from_scale, float origin_y) {
  const auto state = state_.lock();
  if (!state || !state->window) return false;
  const HWND root = state->window->GetWindowHandle();
  const bool floating = state->sidebar_peek && state->sidebar_peek->window_visible() &&
      !(GetWindowLongPtrW(state->sidebar_peek->host(), GWL_STYLE) & WS_CHILD);
  // Root composition is above child HWNDs (F11), but below an owned top-level
  // sidebar. Keep the proxy above that sidebar for the entire entry/handoff/
  // reverse sequence, even if the sidebar subsequently closes.
  if (floating || popup_animation_host_.Active(popup)) {
    corners_.EndPopupAnimation(popup);
    HWND owner = GetWindow(popup, GW_OWNER);
    if (!owner || GetAncestor(owner, GA_ROOTOWNER) != root) owner = root;
    const bool success = popup_animation_host_.Begin(owner, popup, bottom, origin_x, closing, from_scale, origin_y);
    auto details = CefDictionaryValue::Create(); details->SetBool("success", success); details->SetBool("closing", closing);
    state->Record("popup_animation_above_sidebar", details);
    return success;
  }
  popup_animation_host_.End();
  return corners_.BeginPopupAnimation(root, popup, bottom, origin_x, closing, from_scale, origin_y);
}
bool EmbeddedContent::FadePopupShadow(HWND popup, int duration_ms) {
  return popup_animation_host_.Active(popup) ? popup_animation_host_.FadeShadow(popup, duration_ms) : corners_.FadePopupShadow(popup, duration_ms);
}
void EmbeddedContent::EndPopupAnimation(HWND popup) {
  popup_animation_host_.End(popup); corners_.EndPopupAnimation(popup);
}
void EmbeddedContent::ConfigurePopupOwner(HWND popup, bool allow_sidebar) {
  const auto state = state_.lock();
  if (!state || !state->window || !IsWindow(popup) || (GetWindowLongPtrW(popup, GWL_STYLE) & WS_CHILD)) return;
  const HWND root = state->window->GetWindowHandle();
  const bool floating = allow_sidebar && state->sidebar_peek && state->sidebar_peek->window_visible() &&
      !(GetWindowLongPtrW(state->sidebar_peek->host(), GWL_STYLE) & WS_CHILD);
  const HWND owner = floating ? state->sidebar_peek->host() : root;
  if (GetWindow(popup, GW_OWNER) == owner) return;
  // This changes only ownership of our top-level UI popup, never a Chrome
  // child/browser parent. Windows keeps owned windows above their owner even
  // when an opener click activates that owner before the renderer's mouse-up.
  SetWindowLongPtrW(popup, GWLP_HWNDPARENT, reinterpret_cast<LONG_PTR>(owner));
  auto details = CefDictionaryValue::Create(); details->SetBool("floating", floating);
  details->SetBool("success", GetWindow(popup, GW_OWNER) == owner);
  state->Record("popup_native_owner", details);
}
void EmbeddedContent::RestorePopupOwners(HWND sidebar) {
  const auto state = state_.lock();
  if (!sidebar || !state || !state->window) return;
  struct Transfer { HWND from, to; } transfer{sidebar, state->window->GetWindowHandle()};
  // Before parking, changing style or destroying the sidebar wrapper, restore
  // its owned UI popups to the main window so Windows cannot destroy their CEF
  // hosts as a side effect. These are same-thread top-level windows only.
  EnumThreadWindows(GetCurrentThreadId(), [](HWND window, LPARAM data) -> BOOL {
    const auto& owner = *reinterpret_cast<const Transfer*>(data);
    if (GetWindow(window, GW_OWNER) == owner.from)
      SetWindowLongPtrW(window, GWLP_HWNDPARENT, reinterpret_cast<LONG_PTR>(owner.to));
    return TRUE;
  }, reinterpret_cast<LPARAM>(&transfer));
}
bool EmbeddedContent::ShowSidebarShadow(const RECT& panel, float scale, double progress) {
  const auto state = state_.lock();
  return state && state->window && corners_.ShowSidebarShadow(state->window->GetWindowHandle(), panel, scale, progress);
}
bool EmbeddedContent::Create(const std::string& url, const std::string& node_id) {
  auto state = state_.lock();
  if (!state || !state->window) return false;
  if (state->tab_trace.is_open()) state->Record("page_load_create_requested");
  CefWindowInfo info;
  auto initial_bounds = WorkspacePhysicalBounds();
  if (state->workspace_mode) {
    // Chrome ChildWindowDelegate calls Show() after OnAfterCreated. Hiding in
    // that callback alone cannot prevent its initial blank surface flashing.
    // Keep the real viewport size but park the child outside its parent's clip
    // until creation unwinds and the workspace activates it behind the old page.
    initial_bounds.x = -initial_bounds.width - 32;
    initial_bounds.y = -initial_bounds.height - 32;
  }
  info.SetAsChild(state->window->GetWindowHandle(), initial_bounds);
  if (state->workspace_mode) info.ex_style |= WS_EX_NOACTIVATE;
  CefBrowserSettings settings;
  if (state->workspace_mode) {
    link_status_available_ = corners_.PrepareLinkStatus(state->window->GetWindowHandle());
    if (link_status_available_) settings.chrome_status_bubble = STATE_DISABLED;
    if (state->probe) {
      auto data = CefDictionaryValue::Create(); data->SetBool("custom", link_status_available_);
      state->Record("link_status_configured", data);
    }
  }
  const auto background = Themed({229, 223, 240}, state->theme, state->theme_dark);  // Behind the first paint.
  settings.background_color = CefColorSetARGB(255, background.r, background.g, background.b);
  if (IsInternalPageUrl(url)) {
    // Our own internal pages (history, downloads): in the trusted UI profile (no
    // extensions, never the web profile), marked trusted for its renderer. Chrome
    // style like every tab, so the engine rounds its corners the same way; its
    // client keeps it locked (navigation, menus, DevTools, Chrome commands).
    if (!state->ui_context) return false;
    info.runtime_style = CEF_RUNTIME_STYLE_CHROME;
    auto extra = CefDictionaryValue::Create(); extra->SetBool("trusted_ui", true);
    return CefBrowserHost::CreateBrowser(info, client_factory_(node_id, true), url, settings, extra, state->ui_context);
  }
  // This is the document's base color, not just a pre-paint placeholder.
  // Sites without an explicit background should match Chrome's opaque white;
  // theme/backdrop colors belong to the shell and the Views container.
  settings.background_color = CefColorSetARGB(255, 255, 255, 255);
  info.runtime_style = CEF_RUNTIME_STYLE_CHROME;
  const auto profile = state->workspace ? state->workspace->ProfileForNode(node_id) : nullptr;
  return CefBrowserHost::CreateBrowser(info, client_factory_(node_id, false), url,
      settings, nullptr, profile ? profile->context : state->WebContext());
}
void EmbeddedContent::Resize(const CefRect& bounds) {
  // Never shrink pages to a minimized window's empty client area.
  if (const auto root = state_.lock(); bounds.width <= 0 || bounds.height <= 0 ||
      (root && root->window && IsIconic(root->window->GetWindowHandle()))) return;
  if (!cold_cover_ &&
      (bounds.x != bounds_.x || bounds.y != bounds_.y || bounds.width != bounds_.width || bounds.height != bounds_.height))
    FinishPresentation(presentation_generation_);
  bounds_ = bounds;
  if (HasLinkPeek()) { LayoutLinkPeek(); return; }
  if (split_enabled_) { LayoutSplit(); return; }
  const auto state = state_.lock();
  if (state && state->devtools_dock) state->devtools_dock->Layout(WorkspacePhysicalBounds(),
      state->content ? state->content->GetIdentifier() : -1, IsFullscreen());
  for (const auto& [id, browser] : browsers_) {
    const HWND handle = browser->GetHost()->GetWindowHandle();
    auto target = PagePhysicalBounds(id);
    if (parked_ && id == presenting_id_) { target.x = -target.width - 32; target.y = -target.height - 32; }
    // Only visible pages are resized now: resizing every hidden page made each
    // layout change cost ~5 ms per open tab (80 tabs: 400-700 ms on the UI
    // thread, a visible stall). Activate() applies the current target bounds;
    // resizing never introduces a page-readiness or screenshot dependency.
    if (!IsWindowVisible(handle) && id != fullscreen_id_) continue;
    if (split_leaving_.contains(id)) continue;  // Stays a pane until the next page covers it.
    RECT actual{};
    POINT origin{};
    const bool known = GetWindowRect(handle, &actual) != FALSE;
    origin = {actual.left, actual.top};
    const bool local = known && ScreenToClient(GetParent(handle), &origin);
    if (!local || origin.x != target.x || origin.y != target.y ||
        actual.right - actual.left != target.width || actual.bottom - actual.top != target.height)
      SetWindowPos(handle, nullptr, target.x, target.y, target.width, target.height, SWP_NOACTIVATE | SWP_NOZORDER);
    Shape(handle);
  }
  UpdateCorners();
  if (cold_cover_) ShowColdCover();
  const auto area = NotificationArea();
  corners_.PositionNotification(RECT{area.x, area.y, area.x + area.width, area.y + area.height});
}
void EmbeddedContent::NotifyWindowMoved() {
  for (const auto& [id, browser] : browsers_)
    if (browser->IsValid() && IsWindowVisible(browser->GetHost()->GetWindowHandle()))
      browser->GetHost()->NotifyMoveOrResizeStarted();
}
CefRect EmbeddedContent::FullClientBounds() const {
  const auto state = state_.lock();
  RECT client{};
  if (!state || !state->window || !GetClientRect(state->window->GetWindowHandle(), &client)) return WorkspacePhysicalBounds();
  return CefRect(0, 0, client.right - client.left, client.bottom - client.top);
}
void EmbeddedContent::SetFullscreen(CefRefPtr<CefBrowser> browser, bool fullscreen) {
  auto state = state_.lock();
  if (!state || !state->window || !browsers_.contains(browser->GetIdentifier())) return;
  HideLinkStatus();
  FinishPresentation(presentation_generation_, false);
  const int id = browser->GetIdentifier();
  const bool was_fullscreen = IsFullscreen();
  if (fullscreen) {
    // Only the visible page may take over the window.
    if (!state->content || state->content->GetIdentifier() != id) { browser->GetHost()->ExitFullscreen(false); return; }
    fullscreen_id_ = id;
  } else {
    if (fullscreen_id_ != id) return;
    fullscreen_id_ = -1;
  }
  ApplyFullscreenWindow(was_fullscreen);
  // Window bounds callbacks also call Resize; apply now for the current size.
  Resize(bounds_);
  QueueRaise();
  if (fullscreen) {
    Notify("fullscreen", L"Press", {L"Esc"}, L"to exit full screen");
  } else HideNotification();
  auto data = CefDictionaryValue::Create();
  data->SetInt("browser_id", id); data->SetBool("fullscreen", fullscreen);
  state->Record("page_fullscreen", data);
}
void EmbeddedContent::ApplyFullscreenWindow(bool was_fullscreen) {
  HideLinkStatus();
  auto state = state_.lock(); if (!state || !state->window) return;
  const bool fullscreen = IsFullscreen();
  if (fullscreen) {
    if (!was_fullscreen) window_was_fullscreen_ = state->window->IsFullscreen();
    if (state->command_bar) state->command_bar->Hide(false);
    if (state->panel_popup) state->panel_popup->Hide(false);
    if (state->extension_popup) state->extension_popup->Close();
    if (!state->window->IsFullscreen()) state->window->SetFullscreen(true);
  } else if (!window_was_fullscreen_ && state->window->IsFullscreen()) state->window->SetFullscreen(false);
  SetWindowFrameForFullscreen(fullscreen);
  // F11 covers the docked sidebar with the page. Temporarily enable its
  // floating version without changing the saved docked/hidden preference.
  if (state->sidebar_peek) {
    state->sidebar_peek->SetEnabled(!state->sidebar_visible || browser_fullscreen_);
    if (fullscreen_id_ != -1) state->sidebar_peek->Hide();
  }
}
void EmbeddedContent::ToggleBrowserFullscreen() {
  auto state = state_.lock(); if (!state || !state->window) return;
  FinishPresentation(presentation_generation_, false);
  const bool was_fullscreen = IsFullscreen();
  browser_fullscreen_ = !was_fullscreen;
  if (was_fullscreen && fullscreen_id_ != -1) ExitFullscreenFor(fullscreen_id_);
  ApplyFullscreenWindow(was_fullscreen);
  Resize(bounds_); QueueRaise();
  if (browser_fullscreen_) Notify("fullscreen", L"Press", {L"Esc"}, L"to exit full screen");  // Esc also leaves F11 full screen.
  else HideNotification();
  FocusBrowser(state->content);
  if (state->workspace) state->workspace->Refresh();
  auto data = CefDictionaryValue::Create(); data->SetBool("fullscreen", browser_fullscreen_);
  const HWND root = state->window->GetWindowHandle(); RECT rect{}; GetWindowRect(root, &rect);
  MONITORINFO monitor{sizeof(monitor)}; GetMonitorInfoW(MonitorFromWindow(root, MONITOR_DEFAULTTONEAREST), &monitor);
  data->SetBool("covers_monitor", EqualRect(&rect, &monitor.rcMonitor) != FALSE);
  data->SetInt("width", rect.right - rect.left); data->SetInt("height", rect.bottom - rect.top);
  state->Record("browser_fullscreen", data);
}
bool EmbeddedContent::ExitBrowserFullscreen() {
  if (!browser_fullscreen_ || fullscreen_id_ != -1) return false;
  ToggleBrowserFullscreen(); return true;
}
CefRect EmbeddedContent::NotificationArea() const {
  if (IsFullscreen()) return FullClientBounds();
  return notification_page_ != -1 && browsers_.contains(notification_page_) ? PagePhysicalBounds(notification_page_) : PhysicalBounds();
}
void EmbeddedContent::Notify(const std::string& kind, const std::wstring& text, const std::vector<std::wstring>& keys,
                             const std::wstring& tail, int duration_ms, int page) {
  const auto state = state_.lock();
  if (!state || !state->window) return;
  notification_page_ = page;
  const auto area = NotificationArea();
  const auto generation = ++notification_generation_;
  const auto focus = ::GetFocus();
  corners_.SetNotificationTheme(state->theme, state->theme_dark);
  const auto scale = state->window->GetDisplay()->GetDeviceScaleFactor();
  bool animated = false;
  const bool painted = corners_.ShowNotification(state->window->GetWindowHandle(),
      RECT{area.x, area.y, area.x + area.width, area.y + area.height}, scale, L"", text, keys, tail, true, &animated);
  auto data = CefDictionaryValue::Create(); data->SetString("kind", kind);
  data->SetBool("painted", painted); data->SetBool("focus_preserved", focus == ::GetFocus());
  data->SetBool("geometry_ok", painted && NotificationGeometryOk());
  data->SetBool("animated", animated);
  if (painted && state->probe) {
    const auto card = corners_.notification_bounds();
    data->SetDouble("top_inset_dip", (card.top - area.y) / scale);
    data->SetDouble("right_inset_dip", (area.x + area.width - card.right) / scale);
  }
  state->Record("notification_shown", data);
  CefPostDelayedTask(TID_UI, base::BindOnce([](std::weak_ptr<State> weak, unsigned long long generation) {
    if (auto s = weak.lock(); s && s->embedded && s->embedded->notification_generation_ == generation) {
      const int exit_ms = s->embedded->corners_.DismissNotification();
      CefPostDelayedTask(TID_UI, base::BindOnce([](std::weak_ptr<State> weak, unsigned long long generation) {
        if (auto current = weak.lock(); current && current->embedded && current->embedded->notification_generation_ == generation)
          current->embedded->HideNotification();
      }, weak, generation), exit_ms);
    }
  }, state_, generation), duration_ms);
}
bool EmbeddedContent::NotificationGeometryOk() const {
  const auto state = state_.lock();
  if (!state || !state->window || !NotificationVisible()) return false;
  const auto area = NotificationArea();
  const auto r = corners_.notification_bounds();
  const auto gap = 32 * state->window->GetDisplay()->GetDeviceScaleFactor();
  return r.left >= area.x && r.top >= area.y && r.right <= area.x + area.width && r.bottom <= area.y + area.height &&
      r.top - area.y <= gap && area.x + area.width - r.right <= gap;
}
void EmbeddedContent::HideNotification() {
  ++notification_generation_; corners_.HideNotification();
  if (auto state = state_.lock()) state->Record("notification_hidden");
}
void EmbeddedContent::SetWindowFrameForFullscreen(bool fullscreen) const {
  auto state = state_.lock();
  if (!state || !state->window) return;
  const HWND root = state->window->GetWindowHandle();
  const DWM_WINDOW_CORNER_PREFERENCE corners = DWMWCP_DONOTROUND;
  const COLORREF border = fullscreen ? DWMWA_COLOR_NONE : DWMWA_COLOR_DEFAULT;
  const HRESULT a = DwmSetWindowAttribute(root, DWMWA_WINDOW_CORNER_PREFERENCE, &corners, sizeof(corners));
  const HRESULT b = DwmSetWindowAttribute(root, DWMWA_BORDER_COLOR, &border, sizeof(border));
  ApplyWindowCorners(state->window);
  if (state->probe) {
    auto data = CefDictionaryValue::Create(); data->SetBool("fullscreen", fullscreen);
    data->SetBool("corners_ok", SUCCEEDED(a)); data->SetBool("border_ok", SUCCEEDED(b));
    state->Record("window_frame_fullscreen", data);
  }
}
void EmbeddedContent::ExitFullscreenFor(int id) {
  if (fullscreen_id_ != id) return;
  auto state = state_.lock();
  HideNotification();
  fullscreen_id_ = -1;
  ApplyFullscreenWindow(true);
  const auto found = browsers_.find(id);
  if (found != browsers_.end()) found->second->GetHost()->ExitFullscreen(true);
}
bool EmbeddedContent::FullscreenGeometryOk() const {
  auto state = state_.lock();
  if (!state || !state->window || !state->content) return false;
  RECT actual{};
  const auto handle = state->content->GetHost()->GetWindowHandle();
  if (!GetWindowRect(handle, &actual)) return false;
  POINT origin{0, 0}; ClientToScreen(state->window->GetWindowHandle(), &origin);
  const auto full = FullClientBounds();
  RECT region{};
  return actual.left - origin.x == full.x && actual.top - origin.y == full.y &&
      actual.right - actual.left == full.width && actual.bottom - actual.top == full.height &&
      GetWindowRgnBox(handle, &region) == ERROR;
}
void EmbeddedContent::SetSidebarOverlayVisible(bool visible) {
  if (sidebar_overlay_visible_ == visible && !cold_cover_) return;
  if (visible && !cold_cover_) FinishPresentation(presentation_generation_, false);
  if (visible) HideLinkStatus();
  sidebar_overlay_visible_ = visible;
  if (cold_cover_) ShowColdCover();
}
void EmbeddedContent::Shape(HWND handle) const {
  if (HasLinkPeek()) { ShapeLinkPeek(handle); return; }
  // New engines clip the actual compositor subtree. Older engines use
  // PageCorners, with a window region when composition is unavailable.
  // Fullscreen pages remain square.
  const auto state = state_.lock();
  const bool fullscreen = IsFullscreen() && state && state->content && state->content->GetHost()->GetWindowHandle() == handle;
#if defined(RED_CEF_CONTAINER_CORNERS)
  if (state && state->workspace_mode) {
    for (const auto& [id, browser] : browsers_) if (browser->GetHost()->GetWindowHandle() == handle) {
      // Clip the actual document subtree. Opaque corner mattes cannot match
      // an Acrylic/Mica background. Split cells are clipped too; their frame adds only the border.
      // Split panes: 7, inside the frame's 8 DIP corner and 1 DIP border.
      browser->GetHost()->SetContentContainerCornerRadius(fullscreen ? 0 : IsSplitPage(id) ? 7 : 6);
      break;
    }
  }
#endif
  RECT box{};
  if (corners_composited_ || fullscreen || !state || !state->workspace_mode || !state->window) {
    if (GetWindowRgnBox(handle, &box) != ERROR) SetWindowRgn(handle, nullptr, TRUE);
    return;
  }
  RECT physical{}; GetClientRect(handle, &physical);
  const int diameter = static_cast<int>(std::lround(12 * state->window->GetDisplay()->GetDeviceScaleFactor()));
  const HRGN region = CreateRoundRectRgn(0, 0, physical.right + 1, physical.bottom + 1, diameter, diameter);
  const HRGN existing = CreateRectRgn(0,0,0,0);
  const bool unchanged = region && existing && GetWindowRgn(handle,existing) != ERROR && EqualRgn(region,existing);
  if (existing) DeleteObject(existing);
  if (unchanged) { DeleteObject(region); return; }
  // On success Windows owns the region; otherwise release our allocation.
  if (region && !SetWindowRgn(handle, region, TRUE)) DeleteObject(region);
}
void EmbeddedContent::RefreshTheme() {
  if (HasLinkPeek()) { Relayout(); return; }
#if defined(RED_CEF_CONTAINER_CORNERS)
  // The compositor clip does not depend on color. Split frames and link status do.
  if (split_enabled_) UpdateSplitCorners(); else UpdateLinkStatus();
#else
  UpdateCorners();
#endif
}
void EmbeddedContent::UpdateCorners() {
  UpdateLinkStatus();
  if (HasLinkPeek() && LinkPeekComposed()) { corners_.Hide(); return; }
  auto state = state_.lock();
  if (split_enabled_) { UpdateSplitCorners(); return; }
  if (!state || !state->window || !state->workspace_mode || !state->content || IsFullscreen() ||
      crash_presented_ == state->content->GetIdentifier() || !browsers_.contains(state->content->GetIdentifier())) {
    corners_.Hide(); return;
  }
  const HWND page = state->content->GetHost()->GetWindowHandle();
  if (!IsWindowVisible(page)) { corners_.Hide(); return; }
  // 6 DIP, matching .web-slot's border-radius in style.css.
  const int radius = static_cast<int>(std::lround(6 * state->window->GetDisplay()->GetDeviceScaleFactor()));
#if defined(RED_CEF_CONTAINER_CORNERS)
  // Navigation can reattach the native document layer below NativeViewHost.
  // Refresh its wrapper when restoring the native stack as well as on resize.
  Shape(page);
  corners_.Hide();
  const bool painted = true;
#else
  const auto physical = WorkspacePhysicalBounds();
  const RECT rect{physical.x, physical.y, physical.x + physical.width, physical.y + physical.height};
  const bool painted = corners_composited_ && corners_.Show(state->window->GetWindowHandle(), rect, radius);
#endif
  if (!painted && corners_composited_) {
    corners_composited_ = false;  // Composition unavailable: region fallback.
    for (const auto& [id, browser] : browsers_) Shape(browser->GetHost()->GetWindowHandle());
  }
  if (state->probe && !corners_recorded_) {
    corners_recorded_ = true;
    auto data = CefDictionaryValue::Create(); data->SetBool("painted", painted); data->SetInt("radius", radius);
#if defined(RED_CEF_CONTAINER_CORNERS)
    data->SetString("method", "container-clip");
#else
    data->SetString("method", painted ? "composition" : "region");
#endif
    state->Record("page_corners", data);
  }
}
void EmbeddedContent::OnCreated(CefRefPtr<CefBrowser> browser) {
  browsers_[browser->GetIdentifier()] = browser;
  ObservePresentation(browser);
  Shape(browser->GetHost()->GetWindowHandle());
  if (auto state = state_.lock(); state && state->workspace_mode)
    ShowWindow(browser->GetHost()->GetWindowHandle(), SW_HIDE);
  else Activate(browser);
}
void EmbeddedContent::Activate(CefRefPtr<CefBrowser> browser, bool cold) {
  auto state = state_.lock();
  if (!state || !browsers_.contains(browser->GetIdentifier())) return;
  if (state->content != browser) HideLinkStatus();
  if (HasLinkPeek()) { LayoutLinkPeek(); return; }
  if (state->content != browser) OnPageSwitched(state, state->content, browser);
  const auto handle = browser->GetHost()->GetWindowHandle();
  const int id = browser->GetIdentifier();
  if (IsSplitPage(id)) {
    const bool changed = state->content != browser;
    state->content = browser; if (!state->ui_editing) state->keyboard_target = id;
    LayoutSplit();
#if CEF_API_ADDED(CEF_EXPERIMENTAL)
    if (changed) browser->GetHost()->NotifyTabActivated();
#endif
    if (changed && !state->ui_editing) FocusBrowser(browser);
    return;
  }
  if (!state->ui_editing) state->keyboard_target = id;
  // From a split to another tab: the panes stay as they are while the new page
  // paints parked outside the window (shown, so it renders, but off screen); once
  // it has a frame it moves into place and the panes are hidden in the same step
  // (FinishPresentation). An unpainted child window cannot be used as a veil:
  // Windows clips the siblings beneath it, so the parent would show through.
  if (!split_leaving_.empty() && !split_leaving_.contains(id)) {
    FinishPresentation(presentation_generation_, false, false, true);
    presentations_[id].activated_ms = GetTickCount64();
    crash_presented_ = -1;
    if (state->devtools_dock) state->devtools_dock->Layout(WorkspacePhysicalBounds(), id, IsFullscreen());
    state->content = browser;
#if CEF_API_ADDED(CEF_EXPERIMENTAL)
    browser->GetHost()->NotifyTabActivated();
#endif
    const auto physical = PagePhysicalBounds(id);
    const HWND sidebar = InlineSidebar();
    SetWindowPos(handle, sidebar ? sidebar : HWND_TOP, -physical.width - 32, -physical.height - 32, physical.width, physical.height,
        SWP_NOACTIVATE | SWP_SHOWWINDOW);
    Shape(handle);
    parked_ = true;
    for (const auto& [other_id, item] : browsers_)
      if (other_id != id && !split_leaving_.contains(other_id)) ShowWindow(item->GetHost()->GetWindowHandle(), SW_HIDE);
    UpdateCorners();
    for (const int pane : split_leaving_) {
      if (const auto found = browsers_.find(pane); found != browsers_.end())
        BlockPresentationInput(found->second->GetHost()->GetWindowHandle());
    }
    BeginPresentation(browser, cold);
    auto data = CefDictionaryValue::Create(); data->SetInt("browser_id", id); data->SetBool("left_split", true);
    state->Record("embedded_activated", data);
    return;
  }
  const auto retained = browsers_.find(presentation_backdrop_id_);
  const int previous_id = retained != browsers_.end() &&
      IsWindowVisible(retained->second->GetHost()->GetWindowHandle()) ? presentation_backdrop_id_ :
      state->content ? state->content->GetIdentifier() : -1;
  const bool switched = !split_leaving_.contains(id) && (previous_id != id || !IsWindowVisible(handle));
  const bool keep_gradient = cold_cover_ && (switched || cold);
  FinishPresentation(presentation_generation_, false, false, keep_gradient);
  split_leaving_.clear();
  presentations_[id].activated_ms = GetTickCount64();
  crash_presented_ = -1;
  if (fullscreen_id_ != -1 && fullscreen_id_ != id) { ExitFullscreenFor(fullscreen_id_); Resize(bounds_); }
  if (state->devtools_dock) state->devtools_dock->Layout(WorkspacePhysicalBounds(), id, IsFullscreen());
  const auto physical = PagePhysicalBounds(id);
  const auto previous = browsers_.find(previous_id);
  const HWND backdrop = previous != browsers_.end() && previous_id != id ? previous->second->GetHost()->GetWindowHandle() : nullptr;
  const bool retain_page = backdrop && IsWindowVisible(backdrop);
  // The incoming child can render underneath the outgoing child and the
  // passive gradient. Warm switches only use the outgoing child.
  const bool covered = keep_gradient || (cold && switched) || (!sidebar_overlay_visible_ && switched && retain_page);
  // A cold page paints parked outside the window while the page area shows the
  // empty-space look (ShowColdCover); a warm one paints beneath the outgoing page.
  const bool park = covered && (keep_gradient || cold);
  presentation_backdrop_id_ = covered && retain_page && !park ? previous_id : -1;
  state->content = browser;
#if CEF_API_ADDED(CEF_EXPERIMENTAL)
  browser->GetHost()->NotifyTabActivated();
#endif
  const HWND sidebar = InlineSidebar();
  SetWindowPos(handle, presentation_backdrop_id_ != -1 ? backdrop : sidebar ? sidebar : HWND_TOP,
      park ? -physical.width - 32 : physical.x, park ? -physical.height - 32 : physical.y, physical.width, physical.height,
      SWP_NOACTIVATE | SWP_SHOWWINDOW);
  parked_ = park;
  Shape(handle);
  for (const auto& [other_id, item] : browsers_)
    if (other_id != id && other_id != presentation_backdrop_id_) ShowWindow(item->GetHost()->GetWindowHandle(), SW_HIDE);
  if (presentation_backdrop_id_ != -1) {
    BlockPresentationInput(backdrop);
    SetWindowPos(backdrop, sidebar ? sidebar : HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
  }
  UpdateCorners();
  if (covered) {
    BeginPresentation(browser, cold);
  } else {
    if (state->ui_editing) FocusShell(state); else FocusBrowser(browser);
    auto direct = CefDictionaryValue::Create(); direct->SetInt("browser_id", id);
    direct->SetBool("no_outgoing_page", !retain_page); direct->SetBool("target_loading", browser->IsLoading());
    state->Record("page_transition_direct", direct);
  }
  auto data = CefDictionaryValue::Create(); data->SetInt("browser_id", id);
  data->SetBool("loading", browser->IsLoading()); data->SetBool("retained", covered); data->SetBool("contained", AllContained());
  state->Record("embedded_activated", data);
}
void EmbeddedContent::OnClosed(int id) {
  if (id == link_status_browser_) HideLinkStatus();
  if (IsLinkPeek(id)) {
    link_peek_return_delay_=corners_.BeginLinkPeekReturn();
    if (const auto state=state_.lock()) {
      auto data=CefDictionaryValue::Create(); data->SetBool("animated",link_peek_return_delay_>0); state->Record("link_peek_return",data);
    }
    if (link_peek_return_delay_ && link_peek_live_) AnimateLinkPeekSources(1,0,LinkPeekScene::kReturnMs);
    else if (link_peek_return_delay_) for (const auto& [other,page]:browsers_) if (other!=id) SetWindowRgn(page->GetHost()->GetWindowHandle(),nullptr,TRUE);
    if (!link_peek_return_delay_) ClearLinkPeek();
  }
  if (const auto closing = browsers_.find(id); closing != browsers_.end())
    presentation_disabled_.erase(closing->second->GetHost()->GetWindowHandle());
  split_leaving_.erase(id);
  if (presenting_id_ == id) FinishPresentation(presentation_generation_, false, false);
  else if (presentation_backdrop_id_ == id) {
    if (cold_cover_) presentation_backdrop_id_ = -1;
    else FinishPresentation(presentation_generation_, false);
  }
  presentations_.erase(id);
  if (fullscreen_id_ == id) {
    fullscreen_id_ = -1;
    ApplyFullscreenWindow(true);
  }
  browsers_.erase(id);
  auto state = state_.lock();
  if (state && state->content && state->content->GetIdentifier() == id) {
    state->content = nullptr;
    if (!state->workspace_mode && !browsers_.empty()) Activate(browsers_.begin()->second);
  }
  UpdateCorners();
}
void EmbeddedContent::HideAll() {
  HideLinkStatus();
  if (HasLinkPeek()) ClearLinkPeek();
  FinishPresentation(presentation_generation_, false);
  HideNotification();
  crash_presented_ = -1;
  if (fullscreen_id_ != -1) ExitFullscreenFor(fullscreen_id_);
  for (const auto& [id, browser] : browsers_) ShowWindow(browser->GetHost()->GetWindowHandle(), SW_HIDE);
  if (auto state = state_.lock()) state->content = nullptr;
  if (auto state = state_.lock(); state && state->devtools_dock) state->devtools_dock->Layout(WorkspacePhysicalBounds(), -1, IsFullscreen());
  corners_.Hide();
}
void EmbeddedContent::PresentCrash(CefRefPtr<CefBrowser> browser) {
  if (browser && browser->GetIdentifier() == link_status_browser_) HideLinkStatus();
  auto state = state_.lock();
  if (!state || !browsers_.contains(browser->GetIdentifier())) return;
  if (IsLinkPeek(browser->GetIdentifier())) { crash_presented_=browser->GetIdentifier(); LayoutLinkPeek(); FocusShell(state); return; }
  if (IsSplitPage(browser->GetIdentifier())) { state->content = browser; crash_presented_ = browser->GetIdentifier(); LayoutSplit(); return; }
  FinishPresentation(presentation_generation_, false);
  HideNotification();
  for (const auto& [id, item] : browsers_) ShowWindow(item->GetHost()->GetWindowHandle(), SW_HIDE);
  state->content = browser; crash_presented_ = browser->GetIdentifier();
  corners_.Hide();
  FocusShell(state);
}
void EmbeddedContent::RaiseActive() {
  auto state = state_.lock();
  if (state && state->window && HasLinkPeek()) { LayoutLinkPeek(); return; }
  if (!state || !state->window || !state->content || !browsers_.contains(state->content->GetIdentifier())) return;
  if (split_enabled_) { LayoutSplit(); return; }
  if (state->devtools_dock) state->devtools_dock->Layout(WorkspacePhysicalBounds(), state->content->GetIdentifier(), IsFullscreen());
  const HWND sidebar = InlineSidebar();
  const HWND above = state->devtools_dock ? state->devtools_dock->StackAfter(sidebar) : sidebar;
  // Chrome can show a newly-created child again after OnAfterCreated hid it.
  // Reconcile our managed children on native show/reorder events as well.
  for (const auto& [id, browser] : browsers_) {
    const auto other = browser->GetHost()->GetWindowHandle();
    if (id != state->content->GetIdentifier() && id != presentation_backdrop_id_ && !split_leaving_.contains(id) &&
        IsWindowVisible(other)) ShowWindow(other, SW_HIDE);
  }
  const auto retained = browsers_.find(presentation_backdrop_id_);
  if (retained != browsers_.end()) {
    const auto backdrop = retained->second->GetHost()->GetWindowHandle();
    if (IsWindowVisible(backdrop) && GetWindow(backdrop, GW_HWNDPREV) != above)
      SetWindowPos(backdrop, above ? above : HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    return;
  }
  const auto handle = state->content->GetHost()->GetWindowHandle();
  if (crash_presented_ == state->content->GetIdentifier()) {
    if (IsWindowVisible(handle)) ShowWindow(handle, SW_HIDE);
    return;
  }
  // The Alloy renderer creates/restacks a full-client input HWND after browser
  // creation. Keep the visible Chrome child above it for Win32 hit testing,
  // without changing keyboard focus or any browser's native parent.
  if (!IsWindowVisible(handle) || GetParent(handle) != state->window->GetWindowHandle()) return;
  if (IsFullscreen() && !FullscreenGeometryOk()) Resize(bounds_);
  if (GetWindow(handle, GW_HWNDPREV) != above)
    SetWindowPos(handle, above ? above : HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
  if (!IsFullscreen() && !corners_.visible()) UpdateCorners();
}
HWND EmbeddedContent::InlineSidebar() const {
  const auto state = state_.lock();
  if (!state || !state->window || !state->sidebar_peek || !state->sidebar_peek->window_visible()) return nullptr;
  // GetParent returns the owner of the floating (top-level) sidebar too. A page
  // inserted after a non-sibling fails the whole SetWindowPos and stays hidden.
  const HWND host = state->sidebar_peek->host();
  return (GetWindowLongPtrW(host, GWL_STYLE) & WS_CHILD) && GetAncestor(host, GA_PARENT) == state->window->GetWindowHandle() ? host : nullptr;
}
bool EmbeddedContent::AllContained() const {
  auto state = state_.lock();
  if (!state || !state->window || browsers_.empty()) return false;
  const HWND parent = state->window->GetWindowHandle();
  for (const auto& [id, browser] : browsers_) {
    const HWND child = browser->GetHost()->GetWindowHandle();
    if (!IsWindow(child) || !IsChild(parent, child) || GetAncestor(child, GA_ROOT) != parent) return false;
  }
  return true;
}
bool EmbeddedContent::HasSingleVisibleActive() const {
  auto state = state_.lock();
  if (!state || !state->content || !browsers_.contains(state->content->GetIdentifier())) return false;
  int visible = 0;
  for (const auto& [id, browser] : browsers_)
    if (IsWindowVisible(browser->GetHost()->GetWindowHandle())) ++visible;
  return visible == 1 && IsWindowVisible(state->content->GetHost()->GetWindowHandle());
}
bool EmbeddedContent::ActiveMatchesBounds() const {
  const auto state = state_.lock();
  if (!state || !state->window || !state->content) return false;
  RECT actual{};
  if (!GetWindowRect(state->content->GetHost()->GetWindowHandle(), &actual)) return false;
  POINT origin{0, 0}; ClientToScreen(state->window->GetWindowHandle(), &origin);
  const auto expected = PhysicalBounds();
  return actual.left - origin.x == expected.x && actual.top - origin.y == expected.y &&
      actual.right - actual.left == expected.width && actual.bottom - actual.top == expected.height;
}
bool EmbeddedContent::VisiblePagesMatchPresentation() const {
  if (!HasLinkPeek()) return HasSingleVisibleActive();
  const auto state=state_.lock();
  if (!state || !state->content || !IsLinkPeek(state->content->GetIdentifier())) return false;
  bool source=false,preview=false;
  for (const auto& [id,browser]:browsers_) {
    if (!IsWindowVisible(browser->GetHost()->GetWindowHandle())) continue;
    if (id==link_peek_id_) preview=true;
    else if (id==link_peek_base_) source=true;
    else if (!IsSplitPage(id)) return false;
  }
  return source && preview;
}
bool EmbeddedContent::ActiveOwnsInputArea() const {
  auto state = state_.lock();
  if (!state || !state->window || !VisiblePagesMatchPresentation()) return false;
  const auto root = state->window->GetWindowHandle();
  const auto child = state->content->GetHost()->GetWindowHandle();
  RECT area{};
  if (!GetWindowRect(child, &area)) return false;
  // Hit-test our own child hierarchy, independent of other foreground apps.
  // Sample across the viewport so a full-window transparent shell cannot pass.
  for (const int x : {1, 2, 3}) for (const int y : {1, 2, 3}) {
    POINT point{area.left + (area.right - area.left) * x / 4,
                area.top + (area.bottom - area.top) * y / 4};
    ScreenToClient(root, &point);
    const auto hit = ChildWindowFromPointEx(root, point, CWP_SKIPINVISIBLE | CWP_SKIPDISABLED);
    if (hit != child && !IsChild(child, hit)) {
      if (state->probe) {
        auto details = CefDictionaryValue::Create();
        details->SetString("hit", std::to_string(reinterpret_cast<uintptr_t>(hit)));
        details->SetString("expected", std::to_string(reinterpret_cast<uintptr_t>(child)));
        wchar_t name[256]{}; GetClassNameW(hit, name, 256); details->SetString("class", name);
        details->SetInt("x", point.x); details->SetInt("y", point.y);
        state->Record("native_input_area_blocked", details);
      }
      return false;
    }
  }
  return true;
}
}  // namespace red
