// sol-sync: a developer tool that runs cloud-folder sync passes on a Sol
// profile while Sol is closed (docs/sync.md, "Testing by hand"). It syncs
// the workspace (Spaces, Pinned, Favorites, pinned splits). It cannot reach
// Chromium's password store, so it carries password records through
// unchanged and never deletes or applies them. Not shipped with Sol.
//
//   sol-sync folders
//   sol-sync setup  --profile <dir> --folder <cloud folder> [--join merge|synced]
//   sol-sync pass   --profile <dir> [--set-password <realm> <username> <password or ->]   (testing)
//   sol-sync status --profile <dir>
//   sol-sync off    --profile <dir>
//
// <dir> is a Sol profile root (default %LOCALAPPDATA%\Sol\User Data, or the
// --profile-dir given to sol.exe). The passphrase is read from the console
// without echo, or from SOL_SYNC_PASSPHRASE for scripted tests.
#include "storage/tree_store.h"
#include "sync/sync_pass.h"
#include "sync/sync_passwords.h"
#include "sync/sync_tree.h"
#include <vector>
#include <windows.h>
#include <chrono>
#include <cstdio>
#include <map>
#include <string>

namespace {
namespace fs = std::filesystem;
using namespace red::sync;

std::string Utf8(const std::wstring& text) {
  if (text.empty()) return {};
  const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
  std::string out(static_cast<size_t>(size), '\0');
  WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), size, nullptr, nullptr);
  return out;
}
void Print(const std::string& text) { std::fwrite(text.data(), 1, text.size(), stdout); std::fputc('\n', stdout); }
int Fail(const std::string& text) { std::fwrite(text.data(), 1, text.size(), stderr); std::fputc('\n', stderr); return 1; }

std::string ReadPassphrase() {
  wchar_t env[1024]{};
  if (const DWORD size = GetEnvironmentVariableW(L"SOL_SYNC_PASSPHRASE", env, static_cast<DWORD>(std::size(env))); size && size < std::size(env)) {
    auto value = Utf8(std::wstring(env, size));
    SecureZeroMemory(env, sizeof(env));
    return value;
  }
  const HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
  DWORD mode = 0;
  const bool console = GetConsoleMode(input, &mode);
  if (console) SetConsoleMode(input, mode & ~ENABLE_ECHO_INPUT);
  std::fputs("Sync passphrase: ", stdout);
  std::fflush(stdout);
  wchar_t buffer[1024]{};
  DWORD read = 0;
  std::wstring line;
  if (console && ReadConsoleW(input, buffer, static_cast<DWORD>(std::size(buffer) - 1), &read, nullptr)) line.assign(buffer, read);
  if (console) SetConsoleMode(input, mode);
  std::fputc('\n', stdout);
  while (!line.empty() && (line.back() == L'\n' || line.back() == L'\r')) line.pop_back();
  auto value = Utf8(line);
  SecureZeroMemory(buffer, sizeof(buffer));
  SecureZeroMemory(line.data(), line.size() * sizeof(wchar_t));
  return value;
}

// Chromium holds <profile>\lockfile open while Sol runs on that profile.
bool SolRunning(const fs::path& profile) {
  const auto lock = profile / L"lockfile";
  const HANDLE file = CreateFileW(lock.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file != INVALID_HANDLE_VALUE) { CloseHandle(file); return false; }
  return GetLastError() == ERROR_SHARING_VIOLATION;
}

fs::path DefaultProfile() {
  wchar_t local[MAX_PATH]{};
  const DWORD size = GetEnvironmentVariableW(L"LOCALAPPDATA", local, MAX_PATH);
  return size && size < MAX_PATH ? fs::path(local) / L"Sol" / L"User Data" : fs::path();
}

int Folders() {
  const auto folders = DetectCloudFolders(GatherDetectionInputs());
  if (folders.empty()) Print("No OneDrive, Dropbox or Google Drive folder found. Pass any folder with --folder.");
  for (const auto& folder : folders) Print(std::string(ProviderName(folder.provider)) + "\t" + Utf8(folder.path.wstring()));
  return 0;
}

int Setup(const fs::path& profile, const fs::path& folder, JoinMode mode) {
  if (folder.empty()) return Fail("setup needs --folder <cloud folder>");
  if (IsSetUp(profile / L"Sync")) return Fail("This profile already syncs. Run 'sol-sync off' first.");
  auto passphrase = ReadPassphrase();
  LocalSync local;
  OpenedVault vault;
  const auto status = SetUp(profile / L"Sync", folder, passphrase, mode, local, vault);
  Wipe(passphrase);
  vault.Close();
  switch (status) {
    case SetupStatus::Ok: Print("Set up. Device " + local.device + ". Run 'sol-sync pass' while Sol is closed."); return 0;
    case SetupStatus::WrongPassphrase: return Fail("Wrong passphrase for the vault in this folder.");
    case SetupStatus::WeakPassphrase: return Fail("Use a passphrase of at least 10 characters.");
    case SetupStatus::Corrupt: return Fail("The vault in this folder is damaged or from a newer Sol.");
    default: return Fail("Could not set up sync in this folder.");
  }
}

int Pass(const fs::path& profile, const std::vector<std::string>& set_password) {
  if (SolRunning(profile)) return Fail("Sol is running on this profile. Close it first.");
  LocalSync local;
  OpenedVault vault;
  if (const auto loaded = Load(profile / L"Sync", local, vault); loaded != LoadStatus::Ok)
    return Fail(loaded == LoadStatus::NotSetUp ? "Not set up. Run 'sol-sync setup' first." : "Cannot read this PC's sync key or settings.");
  std::error_code error;
  if (!fs::is_directory(profile / L"BrowserData", error)) { vault.Close(); return Fail("Not a Sol profile (no BrowserData folder)."); }
  red::TreeStore store;
  auto read = store.Open(profile / L"BrowserData" / L"workspace.sqlite3");
  if (!read) { vault.Close(); return Fail("Cannot open the workspace: " + read.error); }
  // Passwords are carried through as the base has them: never stamped as
  // changed or deleted, never applied (Sol does that through Chromium).
  Values values;
  if (!ReadBaseValues(local, vault, values)) { vault.Close(); return Fail("Cannot read this PC's last sync state."); }
  std::erase_if(values, [](const auto& item) { return IsTreeKey(item.first); });
  for (auto& item : TreeValues(read.snapshot)) values.insert(std::move(item));
  // Testing only: this "PC" adds, changes (password) or removes ("-") one sign-in of the default profile.
  if (!set_password.empty()) {
    const auto key = PasswordKey("default", set_password[0], set_password[1]);
    if (set_password[2] == "-") values.erase(key);
    else values[key] = PasswordValue(set_password[0], set_password[2]);
  }
  Prepared prepared;
  const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
  if (const auto status = Prepare(local, vault, values, now, &IsTreeKey, prepared); status != PassStatus::Ok) {
    vault.Close();
    WipeValues(values);
    return Fail(status == PassStatus::FolderMissing ? "The cloud folder is missing." : status == PassStatus::VaultChanged ? "The vault in the folder was replaced; set up again." : "Cannot read the sync files.");
  }
  WipeValues(values);
  std::set<std::string> failed;
  size_t password_changes = 0;
  for (const auto& [key, value] : prepared.changes.upsert) if (!IsTreeKey(key)) { failed.insert(key); ++password_changes; }
  for (const auto& key : prepared.changes.remove) if (!IsTreeKey(key)) { failed.insert(key); ++password_changes; }
  const bool tree_touched = prepared.use_synced_tree ||
      std::any_of(prepared.changes.upsert.begin(), prepared.changes.upsert.end(), [](const auto& item) { return IsTreeKey(item.first); }) ||
      std::any_of(prepared.changes.remove.begin(), prepared.changes.remove.end(), [](const auto& key) { return IsTreeKey(key); });
  std::string tree_result = "workspace unchanged";
  if (tree_touched) {
    Values tree_values;
    for (const auto& [key, value] : prepared.live) if (IsTreeKey(key)) tree_values.emplace(key, value);
    const auto applied = ApplyTreeValues(read.snapshot, tree_values);
    const auto saved = applied.ok && applied.changed ? store.ApplySynced(applied.tree, read.snapshot.revision) : red::TreeResult{};
    if (!applied.ok || (applied.changed && !saved)) {
      for (const auto& [key, value] : prepared.changes.upsert) if (IsTreeKey(key)) failed.insert(key);
      for (const auto& key : prepared.changes.remove) if (IsTreeKey(key)) failed.insert(key);
      tree_result = "workspace NOT updated: " + (applied.ok ? saved.error : std::string("synced records give no valid workspace"));
    } else if (applied.changed) tree_result = "workspace updated";
  }
  bool wrote = false;
  const auto committed = Commit(local, vault, prepared, failed, &wrote);
  vault.Close();
  if (committed != PassStatus::Ok) return Fail("Pass not saved; nothing lost, run it again.");
  Print(tree_result + "; " + std::to_string(prepared.devices) + " other device(s)" +
        (prepared.unreadable ? ", " + std::to_string(prepared.unreadable) + " unreadable" : "") + "; " +
        (wrote ? "device file written" : "device file already current") +
        (password_changes ? "; " + std::to_string(password_changes) + " password change(s) left for Sol" : ""));
  return 0;
}

int Status(const fs::path& profile) {
  LocalSync local;
  OpenedVault vault;
  const auto loaded = Load(profile / L"Sync", local, vault);
  if (loaded == LoadStatus::NotSetUp) { Print("Not set up."); return 0; }
  if (loaded != LoadStatus::Ok) return Fail("Cannot read this PC's sync key or settings.");
  Print("Device  " + local.device);
  Print("Folder  " + Utf8(local.folder.wstring()));
  if (!local.join.empty()) Print("Join    " + local.join + " (on the first pass)");
  for (const auto& file : ReadDeviceFiles(local.SyncDir(), vault)) {
    size_t live = 0, passwords = 0;
    for (const auto& [key, record] : file.state) if (!record.deleted) { ++live; passwords += !IsTreeKey(key); }
    Print(file.device + (file.device == local.device ? " (this PC)" : "") + (file.ok ? ": " + std::to_string(live) + " records, " + std::to_string(passwords) + " passwords" : ": unreadable"));
  }
  vault.Close();
  return 0;
}

int Off(const fs::path& profile) {
  LocalSync local;
  OpenedVault vault;
  if (Load(profile / L"Sync", local, vault) != LoadStatus::Ok) return Fail("Not set up, or this PC's sync settings cannot be read.");
  vault.Close();
  return TurnOff(local) ? (Print("Sync turned off for this profile; other PCs keep their data."), 0) : Fail("Could not remove every sync file.");
}
}  // namespace

int wmain(int argc, wchar_t** argv) {
  SetConsoleOutputCP(CP_UTF8);
  if (argc < 2) return Fail("usage: sol-sync folders | setup --profile <dir> --folder <dir> [--join merge|synced] | pass | status | off  [--profile <dir>]");
  const std::wstring command = argv[1];
  std::map<std::wstring, std::wstring> options;
  for (int i = 2; i + 1 < argc; i += 2) options[argv[i]] = argv[i + 1];
  const fs::path profile = options.contains(L"--profile") ? fs::path(options[L"--profile"]) : DefaultProfile();
  if (command == L"folders") return Folders();
  if (profile.empty()) return Fail("Give --profile <Sol profile root>.");
  if (command == L"setup") {
    const auto join = options.contains(L"--join") ? options[L"--join"] : L"merge";
    if (join != L"merge" && join != L"synced") return Fail("--join is merge or synced");
    return Setup(profile, options.contains(L"--folder") ? fs::path(options[L"--folder"]) : fs::path(), join == L"synced" ? JoinMode::UseSynced : JoinMode::Merge);
  }
  if (command == L"pass") {
    std::vector<std::string> set_password;
    for (int i = 2; i + 3 < argc; ++i)
      if (std::wstring_view(argv[i]) == L"--set-password") { set_password = {Utf8(argv[i + 1]), Utf8(argv[i + 2]), Utf8(argv[i + 3])}; break; }
    return Pass(profile, set_password);
  }
  if (command == L"status") return Status(profile);
  if (command == L"off") return Off(profile);
  return Fail("Unknown command.");
}
