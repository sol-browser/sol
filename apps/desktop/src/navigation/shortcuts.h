#pragma once
#include <memory>
#include <iterator>
#include <optional>
#include <string_view>
namespace red {
struct State;
enum class Shortcut { Suppressed = -1, Address = 0, NewTab, CloseTab, NextTab, PreviousTab, Sidebar, Pin, Back, Forward, Reload, ReloadBypass, Stop, FocusPage, CloseWindow, DevTools, History, Downloads, HistoryPage, ReopenClosed,
  Space1, Space2, Space3, Space4, Space5, Space6, Space7, Space8, Space9, Fullscreen, Settings, NewWindow, PictureInPicture, NewPrivateWindow, Print, CopyUrl, RecentTab, RecentTabBack, Screenshot, FullPageScreenshot, Capture,
  SplitFocusLeft, SplitFocusRight, SplitFocusUp, SplitFocusDown, SplitExpand };
inline constexpr std::string_view kShortcutNames[] = {"address", "newTab", "closeTab", "nextTab", "previousTab", "sidebar", "pin", "back", "forward", "reload", "reloadBypass", "stop", "focusPage", "closeWindow", "devTools", "history", "downloads", "historyPage", "reopenClosed",
  "space1", "space2", "space3", "space4", "space5", "space6", "space7", "space8", "space9", "fullscreen", "settings", "newWindow", "pictureInPicture", "newPrivateWindow", "print", "copyUrl", "recentTab", "recentTabBack", "screenshot", "fullPageScreenshot", "capture",
  "splitFocusLeft", "splitFocusRight", "splitFocusUp", "splitFocusDown", "splitExpand"};
inline std::optional<Shortcut> ShortcutNamed(std::string_view name) {
  for (int i = 0; i < static_cast<int>(std::size(kShortcutNames)); ++i) if (kShortcutNames[i] == name) return static_cast<Shortcut>(i);
  return std::nullopt;
}
inline std::optional<Shortcut> ShortcutKey(int key, bool ctrl, bool shift, bool alt) {
  if (alt && !ctrl && !shift) {
    if (key == 115) return Shortcut::CloseWindow;
    if (key == 37) return Shortcut::Back;
    if (key == 39) return Shortcut::Forward;
    if (key == 'P') return Shortcut::PictureInPicture;
  }
  // Ctrl+Alt+arrows: the neighbouring Split View pane; Ctrl+Alt+Enter: expand/restore it.
  if (alt && ctrl && !shift) {
    if (key >= 37 && key <= 40) return static_cast<Shortcut>(static_cast<int>(Shortcut::SplitFocusLeft) + (key == 37 ? 0 : key == 39 ? 1 : key == 38 ? 2 : 3));
    if (key == 13) return Shortcut::SplitExpand;
  }
  if (alt) return std::nullopt;
  if (ctrl) {
    if (shift && key == 'I') return Shortcut::DevTools;
    if (shift && key == 'H') return Shortcut::HistoryPage;
    if (shift && key == 'T') return Shortcut::ReopenClosed;
    if (key == 9) return shift ? Shortcut::RecentTabBack : Shortcut::RecentTab;
    if (key == 33 || key == 34) return key == 33 ? Shortcut::PreviousTab : Shortcut::NextTab;  // Ctrl+PageUp / PageDown: sidebar order.
    if (shift && key == 'C') return Shortcut::CopyUrl;
    if (shift && key == '5') return Shortcut::Capture;
    if (key == 'R') return shift ? Shortcut::ReloadBypass : Shortcut::Reload;
    if (key == 'N') return shift ? Shortcut::NewPrivateWindow : Shortcut::NewWindow;
    if (shift) return std::nullopt;
    // Ctrl+1…9: the space at that place in the switcher (Arc).
    if (key >= '1' && key <= '9') return static_cast<Shortcut>(static_cast<int>(Shortcut::Space1) + key - '1');
    switch (key) {
      case 'L': return Shortcut::Address; case 'T': return Shortcut::NewTab;
      case 'W': return Shortcut::CloseTab; case 'S': return Shortcut::Sidebar;
      case 'D': return Shortcut::Pin;
      case 'H': return Shortcut::History; case 'J': return Shortcut::Downloads;
      case 'P': return Shortcut::Print;
      case 188: return Shortcut::Settings;  // VK_OEM_COMMA (Ctrl+,).
    }
  } else {
    if (!shift && key == 122) return Shortcut::Fullscreen;
    if (!shift && key == 123) return Shortcut::DevTools;
    if (key == 116) return shift ? Shortcut::ReloadBypass : Shortcut::Reload;
    if (!shift && key == 27) return Shortcut::Stop;
  }
  return std::nullopt;
}
// Chrome's reserved commands (BrowserCommandController::IsReservedCommandOrKey) and Sol's own tab
// switcher: handled before the page sees the key, so a site cannot keep them. Every other shortcut
// goes to the page first and runs only if the page leaves the key alone (Figma's Ctrl+D duplicates,
// Docs' Ctrl+K inserts a link), as in Chrome.
inline bool ShortcutReserved(Shortcut action) {
  switch (action) {
    case Shortcut::NewTab: case Shortcut::CloseTab: case Shortcut::NewWindow: case Shortcut::NewPrivateWindow:
    case Shortcut::ReopenClosed: case Shortcut::NextTab: case Shortcut::PreviousTab: case Shortcut::RecentTab:
    case Shortcut::RecentTabBack: case Shortcut::CloseWindow:
      return true;
    default:
      return false;
  }
}
bool ExecuteShortcut(const std::shared_ptr<State>& state, Shortcut action);
std::optional<Shortcut> ShortcutKeyFor(const std::shared_ptr<State>& state, int key, bool ctrl, bool shift, bool alt);
void RefreshShortcuts(const std::shared_ptr<State>& state);
bool ExecuteAccelerator(const std::shared_ptr<State>& state, int id);
std::optional<Shortcut> ChromeShortcut(int command);
}  // namespace red
