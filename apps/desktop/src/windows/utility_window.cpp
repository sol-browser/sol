#include "windows/utility_window.h"
#include "workspace/controller.h"
#include "include/views/cef_display.h"
#include "include/cef_id_mappers.h"
#include <algorithm>
#include <commctrl.h>

namespace red {
namespace {
CefRect BoundsFor(CefRefPtr<CefWindow> owner, const CefSize& size) {
  const auto bounds = owner ? owner->GetBoundsInScreen() : CefRect();
  const auto display = owner ? CefDisplay::GetDisplayMatchingBounds(bounds, false) : CefDisplay::GetPrimaryDisplay();
  return UtilityWindowBounds(bounds, display ? display->GetWorkArea() : CefRect(0, 0, size.width, size.height), size);
}
LRESULT CALLBACK UtilityMessage(HWND window, UINT message, WPARAM wparam, LPARAM lparam, UINT_PTR id, DWORD_PTR) {
  // Also enforce the policy for shell/keyboard system commands, not just the
  // caption buttons. Owner minimization still hides its owned windows normally.
  if (message == WM_SYSCOMMAND && ((wparam & 0xfff0) == SC_MINIMIZE || (wparam & 0xfff0) == SC_MAXIMIZE)) return 0;
  if (message == WM_NCDESTROY) RemoveWindowSubclass(window, UtilityMessage, id);
  return DefSubclassProc(window, message, wparam, lparam);
}
}
CefRect UtilityWindowBounds(const CefRect& owner, const CefRect& work, const CefSize& preferred) {
  const int width = std::min(preferred.width, work.width), height = std::min(preferred.height, work.height);
  const auto anchor = owner.IsEmpty() ? work : owner;
  return CefRect(std::clamp(anchor.x + (anchor.width - width) / 2, work.x, work.x + work.width - width),
                 std::clamp(anchor.y + (anchor.height - height) / 2, work.y, work.y + work.height - height), width, height);
}
CefRefPtr<CefWindow> UtilityWindowDelegate::parent() const {
  const auto state = state_.lock(); return state ? state->window : nullptr;
}
CefRefPtr<CefWindow> UtilityWindowDelegate::GetParentWindow(CefRefPtr<CefWindow>, bool* menu, bool* activate) {
  *menu = false; *activate = true; return parent();
}
CefRect UtilityWindowDelegate::GetInitialBounds(CefRefPtr<CefWindow>) { return BoundsFor(parent(), size_); }
void UtilityWindowDelegate::OnWindowCreated(CefRefPtr<CefWindow> window) {
  SetWindowSubclass(window->GetWindowHandle(), UtilityMessage, 1, 0);
}
void UtilityWindowDelegate::OnWindowClosing(CefRefPtr<CefWindow> window) {
  const auto state = state_.lock(); const auto owner = parent();
  if (!state || !owner || !state->workspace || state->workspace->WindowClosing() || owner->IsMinimized() || !owner->IsVisible()) return;
  // Hand off while the closing widget still owns activation. Do not activate
  // Sol when an inactive utility is closed after the person switched apps.
  auto foreground = GetForegroundWindow();
  while (foreground && foreground != window->GetWindowHandle()) foreground = GetWindow(foreground, GW_OWNER);
  if (foreground) owner->Activate();
}
void PresentUtilityWindow(CefRefPtr<CefWindow> window, CefRefPtr<CefWindow> owner) {
  if (!window) return;
  if (window->IsMinimized() || window->IsMaximized()) window->Restore();
  if (owner) {
    const auto owner_display = CefDisplay::GetDisplayMatchingBounds(owner->GetBoundsInScreen(), false);
    const auto display = CefDisplay::GetDisplayMatchingBounds(window->GetBoundsInScreen(), false);
    if (owner_display && display && owner_display->GetID() != display->GetID()) window->SetBounds(BoundsFor(owner, window->GetSize()));
  }
  window->Show(); window->Activate();
}
bool UtilityWindowClient::OnChromeCommand(CefRefPtr<CefBrowser>, int command, cef_window_open_disposition_t) {
  return command == cef_id_for_command_id_name("IDC_FULLSCREEN");
}
bool UtilityWindowClient::IsChromeAppMenuItemVisible(CefRefPtr<CefBrowser>, int command) {
  return command != cef_id_for_command_id_name("IDC_FULLSCREEN");
}
bool UtilityWindowClient::OnPreKeyEvent(CefRefPtr<CefBrowser>, const CefKeyEvent& event, CefEventHandle, bool*) {
  return event.windows_key_code == VK_F11;
}
}  // namespace red
