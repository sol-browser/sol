#pragma once
#include <cstdint>
#include <filesystem>

namespace red {
// Sol up to 0.1.2 could run websites in <profile>\Shell, the trusted UI
// profile: Chrome starts in Local State's "last used" profile, the floating
// sidebar (a Chrome-style browser in Shell) could make that Shell, and CEF's
// global context followed it. Tabs, logins, saved passwords and Web Store
// extensions then lived in Shell. Sol now always starts Chrome in Default
// (--profile-directory=Default, main_win.cpp); this repair, run before
// CefInitialize, keeps such a person's data: when Shell has newer website
// history than Default, Default is moved aside (kept, never deleted) and Shell
// becomes Default. A fresh Shell is created on start.
enum class WebProfileRepair { None, Moved, Failed };

// The newest http(s) visit in a Chrome "History" database (Chrome time,
// microseconds since 1601), or -1 when there is none or it cannot be read.
int64_t LatestWebVisit(const std::filesystem::path& history);
WebProfileRepair RepairWebProfile(const std::filesystem::path& profile);
}  // namespace red
