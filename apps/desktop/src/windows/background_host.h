#pragma once
#include <functional>
#include <windows.h>
namespace red {
// Sol in the notification area (docs/little-sol.md). While enabled, Sol keeps running after its
// last window closes and Ctrl+Alt+N opens Little Sol's command bar from any app. The tray icon
// opens Sol on a click; its menu has New Little Sol, Open Sol and Quit Sol. UI thread only.
// sol-tray.exe (src/tray/sol_tray.cpp) runs while Sol is closed; a starting Sol closes it and takes
// the icon and Ctrl+Alt+N back.
void CloseTrayHelper();
class BackgroundHost {
 public:
  struct Actions { std::function<void()> little_sol, open_sol, quit; };
  explicit BackgroundHost(Actions actions);
  ~BackgroundHost();
  BackgroundHost(const BackgroundHost&) = delete;
  BackgroundHost& operator=(const BackgroundHost&) = delete;
  // Adds or removes the tray icon and the hotkey. Another app may own Ctrl+Alt+N: the icon still
  // shows and Sol still stays in the background; hotkey_registered() tells which.
  void SetEnabled(bool enabled);
  bool enabled() const { return enabled_; }
  bool hotkey_registered() const { return hotkey_; }
  bool icon_visible() const { return icon_; }
 private:
  static LRESULT CALLBACK Proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam);
  void TakeHotkey();
  void AddIcon();
  void RemoveIcon();
  void ShowMenu();
  Actions actions_;
  HWND window_ = nullptr;
  HICON icon_handle_ = nullptr;
  UINT taskbar_created_ = 0;
  bool enabled_ = false, hotkey_ = false, icon_ = false;
  int hotkey_attempts_ = 0;
};
}  // namespace red
