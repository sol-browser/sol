#include "sync/sync_folder.h"
#include "import/formats/json_lite.h"
#include <algorithm>
#include <cwctype>
#include <fstream>
#include <system_error>

namespace red::sync {
namespace fs = std::filesystem;
namespace {
constexpr size_t kMaxVaultFile = 16 * 1024, kMaxDeviceFile = 128u * 1024 * 1024, kMaxDevices = 64;
// The only compression a format-1 reader accepts; part of the authenticated data.
constexpr std::string_view kCompression = "zstd";

std::string VaultAad(std::string_view id) { return "sol-sync-vault-v1|" + Hex(id); }
std::string DeviceAad(std::string_view vault, std::string_view device) {
  return "sol-sync-device-v1|" + Hex(vault) + "|" + std::string(device) + "|" + std::string(kCompression);
}
fs::path FromUtf8(std::string_view text) { return fs::path(std::u8string(text.begin(), text.end())); }

std::string VaultJson(const std::string& id, const KdfParams& params, const std::string& salt, const std::string& wrapped) {
  return "{\"format\":1,\"vault\":\"" + Base64Encode(id) + "\",\"kdf\":{\"alg\":\"argon2id\",\"m\":" + std::to_string(params.memory_kib) +
         ",\"t\":" + std::to_string(params.iterations) + ",\"p\":" + std::to_string(params.lanes) + "},\"cipher\":\"xchacha20-poly1305\",\"salt\":\"" +
         Base64Encode(salt) + "\",\"key\":\"" + Base64Encode(wrapped) + "\"}\n";
}

struct VaultFile { std::string id, salt, wrapped; KdfParams params; };
VaultStatus ReadVault(const fs::path& sync_dir, VaultFile& out) {
  std::error_code error;
  const auto path = sync_dir / "vault.json";
  if (!fs::exists(path, error)) return error ? VaultStatus::Io : VaultStatus::Missing;
  std::string text;
  if (!ReadFileLimited(path, kMaxVaultFile, text)) return VaultStatus::Io;
  const auto json = importer::ParseJson(text);
  const auto* kdf = json ? json->Get("kdf") : nullptr;
  if (!json || json->Int("format") != 1 || !kdf || kdf->Str("alg") != "argon2id" || json->Str("cipher") != "xchacha20-poly1305") return VaultStatus::Corrupt;
  out.params = KdfParams{static_cast<uint32_t>(std::clamp<int64_t>(kdf->Int("m"), 0, UINT32_MAX)),
                         static_cast<uint32_t>(std::clamp<int64_t>(kdf->Int("t"), 0, UINT32_MAX)),
                         static_cast<uint32_t>(std::clamp<int64_t>(kdf->Int("p"), 0, UINT32_MAX))};
  if (!out.params.Acceptable() || !Base64Decode(json->Str("vault"), out.id) || out.id.size() != 16 ||
      !Base64Decode(json->Str("salt"), out.salt) || out.salt.size() != 16 || !Base64Decode(json->Str("key"), out.wrapped) ||
      out.wrapped.size() != kNonceSize + kKeySize + kTagSize) return VaultStatus::Corrupt;
  return VaultStatus::Ok;
}

VaultStatus WriteVault(const fs::path& sync_dir, const std::string& id, std::string_view key, std::string_view passphrase, const KdfParams& params, bool create) {
  if (passphrase.size() < kMinPassphrase) return VaultStatus::WeakPassphrase;
  std::string salt, kek, wrapped;
  if (!RandomBytes(16, salt) || !Argon2id(passphrase, salt, params, kKeySize, kek)) return VaultStatus::Io;
  const bool sealed = Seal(kek, key, VaultAad(id), wrapped);
  Wipe(kek);
  if (!sealed) return VaultStatus::Io;
  const auto text = VaultJson(id, params, salt, wrapped);
  const auto path = sync_dir / "vault.json";
  if (!create) return platform::AtomicWrite(path, text) ? VaultStatus::Ok : VaultStatus::Io;
  // Never replace a vault that appeared meanwhile.
  bool exists = false;
  if (platform::CreateNewFile(path, text, exists)) return VaultStatus::Ok;
  return exists ? VaultStatus::Exists : VaultStatus::Io;
}

void AddExisting(std::vector<CloudFolder>& out, Provider provider, const fs::path& path) {
  std::error_code error;
  if (path.empty() || !path.is_absolute() || !fs::is_directory(path, error)) return;
  const auto canonical = fs::weakly_canonical(path, error);
  const auto& chosen = error ? path : canonical;
  for (const auto& existing : out) {
    std::error_code same_error;
    if (fs::equivalent(existing.path, chosen, same_error)) return;
  }
  out.push_back({provider, chosen});
}
}  // namespace

fs::path SyncDirectory(const fs::path& cloud_folder) { return cloud_folder / "Sol Sync"; }

VaultStatus CreateVault(const fs::path& sync_dir, std::string_view passphrase, OpenedVault& out, const KdfParams& params) {
  out.Close();
  if (!params.Acceptable()) return VaultStatus::Io;
  if (passphrase.size() < kMinPassphrase) return VaultStatus::WeakPassphrase;
  std::error_code error;
  fs::create_directories(sync_dir / "devices", error);
  if (error) return VaultStatus::Io;
  OpenedVault vault;
  if (!RandomBytes(16, vault.id) || !RandomBytes(kKeySize, vault.key)) return VaultStatus::Io;
  const auto status = WriteVault(sync_dir, vault.id, vault.key, passphrase, params, true);
  if (status != VaultStatus::Ok) { vault.Close(); return status; }
  out = std::move(vault);
  return VaultStatus::Ok;
}

VaultStatus OpenVault(const fs::path& sync_dir, std::string_view passphrase, OpenedVault& out) {
  out.Close();
  VaultFile file;
  if (const auto status = ReadVault(sync_dir, file); status != VaultStatus::Ok) return status;
  std::string kek, key;
  if (!Argon2id(passphrase, file.salt, file.params, kKeySize, kek)) return VaultStatus::Io;
  const bool opened = Open(kek, file.wrapped, VaultAad(file.id), key);
  Wipe(kek);
  if (!opened || key.size() != kKeySize) { Wipe(key); return VaultStatus::WrongPassphrase; }
  out.id = file.id;
  out.key = std::move(key);
  return VaultStatus::Ok;
}

VaultStatus ChangePassphrase(const fs::path& sync_dir, const OpenedVault& vault, std::string_view passphrase, const KdfParams& params) {
  if (!params.Acceptable() || vault.key.size() != kKeySize) return VaultStatus::Io;
  if (PeekVaultId(sync_dir) != vault.id) return VaultStatus::Corrupt;
  return WriteVault(sync_dir, vault.id, vault.key, passphrase, params, false);
}

std::optional<std::string> PeekVaultId(const fs::path& sync_dir) {
  VaultFile file;
  if (ReadVault(sync_dir, file) != VaultStatus::Ok) return std::nullopt;
  return file.id;
}

bool SealState(const OpenedVault& vault, std::string_view device, const State& state, std::string& out) {
  out.clear();
  if (!ValidDeviceId(device) || vault.key.size() != kKeySize) return false;
  std::string plain = Serialize(state), packed, sealed;
  const bool ok = Compress(plain, packed) && Seal(vault.key, packed, DeviceAad(vault.id, device), sealed);
  Wipe(plain);
  Wipe(packed);
  if (!ok) return false;
  out = "{\"format\":1,\"vault\":\"" + Base64Encode(vault.id) + "\",\"device\":\"" + std::string(device) + "\",\"compression\":\"" +
        std::string(kCompression) + "\",\"data\":\"" + Base64Encode(sealed) + "\"}\n";
  return true;
}

bool OpenState(const OpenedVault& vault, std::string_view device, std::string_view envelope, State& out) {
  out.clear();
  const auto json = importer::ParseJson(envelope);
  std::string id, sealed, packed, plain;
  if (!json || json->Int("format") != 1 || json->Str("device") != device || !ValidDeviceId(device) || json->Str("compression") != kCompression ||
      !Base64Decode(json->Str("vault"), id) || id != vault.id || !Base64Decode(json->Str("data"), sealed)) return false;
  // Authenticate before decompressing: zstd only ever sees data this vault wrote.
  if (!Open(vault.key, sealed, DeviceAad(vault.id, device), packed)) return false;
  const bool parsed = Decompress(packed, plain) && Parse(plain, out);
  Wipe(packed);
  Wipe(plain);
  return parsed;
}

bool WriteDeviceFile(const fs::path& sync_dir, const OpenedVault& vault, std::string_view device, const State& state) {
  std::string envelope;
  if (!SealState(vault, device, state, envelope)) return false;
  std::error_code error;
  fs::create_directories(sync_dir / "devices", error);
  return !error && platform::AtomicWrite(sync_dir / "devices" / FromUtf8(std::string(device) + ".sol"), envelope);
}

bool RemoveDeviceFile(const fs::path& sync_dir, std::string_view device) {
  if (!ValidDeviceId(device)) return false;
  std::error_code error;
  fs::remove(sync_dir / "devices" / FromUtf8(std::string(device) + ".sol"), error);
  return !error;
}

std::vector<DeviceFile> ReadDeviceFiles(const fs::path& sync_dir, const OpenedVault& vault) {
  std::vector<DeviceFile> files;
  std::error_code error;
  for (fs::directory_iterator it(sync_dir / "devices", error), end; !error && it != end; it.increment(error)) {
    const auto& path = it->path();
    if (path.extension() != ".sol" || !it->is_regular_file(error)) continue;
    const auto stem = path.stem().u8string();
    DeviceFile file{std::string(stem.begin(), stem.end())};
    if (!ValidDeviceId(file.device)) continue;  // Cloud conflict copies ("<id> (2).sol") and stray files.
    if (files.size() == kMaxDevices) break;
    std::string envelope;
    file.ok = ReadFileLimited(path, kMaxDeviceFile, envelope) && OpenState(vault, file.device, envelope, file.state);
    files.push_back(std::move(file));
  }
  std::sort(files.begin(), files.end(), [](const DeviceFile& a, const DeviceFile& b) { return a.device < b.device; });
  return files;
}

bool ReadFileLimited(const fs::path& path, size_t limit, std::string& out) {
  out.clear();
  std::error_code error;
  const auto size = fs::file_size(path, error);
  if (error || size > limit) return false;
  std::ifstream stream(path, std::ios::binary);
  if (!stream) return false;
  out.resize(static_cast<size_t>(size));
  if (size && !stream.read(out.data(), static_cast<std::streamsize>(size))) { out.clear(); return false; }
  return true;
}

std::string NewDeviceId() {
  std::string bytes;
  return RandomBytes(16, bytes) ? Hex(bytes) : "";
}

namespace {
// A Drive for desktop volume holds My Drive under the account's language
// ("My Drive", "Мій диск", "Мой диск"), next to hidden folders (".Encrypted",
// ".shortcut-targets-by-id", "$RECYCLE.BIN") and, depending on the account,
// "Shared drives" and "Other computers" (backups of other PCs). A known My
// Drive name wins; else the one visible folder that is neither of the others.
// Two unknown candidates are not guessed: the person chooses the folder.
fs::path GoogleDriveMyDrive(const fs::path& volume) {
  // My Drive in English, Ukrainian, Russian, German, Spanish, French, Italian,
  // Portuguese and Polish.
  constexpr std::wstring_view kMyDrive[] = {L"My Drive", L"Мій диск", L"Мой диск",
                                            L"Meine Ablage", L"Mi unidad", L"Mon Drive", L"Il mio Drive", L"Meu Drive",
                                            L"Mój dysk"};
  // Shared drives and Other computers in English, Ukrainian and Russian.
  constexpr std::wstring_view kNotMyDrive[] = {
      L"Shared drives", L"Спільні диски", L"Общие диски",
      L"Other computers", L"Інші комп'ютери",
      L"Другие компьютеры"};
  std::error_code error;
  for (const auto name : kMyDrive)
    if (fs::is_directory(volume / name, error)) return volume / name;
  fs::path found;
  for (fs::directory_iterator it(volume, fs::directory_options::skip_permission_denied, error), end; !error && it != end; it.increment(error)) {
    const auto name = it->path().filename().wstring();
    std::error_code type_error;
    if (name.empty() || name.front() == L'.' || name.front() == L'$' || name == L"System Volume Information" ||
        std::find(std::begin(kNotMyDrive), std::end(kNotMyDrive), name) != std::end(kNotMyDrive) || !it->is_directory(type_error)) continue;
    if (!found.empty()) return {};  // Two candidates: let the person choose the folder.
    found = it->path();
  }
  return error ? fs::path() : found;
}
}  // namespace

namespace {
// One protobuf varint; false at the end of |data| or past 64 bits.
bool ReadVarint(std::string_view data, size_t& at, uint64_t& value) {
  value = 0;
  for (int shift = 0; shift < 64 && at < data.size(); shift += 7) {
    const auto byte = static_cast<unsigned char>(data[at++]);
    value |= static_cast<uint64_t>(byte & 0x7f) << shift;
    if (!(byte & 0x80)) return true;
  }
  return false;
}
// Calls |field| for each length-delimited field of |message|; stops at the
// first malformed or non-length-delimited field.
template <typename Field> void ForEachBytesField(std::string_view message, Field field) {
  size_t at = 0;
  while (at < message.size()) {
    uint64_t key = 0, length = 0;
    if (!ReadVarint(message, at, key) || (key & 7) != 2 || !ReadVarint(message, at, length) || length > message.size() - at) return;
    field(static_cast<uint32_t>(key >> 3), message.substr(at, static_cast<size_t>(length)));
    at += static_cast<size_t>(length);
  }
}
}  // namespace

std::vector<std::string> GoogleDriveMountPoints(std::string_view sync_targets) {
  std::vector<std::string> mounts;
  ForEachBytesField(sync_targets, [&](uint32_t number, std::string_view account) {
    if (number != 1) return;
    ForEachBytesField(account, [&](uint32_t inner, std::string_view value) {
      if (inner == 2 && !value.empty() && value.size() < 1024) mounts.emplace_back(value);
    });
  });
  return mounts;
}

std::vector<CloudFolder> DetectCloudFolders(const DetectionInputs& inputs) {
  std::vector<CloudFolder> out;
  for (const auto& path : inputs.onedrive) AddExisting(out, Provider::OneDrive, path);
  for (const auto& text : inputs.dropbox_info) {
    const auto json = importer::ParseJson(text);
    if (!json || !json->IsObject()) continue;
    for (const char* account : {"personal", "business"})
      if (const auto* entry = json->Get(account); entry && entry->IsObject()) AddExisting(out, Provider::Dropbox, FromUtf8(entry->Str("path")));
  }
  for (const auto& blob : inputs.google_drive_sync_targets)
    for (const auto& mount : GoogleDriveMountPoints(blob)) {
      // "G:" names a drive; anything else is a folder mount.
      const auto root = mount.size() == 2 && mount[1] == ':' ? FromUtf8(mount + "\\") : FromUtf8(mount);
      std::error_code error;
      if (root.is_absolute() && !fs::exists(root, error)) {
        // Streamed drive gone: Drive for desktop is not running.
        if (std::none_of(out.begin(), out.end(), [&](const CloudFolder& f) { return f.path == root; }))
          out.push_back({Provider::GoogleDrive, root, false});
        continue;
      }
      AddExisting(out, Provider::GoogleDrive, GoogleDriveMyDrive(root));
    }
  for (const auto& volume : inputs.google_drive_volumes) AddExisting(out, Provider::GoogleDrive, GoogleDriveMyDrive(volume));
  for (const auto& path : inputs.google_drive) AddExisting(out, Provider::GoogleDrive, path);
  return out;
}

namespace {
// |folder| is |root| or inside it, by name only (|root| may not exist now).
bool LexicallyWithin(const fs::path& folder, const fs::path& root) {
  auto lower = [](const fs::path& path) {
    auto text = path.lexically_normal().wstring();
    for (auto& c : text) c = static_cast<wchar_t>(std::towlower(c));
    while (text.size() > 1 && (text.back() == L'\\' || text.back() == L'/')) text.pop_back();
    return text;
  };
  const auto f = lower(folder), r = lower(root);
  return !r.empty() && f.starts_with(r) && (f.size() == r.size() || f[r.size()] == L'\\' || f[r.size()] == L'/');
}
}  // namespace

std::optional<Provider> ProviderForFolder(const std::vector<CloudFolder>& detected, const fs::path& folder) {
  if (folder.empty()) return std::nullopt;
  for (const auto& candidate : detected)
    if (!candidate.available && LexicallyWithin(folder, candidate.path)) return candidate.provider;
  for (auto path = folder; !path.empty(); path = path.parent_path()) {
    for (const auto& candidate : detected) {
      std::error_code error;
      if (fs::equivalent(candidate.path, path, error)) return candidate.provider;
    }
    if (path == path.parent_path()) break;  // Drive root.
  }
  return std::nullopt;
}

bool ClientRunning(Provider provider, const std::vector<std::wstring>& running) {
  std::wstring_view client;
  switch (provider) {
    case Provider::OneDrive: client = L"onedrive.exe"; break;
    case Provider::Dropbox: client = L"dropbox.exe"; break;
    case Provider::GoogleDrive: client = L"googledrivefs.exe"; break;
  }
  return std::find(running.begin(), running.end(), client) != running.end();
}

const char* ProviderName(Provider provider) {
  switch (provider) {
    case Provider::OneDrive: return "onedrive";
    case Provider::Dropbox: return "dropbox";
    case Provider::GoogleDrive: return "google-drive";
  }
  return "";
}
}  // namespace red::sync
