#include "windows/website_popup_host.h"
#include <dwmapi.h>

namespace red {
namespace {
constexpr wchar_t kClass[] = L"SolWebsitePopup";
constexpr UINT kRelease = WM_APP + 1;

struct Host {
  CefRefPtr<CefBrowser> browser;
  HBRUSH background = nullptr;
};

Host* HostOf(HWND window) { return reinterpret_cast<Host*>(GetWindowLongPtrW(window, GWLP_USERDATA)); }

LRESULT CALLBACK HostProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
  Host* host = HostOf(window);
  switch (message) {
    case WM_NCCREATE:
      SetWindowLongPtrW(window, GWLP_USERDATA,
                        reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams));
      break;
    case WM_ERASEBKGND:
      if (host && host->background) {
        RECT client{};
        GetClientRect(window, &client);
        FillRect(reinterpret_cast<HDC>(wparam), &client, host->background);
        return 1;
      }
      break;
    case WM_SIZE:
      if (const HWND child = GetWindow(window, GW_CHILD))
        SetWindowPos(child, nullptr, 0, 0, LOWORD(lparam), HIWORD(lparam), SWP_NOZORDER | SWP_NOACTIVATE);
      return 0;
    case WM_MOVE:
      // Dismisses the page's open <select> menus and similar widgets, as for a tab.
      if (host && host->browser) host->browser->GetHost()->NotifyMoveOrResizeStarted();
      return 0;
    case WM_SETFOCUS:
      if (host && host->browser) host->browser->GetHost()->SetFocus(true);
      return 0;
    case WM_CLOSE:
      // The page's unload handlers run first; the window goes when the browser has closed.
      // Before the browser exists there is nothing to close yet.
      if (host && host->browser) host->browser->GetHost()->CloseBrowser(false);
      return 0;
    case kRelease:
      DestroyWindow(window);
      return 0;
    case WM_NCDESTROY:
      SetWindowLongPtrW(window, GWLP_USERDATA, 0);
      if (host && host->background) DeleteObject(host->background);
      delete host;
      break;
  }
  return DefWindowProcW(window, message, wparam, lparam);
}
}  // namespace

HWND CreateWebsitePopupHost(HWND owner, const CefRect& bounds, bool dark) {
  static const ATOM atom = [] {
    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc = HostProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kClass;
    return RegisterClassExW(&wc);
  }();
  if (!atom) return nullptr;
  auto* host = new Host;
  host->background = CreateSolidBrush(dark ? RGB(32, 32, 32) : RGB(255, 255, 255));
  // Owned, not a child: it moves freely, stays above Sol and has no taskbar button of its own.
  const HWND window = CreateWindowExW(0, kClass, L"Sol", kWebsitePopupStyle, bounds.x, bounds.y,
                                      bounds.width, bounds.height, owner, nullptr, GetModuleHandleW(nullptr), host);
  if (!window) {
    if (host->background) DeleteObject(host->background);
    delete host;
    return nullptr;
  }
  const BOOL dark_frame = dark ? TRUE : FALSE;
  DwmSetWindowAttribute(window, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark_frame, sizeof(dark_frame));
  ShowWindow(window, SW_SHOWNORMAL);
  return window;
}

void AttachWebsitePopupBrowser(HWND host, CefRefPtr<CefBrowser> browser) {
  if (!IsWindow(host)) return;
  if (Host* data = HostOf(host)) data->browser = browser;
  RECT client{};
  GetClientRect(host, &client);
  if (const HWND child = GetWindow(host, GW_CHILD))
    SetWindowPos(child, nullptr, 0, 0, client.right, client.bottom, SWP_NOZORDER | SWP_NOACTIVATE);
  if (GetForegroundWindow() == host) browser->GetHost()->SetFocus(true);
}

void SetWebsitePopupTitle(HWND host, const std::wstring& title) {
  if (IsWindow(host)) SetWindowTextW(host, title.empty() ? L"Sol" : title.c_str());
}

void ReleaseWebsitePopupHost(HWND host) {
  if (IsWindow(host)) PostMessageW(host, kRelease, 0, 0);
}
}  // namespace red
