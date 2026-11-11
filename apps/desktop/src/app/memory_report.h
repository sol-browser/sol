#pragma once
#include <filesystem>
#include <memory>
#include <string>

namespace red {
struct State;

// The renderer host ID inside a CEF frame identifier ("<host ID in hex>-<frame token>",
// cef/libcef/common/frame_util.cc). Chromium passes the same ID to that renderer process as
// --renderer-client-id, so a report can attribute each renderer process's memory to the tabs and
// Sol windows it hosts. -1 when the identifier has another form.
inline int RendererHostId(const std::string& frame_identifier) {
  const auto dash = frame_identifier.find('-');
  if (dash == std::string::npos || dash == 0 || dash > 8) return -1;
  int value = 0;
  for (size_t i = 0; i < dash; ++i) {
    const char c = frame_identifier[i];
    const int digit = c >= '0' && c <= '9' ? c - '0' : c >= 'A' && c <= 'F' ? c - 'A' + 10 : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
    if (digit < 0 || value > 0x7ffffff) return -1;
    value = value * 16 + digit;
  }
  return value;
}

// Writes which browsers this Sol runs, as JSON: role (tab, pinned, favorite, or which Sol window),
// site host only (never a path, query or title), whether it is shown, active or audible, and its
// renderer host ID. Triggered by `sol.exe --memory-report=<new .json file>` while Sol runs;
// scripts/memory_report.py joins it with process memory. Refuses an existing file or another
// extension. Returns false when nothing was written.
bool WriteMemoryReport(const std::shared_ptr<State>& state, const std::filesystem::path& path);
}  // namespace red
