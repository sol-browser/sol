#include "windows/popup_animation_host.h"
#include <dwmapi.h>
#include <cmath>

namespace red {
namespace {
LRESULT CALLBACK AnimationHostProc(HWND window, UINT message, WPARAM w, LPARAM l) {
  switch (message) {
    case WM_NCHITTEST: return HTTRANSPARENT;
    case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: { PAINTSTRUCT paint; BeginPaint(window, &paint); EndPaint(window, &paint); return 0; }
    case WM_CLOSE: return 0;
  }
  return DefWindowProcW(window, message, w, l);
}
HWND CreateAnimationHost(HWND owner) {
  static const ATOM atom = [] {
    WNDCLASSEXW wc{sizeof(wc)}; wc.lpfnWndProc = AnimationHostProc; wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"RedPopupAnimationHost";
    return RegisterClassExW(&wc);
  }();
  if (!atom) return nullptr;
  const HWND host = CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT,
      L"RedPopupAnimationHost", L"", WS_POPUP, 0, 0, 1, 1, owner, nullptr, GetModuleHandleW(nullptr), nullptr);
  if (host) {
    const DWM_WINDOW_CORNER_PREFERENCE corners = DWMWCP_DONOTROUND;
    DwmSetWindowAttribute(host, DWMWA_WINDOW_CORNER_PREFERENCE, &corners, sizeof(corners));
    const DWMNCRENDERINGPOLICY rendering = DWMNCRP_DISABLED;
    DwmSetWindowAttribute(host, DWMWA_NCRENDERING_POLICY, &rendering, sizeof(rendering));
    const BOOL disabled = TRUE;
    DwmSetWindowAttribute(host, DWMWA_TRANSITIONS_FORCEDISABLED, &disabled, sizeof(disabled));
  }
  return host;
}
}  // namespace
PopupAnimationHost::~PopupAnimationHost() {
  End(); composition_.reset();
  if (host_ && IsWindow(host_)) DestroyWindow(host_);
}
bool PopupAnimationHost::Begin(HWND owner, HWND popup, bool bottom, float origin_x, bool closing, float from_scale, float origin_y) {
  if (!IsWindow(owner) || !IsWindow(popup)) return false;
  RECT panel{}; if (!GetWindowRect(popup, &panel)) return false;
  if (!host_) host_ = CreateAnimationHost(owner);
  if (!host_) return false;
  if (GetWindow(host_, GW_OWNER) != owner)
    SetWindowLongPtrW(host_, GWLP_HWNDPARENT, reinterpret_cast<LONG_PTR>(owner));
  if (!composition_) composition_ = std::make_unique<PageCorners>();
  // The same margin as the shared popup shadow; keep the growing card and its
  // blur inside this transparent target without clipping to sidebar bounds.
  const int margin = static_cast<int>(std::lround(PageCorners::kPopupShadowMarginDip * GetDpiForWindow(popup) / 96.0));
  if (!SetWindowPos(host_, HWND_TOP, panel.left - margin, panel.top - margin,
      panel.right - panel.left + 2 * margin, panel.bottom - panel.top + 2 * margin,
      SWP_NOACTIVATE | SWP_NOOWNERZORDER)) return false;
  if (!composition_->BeginPopupAnimation(host_, popup, bottom, origin_x, closing, from_scale, origin_y)) { End(); return false; }
  source_ = popup;
  SetWindowPos(host_, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_SHOWWINDOW);
  return true;
}
bool PopupAnimationHost::FadeShadow(HWND popup, int duration_ms) {
  return Active(popup) && composition_ && composition_->FadePopupShadow(popup, duration_ms);
}
void PopupAnimationHost::End(HWND popup) {
  if (popup && source_ != popup) return;
  if (composition_ && source_) composition_->EndPopupAnimation(source_);
  source_ = nullptr;
  if (host_) ShowWindow(host_, SW_HIDE);
}
}  // namespace red
