#pragma once
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// The only operating-system code of cloud-folder sync (docs/sync.md,
// "Portability"). Everything else in src/sync is portable C++20 over
// Monocypher and zstd. sync_platform_win.cpp implements this for Windows; a
// macOS or Linux port adds its own file with the same functions.
namespace red::sync::platform {
// Cryptographically secure random bytes (Windows: BCryptGenRandom;
// POSIX: getrandom/arc4random).
bool RandomBytes(void* out, size_t count);
// Protects the vault key for this OS user on this PC (Windows: DPAPI;
// macOS: Keychain; Linux: Secret Service).
bool ProtectLocalSecret(std::string_view plain, std::string& out);
bool UnprotectLocalSecret(std::string_view blob, std::string& out);
// Writes a temporary file next to |path|, flushes it to disk and renames it
// over |path|.
bool AtomicWrite(const std::filesystem::path& path, std::string_view data);
// Creates |path| with |data|; false with |exists| set if it already exists.
bool CreateNewFile(const std::filesystem::path& path, std::string_view data, bool& exists);

// Lowercase executable names of this user's running processes ("onedrive.exe"),
// or nullopt if the list could not be read. Only names; used to tell whether a
// cloud app is running (ClientRunning in sync_folder.h).
std::optional<std::vector<std::wstring>> RunningProcessNames();

// Raw facts the cloud folder detector works from (filtered by
// DetectCloudFolders in sync_folder.cpp).
struct DetectionInputs {
  DetectionInputs() = default;
  DetectionInputs(const DetectionInputs&) = default;
  DetectionInputs(DetectionInputs&&) noexcept = default;
  DetectionInputs& operator=(const DetectionInputs&) = default;
  DetectionInputs& operator=(DetectionInputs&&) noexcept = default;
  ~DetectionInputs() = default;
  std::vector<std::filesystem::path> onedrive;      // Registry UserFolder values, then %OneDrive%.
  std::vector<std::string> dropbox_info;            // Contents of Dropbox info.json files.
  std::vector<std::filesystem::path> google_drive;  // Drive for desktop "My Drive" folders (mirror mode).
  // Roots of volumes labelled "Google Drive" (streaming mode). Their "My Drive"
  // folder has the account's language ("Мій диск"); DetectCloudFolders finds it.
  std::vector<std::filesystem::path> google_drive_volumes;
  // Drive for desktop's own record of where it mounts each account
  // (HKCU\Software\Google\DriveFS\Share SyncTargets, a protobuf blob);
  // parsed by GoogleDriveMountPoints. Works whatever the volume is labelled.
  std::vector<std::string> google_drive_sync_targets;
};
DetectionInputs GatherDetectionInputs();
}  // namespace red::sync::platform
