#pragma once
#include "app/state.h"
#include "include/internal/cef_types_wrappers.h"
namespace red {
// The logo button (top-left) as the browser menu, like Arc's. Popovers opened
// from it or from Ctrl+H / Ctrl+J anchor under the logo (root client DIP).
inline const CefRect kMainMenuAnchor(10, 4, 28, 28);
// |anchor| is the logo button in root client DIP coordinates.
void ShowMainMenu(const std::shared_ptr<State>& state, const CefRect& anchor);
// Right click on the top bar's copy-link button: "Copy Link" and "Super Copy" (the address without
// tracking parameters, Ctrl+Shift+C). |anchor| is that button in root client DIP coordinates.
void ShowCopyMenu(const std::shared_ptr<State>& state, const CefRect& anchor);
// Pins or unpins an installed extension on every window's top bar.
void PinExtension(const std::shared_ptr<State>& state, const std::string& id, bool pinned);
// An extension's menu (Open, Pin/Unpin, Manage Extensions) with its top-left at |at|; |popup| is
// where the extension's popup would open (top-right corner), both in screen DIP.
// From a popover: |return_to| gets focus back, |chosen| runs before a chosen row's action and
// |dismissed| when the menu closes without a choice.
void ShowExtensionMenu(const std::shared_ptr<State>& state, const std::string& id, const CefPoint& at, const CefPoint& popup,
                       CefRefPtr<CefWindow> return_to = nullptr, std::function<void()> chosen = nullptr,
                       std::function<void()> dismissed = nullptr);
}  // namespace red
