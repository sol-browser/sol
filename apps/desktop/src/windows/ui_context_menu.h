#pragma once
#include <functional>
#include <memory>
#include "app/state.h"
#include "include/cef_context_menu_handler.h"
#include "windows/page_menu.h"
namespace red {
// Red's own UI documents (top bar, sidebar, popovers, menus, Boost and Settings windows) have no
// browser page menu (Back, Reload, Print...): a right click on a button shows nothing. Text fields
// keep Chrome's editing rows (undo, cut, copy, paste, select all), drawn as Red's menu.
// A popup that hides when it loses activation (command bar, popovers) passes a MenuHold: the menu
// takes activation, so the popup holds itself open meanwhile and gets activation back afterwards.
struct MenuHold {
  std::function<CefRefPtr<CefWindow>()> window;
  std::function<void()> opened, closed;
};
class UiContextMenu final : public CefContextMenuHandler {
 public:
  explicit UiContextMenu(std::weak_ptr<State> state, MenuHold hold = {}) : state_(std::move(state)), hold_(std::move(hold)) {}
  void OnBeforeContextMenu(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, CefRefPtr<CefContextMenuParams> params,
                           CefRefPtr<CefMenuModel> model) override {
    if (!(params->GetTypeFlags() & CM_TYPEFLAG_EDITABLE)) { model->Clear(); return; }
    // Editing rows only: drop navigation, print, source, inspect and anything Chrome adds after them.
    for (size_t i = model->GetCount(); i-- > 0;) {
      const int id = model->GetCommandIdAt(i);
      const bool editing = id == MENU_ID_UNDO || id == MENU_ID_REDO || id == MENU_ID_CUT || id == MENU_ID_COPY ||
          id == MENU_ID_PASTE || id == MENU_ID_PASTE_MATCH_STYLE || id == MENU_ID_DELETE || id == MENU_ID_SELECT_ALL ||
          model->GetTypeAt(i) == MENUITEMTYPE_SEPARATOR;
      if (!editing) model->RemoveAt(i);
    }
  }
  bool RunContextMenu(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, CefRefPtr<CefContextMenuParams>, CefRefPtr<CefMenuModel> model,
                      CefRefPtr<CefRunContextMenuCallback> callback) override {
    const auto state = state_.lock();
    if (!model->GetCount()) { callback->Cancel(); return true; }
    if (!state) return false;
    if (hold_.opened) hold_.opened();
    if (ShowPageMenu(state, model, callback, hold_.window ? hold_.window() : nullptr, hold_.closed)) return true;
    if (hold_.closed) hold_.closed();
    return false;
  }
 private:
  std::weak_ptr<State> state_;
  MenuHold hold_;
  IMPLEMENT_REFCOUNTING(UiContextMenu);
};
}  // namespace red
