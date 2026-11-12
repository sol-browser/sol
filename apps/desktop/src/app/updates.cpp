#include "app/updates.h"
#include "red_version.h"
#include "app/session.h"
#include "app/release_notes.h"
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
// What the update window shows. Written by whichever check runs, read by the window.
struct Status { std::mutex mutex; UpdateStatus value{UpdatePhase::Idle, RED_APP_VERSION, "", 0}; };
Status& StatusState() { static auto* state = new Status; return *state; }
void SetStatus(UpdatePhase phase, const std::string& version = "", int percent = 0) {
  auto& s = StatusState(); std::lock_guard lock(s.mutex);
  s.value.phase = phase; s.value.version = version; s.value.percent = percent;
}
void SetProgress(int percent) {
  auto& s = StatusState(); std::lock_guard lock(s.mutex);
  if (s.value.phase == UpdatePhase::Downloading) s.value.percent = percent < 0 ? 0 : percent > 100 ? 100 : percent;
}
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
  wchar_t executable[MAX_PATH];
  const DWORD length = GetModuleFileNameW(nullptr, executable, MAX_PATH);
  if (!length || length == MAX_PATH) return;
  const auto folder = std::filesystem::path(executable).parent_path();
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
  wchar_t executable[MAX_PATH];
  const DWORD length = GetModuleFileNameW(nullptr, executable, MAX_PATH);
  if (!length || length == MAX_PATH) return false;
  const auto installed = InstalledBrowserPath();
  std::error_code error;
  return !installed.empty() && installed.parent_path().filename() == L"current" &&
         std::filesystem::equivalent(installed, executable, error) && !error;
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

// What one pass found, for the person who asked ("Check for Updates").
struct Found { bool feed_failed = false; bool downloaded = false; };

// The periodic thread and a manual check never run together: two downloads of
// one version would write the same files.
std::mutex& CheckMutex() { static auto* mutex = new std::mutex; return *mutex; }

Check CheckOnce(const std::function<void(const std::string&)>& ready, Found& found) {
  std::unique_ptr<Velopack::UpdateManager> manager;
  try {
    manager = Manager();
  } catch (const std::exception& error) {
    // Development builds and copies outside a Velopack install never update.
    LOG(WARNING) << "Updates off: " << error.what();
    SetStatus(UpdatePhase::NotInstalled);
    return Check::NotInstalled;
  }
  SetStatus(UpdatePhase::Checking);
  // A version downloaded earlier is offered at once, also without a network.
  std::string waiting;
  try {
    if (const auto pending = manager->UpdatePendingRestart()) {
      waiting = pending->Version;
      if (ReadyUpdate() != waiting) { SetReady(waiting); ready(waiting); }
    }
  } catch (const std::exception& error) {
    LOG(WARNING) << "Reading the downloaded update failed: " << error.what();
  }
  // The feed is still checked while a version waits: a release published after
  // that download replaces it, so the person goes straight to the latest
  // version instead of through each one in turn (0.1.3 -> 0.2.0 -> 0.3.0,
  // user report 2026-11-10).
  try {
    const auto update = manager->CheckForUpdates();
    if (update && UpdateReplacesWaiting(update->TargetFullRelease.Version, waiting)) {
      SetStatus(UpdatePhase::Downloading, update->TargetFullRelease.Version, 0);
      manager->DownloadUpdates(*update, [](void*, size_t percent) { SetProgress(static_cast<int>(percent)); });
      waiting = update->TargetFullRelease.Version;
      found.downloaded = true;
      LOG(WARNING) << "Update " << waiting << " downloaded; applies on next start";
      SetReady(waiting);
      ready(waiting);
    }
  } catch (const std::exception& error) {
    // No network, GitHub unavailable or rate-limited: try again next time.
    LOG(WARNING) << "Update check failed: " << error.what();
    found.feed_failed = true;
  }
  if (!waiting.empty()) SetStatus(UpdatePhase::Ready, waiting, 100);
  else SetStatus(found.feed_failed ? UpdatePhase::Failed : UpdatePhase::UpToDate);
  return waiting.empty() ? Check::Nothing : Check::Ready;
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
    // Keeps checking while an update waits, so a later release replaces it.
    // Only a copy outside a Velopack install stops; the process ending stops the thread.
    for (;;) {
      Check result;
      {
        std::lock_guard lock(CheckMutex());
        Found ignored;
        result = CheckOnce(ready, ignored);
      }
      if (result == Check::NotInstalled) return;
      std::this_thread::sleep_for(interval);
    }
  }).detach();
}

UpdateStatus CurrentUpdateStatus() { auto& s = StatusState(); std::lock_guard lock(s.mutex); return s.value; }

void StartUpdateCheckNow(const std::shared_ptr<State>& state) {
  std::thread([weak = std::weak_ptr<State>(state)] {
    std::unique_lock lock(CheckMutex(), std::try_to_lock);
    if (!lock.owns_lock()) return;  // The periodic check is working: the window shows its progress.
    Found found;
    // The sidebar offers "Restart to update" as soon as a version waits.
    const auto ready = [weak](const std::string& version) {
      LOG(WARNING) << "Update ready: " << version;
      CefPostTask(TID_UI, base::BindOnce([](std::weak_ptr<State> weak) {
        if (const auto owner = weak.lock(); owner && owner->session)
          for (const auto& window : owner->session->Windows()) if (window->workspace) window->workspace->Refresh();
      }, weak));
    };
    CheckOnce(ready, found);
    CefPostTask(TID_UI, base::BindOnce([](std::weak_ptr<State> weak) {
      if (const auto owner = weak.lock()) {
        const auto status = CurrentUpdateStatus();
        auto details = CefDictionaryValue::Create();
        details->SetInt("phase", static_cast<int>(status.phase)); details->SetString("version", status.version);
        owner->Record("update_check_result", details);
      }
    }, weak));
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
