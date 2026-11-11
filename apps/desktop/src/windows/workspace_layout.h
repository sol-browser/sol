#pragma once
#include <algorithm>
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
inline WorkspaceArea WorkspaceContentArea(int width, int height, bool sidebar_visible, int sidebar_width = kSidebarWidth) {
  const int x = (sidebar_visible ? SidebarWidth(sidebar_width, width) : 0) + kContentInset;
  return {x, kToolbarHeight, std::max(1, width - x - kContentInset),
          std::max(1, height - kToolbarHeight - kContentInset)};
}
}  // namespace red
