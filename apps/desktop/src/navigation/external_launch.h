#pragma once
#include <vector>
#include <string>
#include "include/cef_command_line.h"
namespace red {
// Only web URLs or explicit existing local files; no internal/extension origin.
std::vector<std::string> ExternalLaunchUrls(CefRefPtr<CefCommandLine> command);
}  // namespace red
