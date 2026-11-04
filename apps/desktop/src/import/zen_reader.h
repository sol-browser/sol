#pragma once
#include "storage/tree_store.h"
#include "storage/history_store.h"
#include "include/cef_cookie.h"
#include <filesystem>

namespace red {
struct ZenProfile { std::filesystem::path path; std::wstring name; };
struct ZenCookie { std::string url; CefCookie cookie; };
struct ZenImport {
  TreeSnapshot tree;
  std::vector<ZenCookie> cookies;
  std::vector<Visit> history;
  std::string error;
  int tabs = 0, folders = 0, bookmarks = 0, skipped_tabs = 0, skipped_cookies = 0;
  bool used_backup = false;
};
std::vector<ZenProfile> DiscoverZenProfiles();
// Source files are untrusted and read-only. Never log URLs, cookie values or forms.
ZenImport ReadZenProfile(const std::filesystem::path& profile);
}
