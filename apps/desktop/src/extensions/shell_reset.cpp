#include "extensions/shell_reset.h"
#include "app/web_profile_repair.h"
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
  std::error_code error;
  // Website history there means someone's browsing data (Sol up to 0.1.2 could
  // run websites in Shell): keep it aside instead of deleting it.
  if (LatestWebVisit(shell / L"History") > 0) {
    SYSTEMTIME now{}; GetLocalTime(&now);
    wchar_t name[64];
    swprintf_s(name, L"Shell (reset %04u-%02u-%02u %02u%02u%02u)", now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond);
    for (int attempt = 0; attempt < 20 && !MoveFileExW(shell.c_str(), (profile / name).c_str(), 0); ++attempt)
      std::this_thread::sleep_for(std::chrono::milliseconds(150));
  }
  // Child processes can hold files for a moment after CefShutdown.
  for (int attempt = 0; attempt < 20; ++attempt) {
    error.clear(); fs::remove_all(shell, error);
    if (!error && !fs::exists(shell, error)) break;
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
  }
  if (fs::exists(shell, error)) return ShellReset::Failed;
  std::ofstream(profile / kDone, std::ios::trunc) << "Sol reset its interface profile (Shell) after an extension appeared there.\n";
  return ShellReset::Done;
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
