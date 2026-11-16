// Windows implementation of sync_platform.h.
#include "sync/sync_platform.h"
#include <windows.h>
#include <tlhelp32.h>
#include <bcrypt.h>
#include <dpapi.h>
#include <wincrypt.h>
#include <fstream>
#include <system_error>

namespace red::sync::platform {
namespace fs = std::filesystem;
namespace {
constexpr std::string_view kEntropy = "Sol cloud sync vault key v1";
PBYTE Bytes(std::string_view s) { return reinterpret_cast<PBYTE>(const_cast<char*>(s.data())); }

bool WriteAndFlush(HANDLE file, std::string_view data) {
  DWORD written = 0;
  return WriteFile(file, data.data(), static_cast<DWORD>(data.size()), &written, nullptr) && written == data.size() && FlushFileBuffers(file);
}
std::wstring RegistryString(HKEY root, const std::wstring& subkey, const wchar_t* name) {
  wchar_t buffer[MAX_PATH * 2]{};
  DWORD size = sizeof(buffer);
  if (RegGetValueW(root, subkey.c_str(), name, RRF_RT_REG_SZ, nullptr, buffer, &size) != ERROR_SUCCESS) return L"";
  return buffer;
}
std::wstring Environment(const wchar_t* name) {
  wchar_t buffer[MAX_PATH * 2]{};
  const DWORD size = GetEnvironmentVariableW(name, buffer, static_cast<DWORD>(std::size(buffer)));
  return size && size < std::size(buffer) ? std::wstring(buffer, size) : L"";
}
bool ReadSmall(const fs::path& path, std::string& out) {
  std::error_code error;
  const auto size = fs::file_size(path, error);
  if (error || size > 64 * 1024) return false;
  std::ifstream stream(path, std::ios::binary);
  out.resize(static_cast<size_t>(size));
  return stream && (!size || stream.read(out.data(), static_cast<std::streamsize>(size)));
}
}  // namespace

bool RandomBytes(void* out, size_t count) {
  return !count || BCryptGenRandom(nullptr, static_cast<PUCHAR>(out), static_cast<ULONG>(count), BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0;
}

bool ProtectLocalSecret(std::string_view plain, std::string& out) {
  out.clear();
  DATA_BLOB in{static_cast<DWORD>(plain.size()), Bytes(plain)}, entropy{static_cast<DWORD>(kEntropy.size()), Bytes(kEntropy)}, result{};
  if (!CryptProtectData(&in, L"Sol sync", &entropy, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &result)) return false;
  out.assign(reinterpret_cast<const char*>(result.pbData), result.cbData);
  LocalFree(result.pbData);
  return true;
}

bool UnprotectLocalSecret(std::string_view blob, std::string& out) {
  out.clear();
  DATA_BLOB in{static_cast<DWORD>(blob.size()), Bytes(blob)}, entropy{static_cast<DWORD>(kEntropy.size()), Bytes(kEntropy)}, result{};
  if (blob.empty() || !CryptUnprotectData(&in, nullptr, &entropy, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &result)) return false;
  out.assign(reinterpret_cast<const char*>(result.pbData), result.cbData);
  SecureZeroMemory(result.pbData, result.cbData);
  LocalFree(result.pbData);
  return true;
}

bool AtomicWrite(const fs::path& path, std::string_view data) {
  auto temporary = path;
  temporary += L".tmp";
  const HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) return false;
  const bool ok = WriteAndFlush(file, data);
  CloseHandle(file);
  if (ok && MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) return true;
  std::error_code ignored;
  fs::remove(temporary, ignored);
  return false;
}

bool CreateNewFile(const fs::path& path, std::string_view data, bool& exists) {
  exists = false;
  const HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) { exists = GetLastError() == ERROR_FILE_EXISTS; return false; }
  const bool ok = WriteAndFlush(file, data);
  CloseHandle(file);
  if (!ok) { std::error_code ignored; fs::remove(path, ignored); }
  return ok;
}

std::optional<std::vector<std::wstring>> RunningProcessNames() {
  const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snapshot == INVALID_HANDLE_VALUE) return std::nullopt;
  std::vector<std::wstring> names;
  PROCESSENTRY32W entry{};
  entry.dwSize = sizeof(entry);
  for (BOOL more = Process32FirstW(snapshot, &entry); more; more = Process32NextW(snapshot, &entry)) {
    std::wstring name(entry.szExeFile);
    CharLowerBuffW(name.data(), static_cast<DWORD>(name.size()));
    names.push_back(std::move(name));
  }
  const bool complete = GetLastError() == ERROR_NO_MORE_FILES;
  CloseHandle(snapshot);
  if (!complete) return std::nullopt;
  return names;
}

DetectionInputs GatherDetectionInputs() {
  DetectionInputs inputs;
  HKEY accounts = nullptr;
  if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\OneDrive\\Accounts", 0, KEY_READ, &accounts) == ERROR_SUCCESS) {
    wchar_t name[256];
    for (DWORD index = 0;; ++index) {
      DWORD length = static_cast<DWORD>(std::size(name));
      if (RegEnumKeyExW(accounts, index, name, &length, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS) break;
      if (auto folder = RegistryString(accounts, name, L"UserFolder"); !folder.empty()) inputs.onedrive.emplace_back(folder);
    }
    RegCloseKey(accounts);
  }
  if (auto folder = Environment(L"OneDrive"); !folder.empty()) inputs.onedrive.emplace_back(folder);
  for (const wchar_t* base : {L"LOCALAPPDATA", L"APPDATA"}) {
    if (auto root = Environment(base); !root.empty()) {
      std::string text;
      if (ReadSmall(fs::path(root) / L"Dropbox" / L"info.json", text)) inputs.dropbox_info.push_back(std::move(text));
    }
  }
  {
    std::string blob(4096, '\0');
    DWORD size = static_cast<DWORD>(blob.size());
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Google\\DriveFS\\Share", L"SyncTargets", RRF_RT_REG_BINARY, nullptr, blob.data(), &size) == ERROR_SUCCESS) {
      blob.resize(size);
      inputs.google_drive_sync_targets.push_back(std::move(blob));
    }
  }
  // Drive for desktop streams files to a volume labelled "Google Drive" (its
  // My Drive folder is localized); mirror mode uses a local "My Drive" folder.
  const DWORD drives = GetLogicalDrives();
  for (int letter = 0; letter < 26; ++letter) {
    if (!(drives & (1u << letter))) continue;
    const std::wstring root{static_cast<wchar_t>(L'A' + letter), L':', L'\\'};
    // Drive for desktop mounts a local (fixed) volume; network drives are skipped so a
    // disconnected share never stalls Settings.
    if (GetDriveTypeW(root.c_str()) != DRIVE_FIXED) continue;
    wchar_t label[MAX_PATH + 1]{};
    if (GetVolumeInformationW(root.c_str(), label, static_cast<DWORD>(std::size(label)), nullptr, nullptr, nullptr, nullptr, 0) &&
        std::wstring_view(label) == L"Google Drive") inputs.google_drive_volumes.emplace_back(root);
  }
  if (auto profile = Environment(L"USERPROFILE"); !profile.empty()) inputs.google_drive.push_back(fs::path(profile) / L"My Drive");
  return inputs;
}
}  // namespace red::sync::platform
