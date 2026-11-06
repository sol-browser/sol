#include "extensions/shell_reset.h"
#include <windows.h>
#include <atomic>
#include <fstream>
#include <string>
#include <thread>

namespace red {
namespace {
namespace fs = std::filesystem;
constexpr wchar_t kDone[] = L"shell-reset-done";
std::atomic<bool> scheduled{false};
}  // namespace

bool ShellResetAllowed(const fs::path& profile, fs::file_time_type now, std::chrono::minutes window) {
  std::error_code error;
  const auto done = fs::last_write_time(profile / kDone, error);
  return error || now - done > window || done > now;
}
ShellReset ResetShellProfile(const fs::path& profile) {
  if (profile.empty() || !profile.is_absolute()) return ShellReset::Failed;
  const auto shell = profile / L"Shell";
  if (GetFileAttributesW(shell.c_str()) == INVALID_FILE_ATTRIBUTES) {
    const auto error = GetLastError();
    if (error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND) return ShellReset::Failed;
  } else {
    // Older Sol builds could use Shell for browsing, even if its History is missing.
    // Keep the entire profile. Moving it aside also leaves a clean path for CEF.
    SYSTEMTIME now{}; GetLocalTime(&now);
    wchar_t name[64];
    swprintf_s(name, L"Shell (reset %04u-%02u-%02u %02u%02u%02u)", now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond);
    auto backup = profile / name;
    int suffix = 0;
    bool moved = false;
    for (int attempt = 0; attempt < 20; ++attempt) {
      if (MoveFileExW(shell.c_str(), backup.c_str(), 0)) { moved = true; break; }
      if (GetFileAttributesW(backup.c_str()) != INVALID_FILE_ATTRIBUTES)
        backup = profile / (std::wstring(name) + L" " + std::to_wstring(++suffix));
      else  // Child processes can briefly hold files after CefShutdown.
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
    }
    if (!moved) return ShellReset::Failed;
  }
  std::ofstream receipt(profile / kDone, std::ios::trunc);
  receipt << "Sol moved its interface profile (Shell) aside after an extension appeared there.\n";
  receipt.close();
  return receipt ? ShellReset::Done : ShellReset::Failed;
}
void ScheduleShellReset() { scheduled = true; }
bool ShellResetScheduled() { return scheduled; }
bool Relaunch() {
  std::wstring line = GetCommandLineW();
  STARTUPINFOW startup{sizeof(startup)}; PROCESS_INFORMATION process{};
  if (!CreateProcessW(nullptr, line.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &process)) return false;
  CloseHandle(process.hThread); CloseHandle(process.hProcess);
  return true;
}
}  // namespace red
