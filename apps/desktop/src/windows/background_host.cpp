#include "windows/background_host.h"
#include "interface_strings.h"
#include "include/internal/cef_string.h"
#include <shellapi.h>
#include <shlobj.h>
#pragma comment(lib, "shell32.lib")
namespace red {
namespace {
constexpr wchar_t kClass[] = L"SolBackgroundHost";
constexpr UINT kTrayMessage = WM_APP + 41;
constexpr UINT kTrayId = 1;
constexpr int kHotkeyId = 0x534C;
constexpr UINT_PTR kHotkeyTimer = 1;
enum : UINT { kMenuLittleSol = 1, kMenuOpen, kMenuQuit };
HICON TrayIcon() {
  // sol.exe's own icon, at the notification area's size for this display.
  wchar_t path[MAX_PATH]{};
  const auto length = GetModuleFileNameW(nullptr, path, MAX_PATH);
  const int size = GetSystemMetrics(SM_CXSMICON);
  HICON small_icon = nullptr;
  if (length && length < MAX_PATH && SUCCEEDED(SHDefExtractIconW(path, 0, 0, nullptr, &small_icon, MAKELONG(size * 2, size))) && small_icon)
    return small_icon;
  return LoadIconW(nullptr, IDI_APPLICATION);
}
}  // namespace

void CloseTrayHelper() {
  if (const HWND tray = FindWindowW(L"SolTrayHelper", nullptr)) PostMessageW(tray, WM_CLOSE, 0, 0);
}

BackgroundHost::BackgroundHost(Actions actions, std::string locale, KeyBinding binding)
    : actions_(std::move(actions)), locale_(std::move(locale)), binding_(binding) {
  const HINSTANCE instance = GetModuleHandleW(nullptr);
  WNDCLASSEXW type{sizeof(type)};
  type.lpfnWndProc = Proc; type.hInstance = instance; type.lpszClassName = kClass;
  RegisterClassExW(&type);  // Fails harmlessly when the class already exists.
  // A hidden top-level window, not a message-only one: the tray menu needs a window that can be foreground.
  window_ = CreateWindowExW(WS_EX_TOOLWINDOW, kClass, L"Sol", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, instance, this);
  taskbar_created_ = RegisterWindowMessageW(L"TaskbarCreated");
}

BackgroundHost::~BackgroundHost() {
  SetEnabled(false);
  if (reserved_hotkey_) UnregisterHotKey(window_, reserved_hotkey_);
  if (window_) DestroyWindow(window_);
  if (icon_handle_) DestroyIcon(icon_handle_);
}

void BackgroundHost::SetEnabled(bool enabled) {
  if (!window_ || enabled == enabled_) return;
  enabled_ = enabled;
  if (enabled) {
    hotkey_attempts_ = 0;
    TakeHotkey();
    AddIcon();
  } else {
    KillTimer(window_, kHotkeyTimer);
    if (registered_hotkey_) UnregisterHotKey(window_, registered_hotkey_);
    registered_hotkey_ = 0;
    RemoveIcon();
  }
}

bool BackgroundHost::ReserveHotkey(const KeyBinding& binding) {
  if (!window_ || reserved_hotkey_) return false;
  if (registered_hotkey_ && binding_ == binding) return true;
  const int id = registered_hotkey_ == kHotkeyId ? kHotkeyId + 1 : kHotkeyId;
  if (!RegisterLittleSolHotkey(window_, id, binding)) return false;
  reserved_hotkey_ = id;
  return true;
}

void BackgroundHost::FinishHotkeyChange(const KeyBinding& binding, bool enabled, bool saved) {
  if (!saved || !enabled) {
    if (reserved_hotkey_) UnregisterHotKey(window_, reserved_hotkey_);
  } else if (reserved_hotkey_) {
    if (registered_hotkey_) UnregisterHotKey(window_, registered_hotkey_);
    registered_hotkey_ = reserved_hotkey_;
    KillTimer(window_, kHotkeyTimer);
  }
  reserved_hotkey_ = 0;
  if (!saved) return;
  binding_ = binding;
  if (icon_) AddIcon();
}

void BackgroundHost::TakeHotkey() {
  if (registered_hotkey_ || reserved_hotkey_ || !enabled_) return;
  if (RegisterLittleSolHotkey(window_, kHotkeyId, binding_)) {
    registered_hotkey_ = kHotkeyId;
    KillTimer(window_, kHotkeyTimer);
    if (icon_) AddIcon();  // The tooltip names the shortcut now.
    return;
  }
  // sol-tray.exe may hold Ctrl+Alt+N until this Sol's first window shows: try again for 10 s.
  if (++hotkey_attempts_ < 40) SetTimer(window_, kHotkeyTimer, 250, nullptr);
  else KillTimer(window_, kHotkeyTimer);
}

void BackgroundHost::AddIcon() {
  if (!icon_handle_) icon_handle_ = TrayIcon();
  NOTIFYICONDATAW data{sizeof(data)};
  data.hWnd = window_; data.uID = kTrayId; data.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
  data.uCallbackMessage = kTrayMessage; data.hIcon = icon_handle_;
  const auto tooltip = registered_hotkey_ ? CefString(InterfaceTextWithName(locale_, InterfaceMessage::MenuHotkey,
      LittleSolHotkeyLabel(binding_))).ToWString() : L"Sol";
  wcscpy_s(data.szTip, tooltip.c_str());
  if (icon_) { Shell_NotifyIconW(NIM_MODIFY, &data); return; }
  icon_ = Shell_NotifyIconW(NIM_ADD, &data) != FALSE;
  if (icon_) { data.uVersion = NOTIFYICON_VERSION_4; Shell_NotifyIconW(NIM_SETVERSION, &data); }
}

void BackgroundHost::RemoveIcon() {
  if (!icon_) return;
  NOTIFYICONDATAW data{sizeof(data)};
  data.hWnd = window_; data.uID = kTrayId;
  Shell_NotifyIconW(NIM_DELETE, &data);
  icon_ = false;
}

void BackgroundHost::ShowMenu() {
  HMENU menu = CreatePopupMenu();
  if (!menu) return;
  const auto label = [&](InterfaceMessage id) { return CefString(InterfaceText(locale_, id)).ToWString(); };
  auto little = label(InterfaceMessage::MenuNewLittle);
  if (registered_hotkey_) little += L"\t" + CefString(LittleSolHotkeyLabel(binding_)).ToWString();
  AppendMenuW(menu, MF_STRING, kMenuLittleSol, little.c_str());
  AppendMenuW(menu, MF_STRING, kMenuOpen, label(InterfaceMessage::MenuOpenSol).c_str());
  AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(menu, MF_STRING, kMenuQuit, label(InterfaceMessage::MenuQuitSol).c_str());
  SetMenuDefaultItem(menu, kMenuOpen, FALSE);
  POINT point{};
  GetCursorPos(&point);
  // Without this the menu does not close when you click elsewhere (KB135788).
  SetForegroundWindow(window_);
  const UINT chosen = TrackPopupMenuEx(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY, point.x, point.y, window_, nullptr);
  PostMessageW(window_, WM_NULL, 0, 0);
  DestroyMenu(menu);
  const auto& action = chosen == kMenuLittleSol ? actions_.little_sol : chosen == kMenuOpen ? actions_.open_sol :
      chosen == kMenuQuit ? actions_.quit : std::function<void()>{};
  if (action) action();
}

LRESULT CALLBACK BackgroundHost::Proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
  if (message == WM_NCCREATE) {
    SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams));
    return DefWindowProcW(window, message, wparam, lparam);
  }
  auto* self = reinterpret_cast<BackgroundHost*>(GetWindowLongPtrW(window, GWLP_USERDATA));
  if (!self) return DefWindowProcW(window, message, wparam, lparam);
  if (message == WM_TIMER && wparam == kHotkeyTimer) {
    self->TakeHotkey();
    return 0;
  }
  if (message == WM_HOTKEY) {
    if (self->enabled_ && self->registered_hotkey_ && wparam == static_cast<WPARAM>(self->registered_hotkey_) &&
        self->actions_.little_sol) self->actions_.little_sol();
    return 0;
  }
  if (message == kTrayMessage) {
    // NOTIFYICON_VERSION_4: the event is in the low word of lparam.
    switch (LOWORD(lparam)) {
      // Version 4 also sends WM_LBUTTONUP/WM_RBUTTONUP for the same clicks; one handler each.
      case NIN_SELECT: case NIN_KEYSELECT: if (self->actions_.open_sol) self->actions_.open_sol(); break;
      case WM_CONTEXTMENU: self->ShowMenu(); break;
      default: break;
    }
    return 0;
  }
  if (message == self->taskbar_created_ && self->enabled_) {
    // Explorer restarted: the icon must be added again.
    self->icon_ = false; self->AddIcon();
    return 0;
  }
  return DefWindowProcW(window, message, wparam, lparam);
}
}  // namespace red
