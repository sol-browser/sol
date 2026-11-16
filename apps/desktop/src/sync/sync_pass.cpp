#include "sync/sync_pass.h"
#include "import/formats/json_lite.h"
#include <system_error>

namespace red::sync {
namespace fs = std::filesystem;
namespace {
constexpr size_t kMaxConfig = 64 * 1024, kMaxBase = 128u * 1024 * 1024;
fs::path ConfigPath(const fs::path& dir) { return dir / "sync.json"; }
fs::path KeyPath(const fs::path& dir) { return dir / "vault-key.bin"; }
fs::path BasePath(const fs::path& dir) { return dir / "base.sol"; }
fs::path PendingPath(const fs::path& dir) { return dir / "pending-removal.json"; }
const char* JoinName(JoinMode mode) { return mode == JoinMode::UseSynced ? "synced" : "merge"; }

bool SaveConfig(const LocalSync& local) {
  const auto folder = local.folder.u8string();
  std::string text = "{\"format\":1,\"device\":";
  AppendJsonString(text, local.device);
  text += ",\"folder\":";
  AppendJsonString(text, std::string_view(reinterpret_cast<const char*>(folder.data()), folder.size()));
  text += ",\"vault\":\"" + Base64Encode(local.vault) + "\",\"join\":";
  AppendJsonString(text, local.join);
  text += "}\n";
  std::error_code error;
  fs::create_directories(local.dir, error);
  return !error && platform::AtomicWrite(ConfigPath(local.dir), text);
}

bool LoadBase(const LocalSync& local, const OpenedVault& vault, State& out) {
  out.clear();
  std::error_code error;
  if (!fs::exists(BasePath(local.dir), error)) return !error;  // First pass.
  std::string envelope;
  return ReadFileLimited(BasePath(local.dir), kMaxBase, envelope) && OpenState(vault, local.device, envelope, out);
}

bool HasLiveTree(const State& state, bool (*is_tree)(std::string_view)) {
  for (const auto& [key, record] : state) if (!record.deleted && key.starts_with("space/") && is_tree(key)) return true;
  return false;
}
}  // namespace

Prepared::~Prepared() {
  WipeState(merged);
  WipeState(base);
  WipeValues(changes.upsert);
  WipeValues(live);
}

SetupStatus SetUp(const fs::path& dir, const fs::path& cloud_folder, std::string_view passphrase, JoinMode mode, LocalSync& out,
                  OpenedVault& vault, const KdfParams& params) {
  vault.Close();
  LocalSync local{dir, NewDeviceId(), cloud_folder, {}, {}};
  if (local.device.empty()) return SetupStatus::Io;
  const auto sync_dir = SyncDirectory(cloud_folder);
  std::error_code error;
  if (!fs::is_directory(cloud_folder, error)) return SetupStatus::Io;
  VaultStatus status = OpenVault(sync_dir, passphrase, vault);
  if (status == VaultStatus::Missing) {
    status = CreateVault(sync_dir, passphrase, vault, params);
    mode = JoinMode::Merge;  // Nothing to join yet.
    // Another PC created the vault at the same moment: join that one.
    if (status == VaultStatus::Exists) status = OpenVault(sync_dir, passphrase, vault);
  }
  switch (status) {
    case VaultStatus::Ok: break;
    case VaultStatus::WrongPassphrase: return SetupStatus::WrongPassphrase;
    case VaultStatus::WeakPassphrase: return SetupStatus::WeakPassphrase;
    case VaultStatus::Corrupt: return SetupStatus::Corrupt;
    default: return SetupStatus::Io;
  }
  local.vault = vault.id;
  local.join = JoinName(mode);
  std::string protected_key;
  fs::create_directories(dir, error);
  std::error_code ignored;
  fs::remove(BasePath(dir), ignored);  // A previous setup's base belongs to another device id.
  if (error || !platform::ProtectLocalSecret(vault.key, protected_key) || !platform::AtomicWrite(KeyPath(dir), protected_key) || !SaveConfig(local)) {
    vault.Close();
    return SetupStatus::Io;
  }
  out = std::move(local);
  return SetupStatus::Ok;
}

bool TurnOff(const LocalSync& local) {
  // A folder that is not there now (its cloud app is not running, a drive is
  // unplugged) keeps this PC's file: remember it, so RemovePendingDeviceFile
  // deletes it once the folder is back instead of leaving it to the others.
  std::error_code there_error;
  const bool there = fs::is_directory(local.SyncDir() / "devices", there_error);
  bool ok = there && RemoveDeviceFile(local.SyncDir(), local.device);
  if (!ok && ValidDeviceId(local.device) && !local.folder.empty()) {
    const auto folder = local.folder.u8string();
    std::string text = "{\"device\":";
    AppendJsonString(text, local.device);
    text += ",\"folder\":";
    AppendJsonString(text, std::string_view(reinterpret_cast<const char*>(folder.data()), folder.size()));
    text += "}\n";
    ok = platform::AtomicWrite(PendingPath(local.dir), text);
  }
  for (const auto& path : {BasePath(local.dir), KeyPath(local.dir), ConfigPath(local.dir)}) {
    std::error_code error;
    fs::remove(path, error);
    ok = ok && !error;
  }
  return ok;
}

bool RemovePendingDeviceFile(const fs::path& dir) {
  std::error_code error;
  if (!fs::exists(PendingPath(dir), error)) return true;
  std::string text;
  const auto json = ReadFileLimited(PendingPath(dir), 64 * 1024, text) ? importer::ParseJson(text) : std::nullopt;
  const auto device = json && json->IsObject() ? json->Str("device") : std::string();
  const auto folder = json && json->IsObject() ? json->Str("folder") : std::string();
  if (!ValidDeviceId(device) || folder.empty()) { fs::remove(PendingPath(dir), error); return true; }  // Unusable: drop it.
  const auto sync_dir = SyncDirectory(fs::path(std::u8string(folder.begin(), folder.end())));
  if (!fs::is_directory(sync_dir / "devices", error)) return false;  // Still away: try again later.
  if (!RemoveDeviceFile(sync_dir, device)) return false;
  fs::remove(PendingPath(dir), error);
  return !error;
}

bool IsSetUp(const fs::path& dir) {
  std::error_code error;
  return fs::exists(ConfigPath(dir), error);
}

LoadStatus Load(const fs::path& dir, LocalSync& out, OpenedVault& vault) {
  vault.Close();
  if (!IsSetUp(dir)) return LoadStatus::NotSetUp;
  std::string text, blob, key;
  if (!ReadFileLimited(ConfigPath(dir), kMaxConfig, text)) return LoadStatus::Corrupt;
  const auto json = importer::ParseJson(text);
  LocalSync local;
  local.dir = dir;
  if (!json || json->Int("format") != 1) return LoadStatus::Corrupt;
  local.device = json->Str("device");
  const auto folder = json->Str("folder");
  local.folder = fs::path(std::u8string(folder.begin(), folder.end()));
  local.join = json->Str("join");
  if (!ValidDeviceId(local.device) || folder.empty() || !Base64Decode(json->Str("vault"), local.vault) || local.vault.size() != 16 ||
      (local.join != "" && local.join != "merge" && local.join != "synced")) return LoadStatus::Corrupt;
  if (!ReadFileLimited(KeyPath(dir), 4096, blob) || !platform::UnprotectLocalSecret(blob, key) || key.size() != kKeySize) {
    Wipe(key);
    return LoadStatus::KeyUnavailable;
  }
  vault.id = local.vault;
  vault.key = std::move(key);
  out = std::move(local);
  return LoadStatus::Ok;
}

PassStatus Prepare(const LocalSync& local, const OpenedVault& vault, const Values& local_values, int64_t now_ms,
                   bool (*is_tree)(std::string_view), Prepared& out) {
  out = Prepared{};
  std::error_code error;
  const auto sync_dir = local.SyncDir();
  if (!fs::is_directory(sync_dir, error)) return PassStatus::FolderMissing;
  if (PeekVaultId(sync_dir) != vault.id || vault.id != local.vault) return PassStatus::VaultChanged;
  if (!LoadBase(local, vault, out.base)) return PassStatus::Io;

  State own, others;
  for (auto& file : ReadDeviceFiles(sync_dir, vault)) {
    if (file.device == local.device) {
      if (file.ok) own = std::move(file.state);
      continue;
    }
    ++out.devices;
    if (!file.ok) { ++out.unreadable; continue; }
    Merge(others, file.state);
    WipeState(file.state);
  }

  // Joining with "use the synced Spaces": this PC's tree part is not
  // offered, so the other PCs' tree replaces it. Without any, merge.
  out.use_synced_tree = local.join == "synced" && is_tree && HasLiveTree(others, is_tree);
  // Joining with "merge" adds only what the vault does not know: a PC that
  // rejoins after a while must not override newer edits or revive deletions
  // made elsewhere in the meantime.
  const bool merge_join = local.join == "merge";
  Values offered;
  for (const auto& [key, value] : local_values) {
    if (out.use_synced_tree && is_tree && is_tree(key)) continue;
    if (merge_join && others.contains(key)) continue;
    offered.emplace(key, value);
  }

  // A newer Sol's kinds of records: this PC cannot produce them, so their
  // absence from |local_values| is not a deletion. Carry the base's.
  for (const auto& [key, record] : out.base)
    if (!record.deleted && !IsKnownRecordKey(key) && !offered.contains(key)) offered.emplace(key, record.value);

  Clock clock(local.device);
  clock.Observe(own);
  clock.Observe(others);
  out.merged = out.base;
  StampLocalChanges(out.merged, offered, clock, now_ms);
  WipeValues(offered);
  Merge(out.merged, own);
  Merge(out.merged, others);
  out.own_file_current = !own.empty() && own == out.merged;
  WipeState(own);
  WipeState(others);
  out.changes = Diff(out.merged, local_values);
  out.live = Live(out.merged);
  return PassStatus::Ok;
}

bool IsKnownRecordKey(std::string_view key) {
  for (const std::string_view kind : {"profile/", "space/", "node/", "split/", "password/", "icon/"})
    if (key.starts_with(kind)) return true;
  return false;
}

bool ReadBaseValues(const LocalSync& local, const OpenedVault& vault, Values& out) {
  WipeValues(out);
  State base;
  if (!LoadBase(local, vault, base)) return false;
  out = Live(base);
  WipeState(base);
  return true;
}

PassStatus Commit(LocalSync& local, const OpenedVault& vault, Prepared& prepared, const std::set<std::string>& failed, bool* wrote) {
  if (wrote) *wrote = false;
  State next = prepared.merged;
  for (const auto& key : failed) {
    if (const auto old = prepared.base.find(key); old != prepared.base.end()) next[key] = old->second;
    else next.erase(key);
  }
  const auto sync_dir = local.SyncDir();
  if (PeekVaultId(sync_dir) != vault.id) { WipeState(next); return PassStatus::VaultChanged; }
  if (!prepared.own_file_current) {
    if (!WriteDeviceFile(sync_dir, vault, local.device, prepared.merged)) { WipeState(next); return PassStatus::Io; }
    if (wrote) *wrote = true;
  }
  std::string envelope;
  const bool saved = SealState(vault, local.device, next, envelope) && platform::AtomicWrite(BasePath(local.dir), envelope);
  WipeState(next);
  if (!saved) return PassStatus::Io;
  if (!local.join.empty()) {
    local.join.clear();
    if (!SaveConfig(local)) return PassStatus::Io;
  }
  return PassStatus::Ok;
}
}  // namespace red::sync
