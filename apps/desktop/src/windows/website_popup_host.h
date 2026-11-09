#pragma once
#include <windows.h>
#include <string>
#include "include/cef_browser.h"

namespace red {
// The plain top-level window that holds a website popup (window.open with popup features, such as
// "Continue with Google"). CEF places a Chrome-style browser inside its parent window, so Sol
// creates this window itself, owned by the Sol window, and creates the popup browser as its child.
// Measured with CEF's default popup: a full Chrome window with tabs on the primary monitor.

// A title bar with only the close button; the edges still resize.
inline constexpr DWORD kWebsitePopupStyle = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_CLIPCHILDREN;

// Creates and shows the window at |bounds| (screen pixels, outer size). Returns nullptr on failure.
HWND CreateWebsitePopupHost(HWND owner, const CefRect& bounds, bool dark);
// The browser created inside |host|: resized with the window, closed by its close button.
void AttachWebsitePopupBrowser(HWND host, CefRefPtr<CefBrowser> browser);
void SetWebsitePopupTitle(HWND host, const std::wstring& title);
// Destroys the window from any thread, once its browser closed or its creation was aborted.
void ReleaseWebsitePopupHost(HWND host);
}  // namespace red
