#pragma once
#include <windows.h>
#include "app/state.h"
#include "include/views/cef_browser_view.h"
#include "include/wrapper/cef_message_router.h"
namespace red {
inline bool BrowserIsForeground(CefRefPtr<CefBrowser> browser) {
  if (!browser) return false;
  const auto handle = browser->GetHost()->GetWindowHandle();
  const auto foreground = GetForegroundWindow();
  return IsWindow(handle) && foreground && GetAncestor(handle, GA_ROOT) == GetAncestor(foreground, GA_ROOT);
}
inline bool BrowserOwnsKeyboard(const std::shared_ptr<State>& state, CefRefPtr<CefBrowser> browser) {
  if (!browser) return false;
  const auto handle = browser->GetHost()->GetWindowHandle(), focus = ::GetFocus();
  if (!focus || (focus != handle && !IsChild(handle, focus))) return false;
  if (browser->GetIdentifier() == state->shell_id) {
    for (const auto& [id, other] : state->browsers) {
      if (id == state->shell_id) continue;
      const auto child = other->GetHost()->GetWindowHandle();
      if (child != handle && IsChild(handle, child) && (focus == child || IsChild(child, focus))) return false;
    }
  }
  return true;
}
inline void FocusBrowser(CefRefPtr<CefBrowser> browser) {
  if (!browser || !BrowserIsForeground(browser)) return;
  // Each Chrome child has its own native widget and Views focus manager.
  // CEF's virtual view focus alone does not move the Win32 keyboard target.
  const auto handle = browser->GetHost()->GetWindowHandle();
  if (!IsWindowVisible(handle) || !IsWindowEnabled(handle)) return;
  ::SetFocus(handle);
  if (auto view = CefBrowserView::GetForBrowser(browser); view && view->IsValid()) view->RequestFocus();
  browser->GetHost()->SetFocus(true);
}
inline void FocusShell(const std::shared_ptr<State>& state) {
  const int target = state->ui_editing && state->keyboard_target == state->peek_id ? state->peek_id.load() : state->shell_id.load();
  const auto shell = state->browsers.find(target);
  if (shell != state->browsers.end()) FocusBrowser(shell->second);
}
inline bool FocusUiInput(const std::shared_ptr<State>& state, CefRefPtr<CefBrowser> browser) {
  if (!BrowserIsForeground(browser)) return false;
  if (browser->GetIdentifier() == state->shell_id || browser->GetIdentifier() == state->peek_id) {
    state->ui_editing = true; state->keyboard_target = browser->GetIdentifier();
    if (state->content && BrowserOwnsKeyboard(state, state->content)) state->content->GetHost()->SetFocus(false);
  }
  FocusBrowser(browser); return true;
}
// Call only AFTER the document's exact native ID/main-frame/origin guard.
inline bool HandleUiFocus(const std::shared_ptr<State>& state, CefRefPtr<CefBrowser> browser,
    const std::string& method, size_t fields, bool persistent,
    CefRefPtr<CefMessageRouterBrowserSide::Callback> callback) {
  if (method != "ui.focus" && method != "ui.editingEnded") return false;
  if (persistent || fields != 1) { callback->Failure(400, "Invalid UI focus request"); return true; }
  if (method == "ui.editingEnded") {
    if (state->keyboard_target == browser->GetIdentifier() &&
        (browser->GetIdentifier() == state->shell_id || browser->GetIdentifier() == state->peek_id)) state->ui_editing = false;
    callback->Success("true"); return true;
  }
  callback->Success(FocusUiInput(state, browser) ? "true" : "false"); return true;
}
}  // namespace red
