#pragma once
#include "sync/sync_crypto.h"
#include "sync/sync_model.h"
#include "sync/sync_platform.h"
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

// The encrypted "Sol Sync" folder inside a cloud client's folder
// (docs/sync.md, "Folder layout"). CEF-free.
namespace red::sync {
enum class VaultStatus { Ok, Missing, Exists, Corrupt, WrongPassphrase, WeakPassphrase, Io };
constexpr size_t kMinPassphrase = 10;

struct OpenedVault {
  std::string id;   // Raw 16 bytes.
  std::string key;  // Raw 32-byte vault key; wipe with Close().
  void Close() { Wipe(key); id.clear(); }
};

std::filesystem::path SyncDirectory(const std::filesystem::path& cloud_folder);
// Creates vault.json with a new random vault key. Refuses to replace an
// existing vault (VaultStatus::Exists).
VaultStatus CreateVault(const std::filesystem::path& sync_dir, std::string_view passphrase, OpenedVault& out, const KdfParams& params = {});
VaultStatus OpenVault(const std::filesystem::path& sync_dir, std::string_view passphrase, OpenedVault& out);
// Rewraps the same vault key under a new passphrase. Device files are unchanged.
VaultStatus ChangePassphrase(const std::filesystem::path& sync_dir, const OpenedVault& vault, std::string_view passphrase, const KdfParams& params = {});
// Vault id of an existing vault.json, without the passphrase.
std::optional<std::string> PeekVaultId(const std::filesystem::path& sync_dir);

// Encrypted state envelope, bound to vault and device (also used for the local base).
bool SealState(const OpenedVault& vault, std::string_view device, const State& state, std::string& out);
bool OpenState(const OpenedVault& vault, std::string_view device, std::string_view envelope, State& out);

bool WriteDeviceFile(const std::filesystem::path& sync_dir, const OpenedVault& vault, std::string_view device, const State& state);
bool RemoveDeviceFile(const std::filesystem::path& sync_dir, std::string_view device);
struct DeviceFile {
  DeviceFile() = default;
  DeviceFile(const DeviceFile&) = default;
  DeviceFile(DeviceFile&&) noexcept = default;
  DeviceFile& operator=(const DeviceFile&) = default;
  DeviceFile& operator=(DeviceFile&&) noexcept = default;
  ~DeviceFile() = default;
  explicit DeviceFile(std::string id) : device(std::move(id)) {}
  std::string device;
  bool ok = false;  // False: unreadable, wrong vault or failed authentication.
  State state;
};
std::vector<DeviceFile> ReadDeviceFiles(const std::filesystem::path& sync_dir, const OpenedVault& vault);

using platform::AtomicWrite;
bool ReadFileLimited(const std::filesystem::path& path, size_t limit, std::string& out);
std::string NewDeviceId();

// Cloud folders offered in Settings (docs/sync.md, "Cloud folder detection").
enum class Provider { OneDrive, Dropbox, GoogleDrive };
struct CloudFolder {
  Provider provider;
  std::filesystem::path path;
  // False for a Google Drive whose streamed drive is not mounted (its app is
  // not running): |path| is then the mount point, e.g. "G:\", and Settings
  // shows it as not running instead of leaving it out.
  bool available = true;
  bool operator==(const CloudFolder&) const = default;
};
using platform::DetectionInputs;
using platform::GatherDetectionInputs;
// Existing, de-duplicated folders in provider order, plus unmounted Google
// Drive mount points (available = false).
std::vector<CloudFolder> DetectCloudFolders(const DetectionInputs& inputs);
const char* ProviderName(Provider provider);
// Mount points ("G:" or a folder path, UTF-8) in Drive for desktop's
// SyncTargets value: repeated field 1 (one per account), each holding the
// mount point as field 2 (field 3 lists backed-up local folders, which only
// upload and are not used). Malformed input yields what parsed before it.
std::vector<std::string> GoogleDriveMountPoints(std::string_view sync_targets);
// The provider whose detected folder is |folder| or contains it (a chosen
// subfolder of OneDrive is still OneDrive); nullopt for any other folder.
std::optional<Provider> ProviderForFolder(const std::vector<CloudFolder>& detected, const std::filesystem::path& folder);
// Whether the provider's desktop app (OneDrive.exe, Dropbox.exe,
// GoogleDriveFS.exe) is among |running| (lowercase names from
// platform::RunningProcessNames). Without it the folder is not uploaded.
bool ClientRunning(Provider provider, const std::vector<std::wstring>& running);
}  // namespace red::sync
