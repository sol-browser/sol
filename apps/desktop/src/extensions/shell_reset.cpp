#include "extensions/shell_reset.h"
#include <windows.h>
#include <string>
#include <thread>

namespace red {
namespace {
namespace fs = std::filesystem;
constexpr wchar_t kAttempted[] = L"shell-protection-restart-attempted";
constexpr wchar_t kPending[] = L"sol-rebuild-interface";
bool scheduled = false;
}  // namespace

ShellQuarantine QuarantineShellProfile(const fs::path& profile) {
  if (!profile.is_absolute()) return ShellQuarantine::Failed;
  const auto shell = profile / L"Shell";
  if (GetFileAttributesW(shell.c_str()) == INVALID_FILE_ATTRIBUTES) {
    const auto error = GetLastError();
    return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND
        ? ShellQuarantine::Missing : ShellQuarantine::Failed;
  }
  // Older Sol builds could use Shell for browsing, even without a History file.
  SYSTEMTIME now{};
  GetLocalTime(&now);
  wchar_t name[64];
  swprintf_s(name, L"Shell (reset %04u-%02u-%02u %02u%02u%02u)", now.wYear, now.wMonth, now.wDay,
             now.wHour, now.wMinute, now.wSecond);
  auto backup = profile / name;
  int suffix = 0;
  for (int attempt = 0; attempt < 20; ++attempt) {
    if (MoveFileExW(shell.c_str(), backup.c_str(), 0)) return ShellQuarantine::Moved;
    if (GetFileAttributesW(backup.c_str()) != INVALID_FILE_ATTRIBUTES)
      backup = profile / (std::wstring(name) + L" " + std::to_wstring(++suffix));
    else  // Child processes can briefly hold files after CefShutdown.
      std::this_thread::sleep_for(std::chrono::milliseconds(150));
  }
  return ShellQuarantine::Failed;
}
bool ScheduleShellReset(const fs::path& profile) {
  if (!profile.is_absolute()) return false;
  const HANDLE claim = CreateFileW((profile / kAttempted).c_str(), GENERIC_WRITE, 0,
      nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (claim == INVALID_HANDLE_VALUE) return false;
  CloseHandle(claim);
  const HANDLE pending = CreateFileW((profile / L"Shell" / kPending).c_str(), GENERIC_WRITE, 0,
      nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (pending == INVALID_HANDLE_VALUE) return false;
  CloseHandle(pending);
  scheduled = true;
  return true;
}
bool ShellResetScheduled() { return scheduled; }
bool ShellResetPending(const fs::path& profile) {
  return GetFileAttributesW((profile / L"Shell" / kPending).c_str()) != INVALID_FILE_ATTRIBUTES;
}
bool Relaunch() {
  std::wstring line = GetCommandLineW();
  STARTUPINFOW startup{sizeof(startup)}; PROCESS_INFORMATION process{};
  if (!CreateProcessW(nullptr, line.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &process)) return false;
  CloseHandle(process.hThread); CloseHandle(process.hProcess);
  return true;
}
}  // namespace red
