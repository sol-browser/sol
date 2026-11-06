#pragma once
#include <memory>
#include "app/state.h"
#include "include/cef_browser.h"

namespace red {
// The page's own back/forward list, as Chrome shows it from its arrows.
// Right click on Back or Forward: every entry of the active page, newest at
// the top, the current one checked; a click goes there (history.go), and
// "Show full history" opens sol://history. |anchor|: the button (client DIP).
void ShowTabHistoryMenu(const std::shared_ptr<State>& state, CefRefPtr<CefBrowser> page, const CefRect& anchor);
// Middle click on Back (-1) or Forward (+1): that entry in a new background tab
// below the page. False when there is no such entry or it is not a website.
bool OpenHistoryEntryInNewTab(const std::shared_ptr<State>& state, CefRefPtr<CefBrowser> page, int offset);
}  // namespace red
