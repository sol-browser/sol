#pragma once
#include <algorithm>
#include <string_view>
namespace red {
inline constexpr int kSidebarWidth = 256;
// 150 DIP still fits four space buttons (compact bottom bar) and two columns
// of Favorites; schema v14 stores this range.
inline constexpr int kSidebarMinWidth = 150, kSidebarMaxWidth = 480;
inline int SidebarWidth(int preferred, int window_width) {
  return std::clamp(preferred, kSidebarMinWidth,
      std::clamp(window_width - 400, kSidebarMinWidth, kSidebarMaxWidth));
}
// The top bar is our title bar (frameless window): navigation, centered
// address, window controls. The page always starts below it.
inline constexpr int kToolbarHeight = 31;
inline constexpr int kContentInset = 8;
struct WorkspaceArea { int x, y, width, height; };
inline constexpr int kSidebarPeekInset = 6;
inline int SidebarPeekTop(bool browser_fullscreen) {
  return browser_fullscreen ? kSidebarPeekInset : kToolbarHeight;
}
// The Spaces board is as wide as its columns need (library.css: an 84 DIP
// rail, 24 of row padding, 268-wide columns 16 apart and a 56-wide new-Space
// slot), up to what keeps 320 DIP of the page visible. 0: count unknown.
inline int LibraryBoardWidth(int spaces) {
  return spaces > 0 ? 84 + 24 + spaces * (268 + 16) - 16 + 56 + 4 : 1120;
}
inline int WorkspaceRailWidth(int width, bool sidebar_visible, int sidebar_width, std::string_view library_section,
                              int library_spaces = 0) {
  if (!library_section.empty())
    return std::clamp(width - 336, 0, library_section == "spaces" ? std::max(456, LibraryBoardWidth(library_spaces)) : 456);
  return sidebar_visible ? SidebarWidth(sidebar_width, width) : 0;
}
// With Library open the page keeps the size it has beside the sidebar and is
// only shifted right, cut off by the window edge (as in Arc): opening Library,
// changing its section and switching tabs from it never resize a page.
inline WorkspaceArea WorkspaceContentArea(int width, int height, bool sidebar_visible, int sidebar_width = kSidebarWidth,
                                          std::string_view library_section = {}, int library_spaces = 0) {
  const int x = WorkspaceRailWidth(width, sidebar_visible, sidebar_width, library_section, library_spaces) + kContentInset;
  const int beside = WorkspaceRailWidth(width, sidebar_visible, sidebar_width, {}) + kContentInset;
  return {x, kToolbarHeight, std::max(1, width - beside - kContentInset),
          std::max(1, height - kToolbarHeight - kContentInset)};
}
// The part of the page area inside the window (DIP width).
inline int VisibleContentWidth(const WorkspaceArea& area, int window_width) {
  return std::max(1, std::min(area.width, window_width - area.x - kContentInset));
}
}  // namespace red
