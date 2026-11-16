#pragma once
#include "sync/sync_model.h"
#include <filesystem>
#include <string>
#include <string_view>

// Saved passwords as sync records (docs/sync.md, "Passwords").
//   key   password/<browsing profile>/<signon realm>\n<username>
//   value {"url":<origin url>,"password":<password>}
namespace red::sync {
std::string PasswordKey(std::string_view profile, std::string_view realm, std::string_view username);
bool ParsePasswordKey(std::string_view key, std::string& profile, std::string& realm, std::string& username);
std::string PasswordValue(std::string_view url, std::string_view password);
bool ParsePasswordValue(std::string_view value, std::string& url, std::string& password);
bool IsPasswordKey(std::string_view key);

// Reads one browsing profile's saved sign-ins from Chromium's store while
// Sol runs. Chromium keeps "Login Data" locked, so the file's bytes are copied
// into memory and opened there; a copy taken during a write (non-empty
// journal, or the change counter moved while reading) is refused as Busy and
// retried by a later pass. Windows: the os_crypt key is unwrapped with DPAPI
// (sync_passwords_win.cpp); other systems need their own reader.
enum class PasswordReadStatus { Ok, NoStore, Busy, KeyUnavailable, Damaged };
struct PasswordRead {
  PasswordRead() = default;
  PasswordRead(const PasswordRead&) = delete;
  PasswordRead(PasswordRead&&) noexcept = default;
  PasswordRead& operator=(const PasswordRead&) = delete;
  PasswordRead& operator=(PasswordRead&&) noexcept = default;
  ~PasswordRead() { WipeValues(values); }
  PasswordReadStatus status = PasswordReadStatus::Ok;
  Values values;     // Keys of |profile| only.
  int skipped = 0;   // Never-save entries, non-HTML sign-ins, app-bound or unreadable values.
};
// |root| is Sol's profile root (Local State lives there); |profile| is
// "default" (folder Default) or a browsing profile id (folder Profile-<id>).
PasswordRead ReadPasswords(const std::filesystem::path& root, std::string_view profile);
std::filesystem::path BrowsingProfileDirectory(const std::filesystem::path& root, std::string_view profile);
}  // namespace red::sync
