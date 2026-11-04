#pragma once
#include <windows.h>
#include <dwmapi.h>
#include <string>
#include "include/views/cef_window.h"
namespace red {
// Keep the root's smooth DWM contour and shadow. Custom GDI regions break that
// presentation; exact six-DIP corners belong to the composed page area only.
inline bool ApplyWindowCorners(CefRefPtr<CefWindow> window) {
  const auto handle = window->GetWindowHandle();
  RECT region{};
  if (GetWindowRgnBox(handle, &region) != ERROR && !SetWindowRgn(handle, nullptr, TRUE)) return false;
  const DWM_WINDOW_CORNER_PREFERENCE preference = window->IsMaximized() || window->IsFullscreen() ? DWMWCP_DONOTROUND : DWMWCP_ROUND;
  return SUCCEEDED(DwmSetWindowAttribute(handle, DWMWA_WINDOW_CORNER_PREFERENCE, &preference, sizeof(preference)));
}
inline bool WindowCornersMatch(CefRefPtr<CefWindow> window) {
  const auto handle = window->GetWindowHandle();
  RECT region{}; DWM_WINDOW_CORNER_PREFERENCE preference{};
  const auto expected = window->IsMaximized() || window->IsFullscreen() ? DWMWCP_DONOTROUND : DWMWCP_ROUND;
  return GetWindowRgnBox(handle, &region) == ERROR &&
      SUCCEEDED(DwmGetWindowAttribute(handle, DWMWA_WINDOW_CORNER_PREFERENCE, &preference, sizeof(preference))) && preference == expected;
}
// Windows 11 22H2 (build 22621) and later draw a system backdrop (DWMWA_SYSTEMBACKDROP_TYPE).
inline bool SystemBackdropSupported() {
  using RtlGetVersionFn = LONG(WINAPI*)(OSVERSIONINFOW*);
  static const bool supported = [] {
    const auto get = reinterpret_cast<RtlGetVersionFn>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion"));
    OSVERSIONINFOW version{sizeof(version)};
    return get && get(&version) == 0 && version.dwMajorVersion >= 10 && version.dwBuildNumber >= 22621;
  }();
  return supported;
}
// "mica" (DWMSBT_MAINWINDOW), "acrylic" (DWMSBT_TRANSIENTWINDOW), else none.
inline void ApplySystemBackdrop(HWND window, const std::string& backdrop) {
  const DWM_SYSTEMBACKDROP_TYPE type = backdrop == "mica" ? DWMSBT_MAINWINDOW : backdrop == "acrylic" ? DWMSBT_TRANSIENTWINDOW : DWMSBT_NONE;
  DwmSetWindowAttribute(window, DWMWA_SYSTEMBACKDROP_TYPE, &type, sizeof(type));
}
}  // namespace red
