#include "app/updates.h"
#include "app/session.h"
#include "workspace/controller.h"
#include "include/base/cef_callback.h"
#include "include/wrapper/cef_closure_task.h"
#include <windows.h>
#include <shellapi.h>
#include <chrono>
#include <cwchar>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include "Velopack.hpp"
#include "include/base/cef_logging.h"
#include "windows/default_browser.h"

namespace red {
namespace {
// The version waiting for a restart. Never destroyed: the check thread may
// still run while the process exits.
struct Ready { std::mutex mutex; std::string version; };
Ready& ReadyState() { static auto* state = new Ready; return *state; }
void SetReady(const std::string& version) { auto& r = ReadyState(); std::lock_guard lock(r.mutex); r.version = version; }
// icacls <folder> /grant *S-1-15-2-2:(OI)(CI)(RX): "ALL RESTRICTED APPLICATION
// PACKAGES" may read and execute Sol's own files, as the build and setup grant.
bool GrantSandboxAccess(const std::filesystem::path& folder) {
  wchar_t system[MAX_PATH];
  if (!GetSystemDirectoryW(system, MAX_PATH)) return false;
  std::wstring command = L"\"" + (std::filesystem::path(system) / L"icacls.exe").wstring() + L"\" \"" + folder.wstring() +
                         L"\" /grant *S-1-15-2-2:(OI)(CI)(RX) /Q";
  STARTUPINFOW startup{sizeof(startup)}; PROCESS_INFORMATION process{};
  if (!CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process)) return false;
  WaitForSingleObject(process.hProcess, 20000);  // Velopack ends hooks after 30 s.
  DWORD code = 1; GetExitCodeProcess(process.hProcess, &code);
  CloseHandle(process.hThread); CloseHandle(process.hProcess);
  return code == 0;
}

// The running sol.exe is the new version (Velopack starts it from its folder).
// Grant on that folder and on the app root, so later versions placed under the
// root inherit the access even if a hook is skipped.
void GrantAfterChange(void*, const char*) {
  wchar_t module[MAX_PATH];
  const DWORD length = GetModuleFileNameW(nullptr, module, MAX_PATH);
  if (!length || length == MAX_PATH) return;
  const auto folder = std::filesystem::path(module).parent_path();
  GrantSandboxAccess(folder.parent_path());
  GrantSandboxAccess(folder);
}

// Sol used an Inno setup (%LOCALAPPDATA%\Sol\Application) before Velopack. Its
// own uninstaller, run silently, closes that Sol, removes its files, shortcut,
// Apps entry and registration, and keeps the profile (only /PURGEPROFILE=1
// deletes it). Wait for it, so its cleanup cannot remove what this install
// registers next; Velopack ends hooks after 30 s.
void RemoveInnoInstall() {
  constexpr wchar_t kKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\{55D75F28-1394-4E21-88D0-D3BA0D757491}_is1";
  wchar_t command[MAX_PATH * 2]{};
  DWORD bytes = sizeof(command);
  if (RegGetValueW(HKEY_CURRENT_USER, kKey, L"UninstallString", RRF_RT_REG_SZ, nullptr, command, &bytes) != ERROR_SUCCESS) return;
  std::wstring line = std::wstring(command) + L" /VERYSILENT /SUPPRESSMSGBOXES /NORESTART";
  STARTUPINFOW startup{sizeof(startup)}; PROCESS_INFORMATION process{};
  if (!CreateProcessW(nullptr, line.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process)) return;
  CloseHandle(process.hThread); CloseHandle(process.hProcess);
  // The uninstaller hands over to a temporary copy; it is done when its Apps entry is gone.
  for (int waited = 0; waited < 22000; waited += 250) {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kKey, 0, KEY_READ, &key) != ERROR_SUCCESS) break;
    RegCloseKey(key); Sleep(250);
  }
}

// Only the real install (%LOCALAPPDATA%\SolBrowser\current) owns Sol's
// registration and replaces the Inno install. A test package installed under
// another packId must not remove or unregister the person's Sol.
bool IsMainInstall() {
  wchar_t module[MAX_PATH];
  const DWORD length = GetModuleFileNameW(nullptr, module, MAX_PATH);
  if (!length || length == MAX_PATH) return false;
  const auto installed = InstalledBrowserPath();
  std::error_code error;
  return !installed.empty() && installed.parent_path().filename() == L"current" &&
         std::filesystem::equivalent(installed, module, error) && !error;
}

void AfterInstall(void* data, const char* version) {
  GrantAfterChange(data, version);
  if (IsMainInstall()) RemoveInnoInstall();
}

void BeforeUninstall(void*, const char*) { if (IsMainInstall()) Unregister(); }

std::unique_ptr<Velopack::UpdateManager> Manager() {
  wchar_t feed[1024]{};
  const DWORD length = GetEnvironmentVariableW(L"SOL_UPDATE_FEED", feed, 1024);
  if (length > 0 && length < 1024) {
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, feed, -1, nullptr, 0, nullptr, nullptr);
    std::string utf8(bytes > 0 ? bytes - 1 : 0, '\0');
    WideCharToMultiByte(CP_UTF8, 0, feed, -1, utf8.data(), bytes, nullptr, nullptr);
    return std::make_unique<Velopack::UpdateManager>(utf8);
  }
  return std::make_unique<Velopack::UpdateManager>(std::make_unique<Velopack::GithubSource>("https://github.com/sol-browser/sol"));
}
}  // namespace

namespace {
enum class Check { Ready, Nothing, NotInstalled };

Check CheckOnce(const std::function<void(const std::string&)>& ready) {
  std::unique_ptr<Velopack::UpdateManager> manager;
  try {
    manager = Manager();
  } catch (const std::exception& error) {
    // Development builds and copies outside a Velopack install never update.
    LOG(WARNING) << "Updates off: " << error.what();
    return Check::NotInstalled;
  }
  try {
    if (const auto pending = manager->UpdatePendingRestart()) { SetReady(pending->Version); ready(pending->Version); return Check::Ready; }
    const auto update = manager->CheckForUpdates();
    if (!update) return Check::Nothing;
    manager->DownloadUpdates(*update);
    LOG(WARNING) << "Update " << update->TargetFullRelease.Version << " downloaded; applies on next start";
    SetReady(update->TargetFullRelease.Version);
    ready(update->TargetFullRelease.Version);
    return Check::Ready;
  } catch (const std::exception& error) {
    // No network, GitHub unavailable or rate-limited: try again next time.
    LOG(WARNING) << "Update check failed: " << error.what();
    return Check::Nothing;
  }
}

// Four hours, like browsers that update in the background. SOL_UPDATE_INTERVAL
// (seconds, at least 30) shortens it for testing.
std::chrono::seconds CheckInterval() {
  wchar_t value[32]{};
  const DWORD length = GetEnvironmentVariableW(L"SOL_UPDATE_INTERVAL", value, 32);
  if (length > 0 && length < 32) {
    const long seconds = std::wcstol(value, nullptr, 10);
    if (seconds >= 30) return std::chrono::seconds(seconds);
  }
  return std::chrono::hours(4);
}
}  // namespace

void StartUpdateCheck(std::function<void(const std::string&)> ready) {
  std::thread([ready = std::move(ready)] {
    const auto interval = CheckInterval();
    // Until an update waits for a restart; the process ending stops the thread.
    for (Check result = CheckOnce(ready); result == Check::Nothing; result = CheckOnce(ready))
      std::this_thread::sleep_for(interval);
  }).detach();
}

std::string ReadyUpdate() { auto& r = ReadyState(); std::lock_guard lock(r.mutex); return r.version; }

bool PrepareRestartToUpdate() {
  try {
    const auto manager = Manager();
    const auto pending = manager->UpdatePendingRestart();
    if (!pending) return false;
    // Velopack's updater waits for Sol to exit, applies the version and starts it
    // again with the launch switches that choose the window and profile. A URL
    // or other one-off argument is not repeated.
    std::vector<std::string> restart{"--workspace"};
    int count = 0;
    if (wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &count)) {
      for (int i = 1; i < count; ++i) {
        const std::wstring arg(argv[i]);
        if (!arg.starts_with(L"--profile-dir=")) continue;
        const int bytes = WideCharToMultiByte(CP_UTF8, 0, arg.c_str(), -1, nullptr, 0, nullptr, nullptr);
        std::string utf8(bytes > 0 ? bytes - 1 : 0, '\0');
        WideCharToMultiByte(CP_UTF8, 0, arg.c_str(), -1, utf8.data(), bytes, nullptr, nullptr);
        restart.push_back(std::move(utf8));
      }
      LocalFree(argv);
    }
    manager->WaitExitThenApplyUpdates(*pending, true, true, restart);
    return true;
  } catch (const std::exception& error) {
    LOG(WARNING) << "Restart to update failed: " << error.what();
    return false;
  }
}

bool RequestUpdateRestart(const std::shared_ptr<State>& state) {
  if (!state || !state->session || !state->workspace || state->workspace->WindowClosing() || ReadyUpdate().empty()) return false;
  if (!PrepareRestartToUpdate()) return false;
  CefPostTask(TID_UI, base::BindOnce([](std::weak_ptr<State> weak) {
    if (const auto owner=weak.lock(); owner && owner->session)
      for (const auto& window:owner->session->Windows()) if(window->window)window->window->Close();
  },std::weak_ptr<State>(state)));
  return true;
}

void RunUpdateHooks() {
  Velopack::VelopackApp::Build()
      .OnAfterInstall(&AfterInstall)
      .OnAfterUpdate(&GrantAfterChange)
      .OnBeforeUninstall(&BeforeUninstall)
      // The profile lives outside Velopack's folder (%LOCALAPPDATA%\Sol\User Data)
      // and is never touched on uninstall (docs/sol-plan.md).
      .Run();
}
}  // namespace red
