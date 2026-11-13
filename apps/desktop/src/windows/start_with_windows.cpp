#include "windows/start_with_windows.h"
#include <windows.h>
#include <algorithm>
#include "windows/default_browser.h"

namespace red {
namespace {
constexpr wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t kValue[] = L"SolBrowser";

// This process is the installed Sol, not a development build or a test package.
bool IsInstalledSol() {
  wchar_t executable[MAX_PATH]{};
  const DWORD length = GetModuleFileNameW(nullptr, executable, MAX_PATH);
  if (!length || length >= MAX_PATH) return false;
  const auto installed = InstalledBrowserPath();
  std::error_code error;
  return !installed.empty() && installed.parent_path().filename() == L"current" &&
         std::filesystem::equivalent(installed, executable, error) && !error;
}
}  // namespace

bool ApplyStartWithWindows(bool enabled, const std::string& engine, bool suggestions, const std::wstring& profile_dir) {
  if (!IsInstalledSol()) return false;
  HKEY key = nullptr;
  if (RegCreateKeyExW(HKEY_CURRENT_USER, kRunKey, 0, nullptr, 0, KEY_QUERY_VALUE | KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS)
    return false;
  bool ok = false;
  if (enabled) {
    const auto command = StartWithWindowsCommand(InstalledBrowserPath().parent_path() / L"sol-tray.exe", engine, suggestions, profile_dir);
    // Rewritten only when it changed (settings saves are frequent).
    wchar_t current[2048]{};
    DWORD bytes = sizeof(current) - sizeof(wchar_t);
    const bool same = RegQueryValueExW(key, kValue, nullptr, nullptr, reinterpret_cast<BYTE*>(current), &bytes) == ERROR_SUCCESS &&
                      command == current;
    ok = !command.empty() && (same || RegSetValueExW(key, kValue, 0, REG_SZ, reinterpret_cast<const BYTE*>(command.c_str()),
                                                      static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t))) == ERROR_SUCCESS);
  } else {
    const auto status = RegDeleteValueW(key, kValue);
    ok = status == ERROR_SUCCESS || status == ERROR_FILE_NOT_FOUND;
  }
  RegCloseKey(key);
  return ok;
}

void RemoveStartWithWindows() {
  HKEY key = nullptr;
  if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_SET_VALUE, &key) != ERROR_SUCCESS) return;
  RegDeleteValueW(key, kValue);
  RegCloseKey(key);
}
}  // namespace red
