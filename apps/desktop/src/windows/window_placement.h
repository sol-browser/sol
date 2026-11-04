#pragma once
#include <filesystem>
#include <optional>
#include "include/internal/cef_types_wrappers.h"
namespace red {
// The workspace window's last normal bounds (DIP screen coordinates) and
// whether it was maximized, kept in BrowserData/window.json. Read synchronously
// at startup (the workspace database loads later, after the window exists).
struct WindowPlacement {
  CefRect bounds;
  bool maximized = false;
};
// Returns nothing for a missing, malformed or implausible file. The caller
// still checks that the bounds land on a connected display.
std::optional<WindowPlacement> LoadWindowPlacement(const std::filesystem::path& file);
// Pure validation, shared with tests: sane size and position.
bool PlausibleWindowBounds(const CefRect& bounds);
void SaveWindowPlacement(const std::filesystem::path& file, const WindowPlacement& placement);
}  // namespace red
