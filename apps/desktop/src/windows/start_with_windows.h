#pragma once
// Start with Windows (docs/little-sol.md): at sign-in only sol-tray.exe starts, so Ctrl+Alt+N
// and links from other apps work at once while the browser stays closed until it is used.
// The entry is HKCU\Software\Microsoft\Windows\CurrentVersion\Run, value "SolBrowser".
#include <algorithm>
#include <filesystem>
#include <string>

namespace red {
// The Run value's command: sol-tray.exe with the settings it needs before Sol has run (search
// engine, completions) and a non-default profile. Empty when |engine| is not a plain id or the
// profile path cannot be quoted.
inline std::wstring StartWithWindowsCommand(const std::filesystem::path& tray, const std::string& engine, bool suggestions,
                                            const std::wstring& profile_dir) {
  if (engine.empty() || engine.size() > 32 || !std::all_of(engine.begin(), engine.end(), [](char c) { return c >= 'a' && c <= 'z'; }))
    return {};
  if (tray.native().find(L'"') != std::wstring::npos || profile_dir.find(L'"') != std::wstring::npos) return {};
  std::wstring command = L"\"" + tray.native() + L"\" --search-engine=" + std::wstring(engine.begin(), engine.end());
  if (suggestions) command += L" --search-suggestions=1";
  if (!profile_dir.empty()) command += L" \"--profile-dir=" + profile_dir + L"\"";
  return command;
}
// Writes or removes the entry. Only the installed Sol (%LOCALAPPDATA%\SolBrowser\current) does
// this; development builds and probes never touch it. Returns false when nothing could be done.
bool ApplyStartWithWindows(bool enabled, const std::string& engine, bool suggestions, const std::wstring& profile_dir);
// Velopack uninstall.
void RemoveStartWithWindows();
}  // namespace red
