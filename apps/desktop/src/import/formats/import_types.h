#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace red::import {
// Overwrites a secret's bytes before the buffer is released. Best effort:
// earlier reallocations of the same std::string are not reachable.
void Wipe(std::string& secret);

// A saved sign-in read from another browser. Lives in memory only: never
// logged, sent to React, written to disk or included in reports.
struct Login {
  std::string url, username, password;
  Login() = default;
  Login(std::string u, std::string n, std::string p) : url(std::move(u)), username(std::move(n)), password(std::move(p)) {}
  Login(const Login&) = default;
  Login(Login&&) noexcept = default;
  Login& operator=(const Login&) = default;
  Login& operator=(Login&&) noexcept = default;
  ~Login() { Wipe(password); }
};
void WipeAll(std::vector<Login>& logins);

struct Bookmark {
  std::string id, parent, title, url;  // Source-local ids; empty parent = root.
  bool folder = false;
  bool bar = false;  // Chromium's bookmarks bar / Arc pinned roots.
};
struct OpenTab {
  std::string url, title;
  bool pinned = false;
  int window = 0;
};
struct HistoryVisit {
  std::string url, title;
  int64_t visited_ms = 0;  // Unix epoch milliseconds.
};
struct PasswordCounts {
  int found = 0;        // Rows/entries in the source store.
  int decrypted = 0;    // Usable website sign-ins.
  int app_bound = 0;    // Chrome/Edge app-bound (v20) values a third party cannot decrypt.
  int unreadable = 0;   // Damaged, foreign-key or unknown-format values.
  int skipped = 0;      // Never-save entries, non-web origins, empty passwords.
};
// http(s) only, no control characters or embedded credentials, <= 4096 bytes.
bool IsWebUrl(const std::string& url);
// Trims to max bytes on a UTF-8 boundary and removes control characters.
std::string CleanLabel(std::string text, size_t max, const char* fallback);
}  // namespace red::import
