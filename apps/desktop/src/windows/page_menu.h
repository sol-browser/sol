#pragma once
#include "app/state.h"
#include <functional>
#include "include/cef_context_menu_handler.h"
#include "include/views/cef_window.h"
namespace red {
// A website page's context menu shown in Red's own menu (windows/menu_popup.cpp): Chrome's model
// (copy, paste, open link, save image, spelling, Red's Inspect element...) is copied row by row,
// and the chosen command goes back to Chrome through |callback|, so every item keeps Chrome's
// behaviour. Returns false (Chrome draws its native menu) when the window cannot show it.
// Chrome's "Open link as <profile>" item, or its submenu with several profiles
// (IDC_OPEN_LINK_IN_PROFILE_FIRST..LAST, 100 ids in Chrome's command table).
bool OpensLinkInProfile(int command);
// |return_to| gets activation back when the menu closes (default: the browser window); |closed|
// runs once when the menu goes away, chosen or not.
bool ShowPageMenu(const std::shared_ptr<State>& state, CefRefPtr<CefMenuModel> model, CefRefPtr<CefRunContextMenuCallback> callback,
                  CefRefPtr<CefWindow> return_to = nullptr, std::function<void()> closed = nullptr);
}  // namespace red
