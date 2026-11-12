#pragma once
#include "import/formats/import_types.h"
#include <filesystem>
#include <map>
#include <string_view>

namespace red::importer {
// Chromium-family profile formats (Chrome, Edge, Arc): public, documented-by-
// source formats only. Every reader opens files read-only and fails closed.

// Local State → os_crypt.encrypted_key ("DPAPI" + DPAPI blob) → 32-byte AES key.
// `app_bound` reports whether the browser also uses app-bound encryption.
bool ChromiumMasterKey(std::string_view local_state_json, std::string& key, bool& app_bound, std::string& error);
// Local State → profile.info_cache: profile folder ("Default", "Profile 1") →
// the name the browser shows for it. Only names are returned.
std::map<std::string, std::string> ChromiumProfileNames(std::string_view local_state_json);
enum class Decrypted { Ok, AppBound, Unreadable };
// "v10"/"v11": AES-256-GCM with the master key; "v20": app-bound (refused);
// otherwise a legacy raw DPAPI blob.
Decrypted DecryptChromiumValue(std::string_view key, std::string_view blob, std::string& out);

struct ChromiumLogins { std::vector<Login> logins; PasswordCounts counts; std::string error; };
ChromiumLogins ReadChromiumLogins(const std::filesystem::path& login_data, std::string_view key);

struct ChromiumBookmarks { std::vector<Bookmark> items; int urls = 0; std::string error; };
ChromiumBookmarks ParseChromiumBookmarks(std::string_view json);
// Netscape bookmark file (what Chromium/Arc bookmark managers export).
ChromiumBookmarks ParseBookmarksHtml(std::string_view html);

struct ChromiumHistory { std::vector<HistoryVisit> visits; std::string error; };
// Visits newer than `since_ms` (Unix ms), newest first, at most `limit`.
ChromiumHistory ReadChromiumHistory(const std::filesystem::path& history, int64_t since_ms, size_t limit);

struct ChromiumSession { std::vector<OpenTab> tabs; int skipped = 0; std::string error; };
// One SNSS command stream (Sessions/Session_* or legacy "Current Session").
ChromiumSession ParseSnss(std::string_view bytes);
// The newest readable session file of a profile directory.
ChromiumSession ReadChromiumSession(const std::filesystem::path& profile);

bool ReadSmallFile(const std::filesystem::path& path, size_t max, std::string& out);
}  // namespace red::importer
