#pragma once
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
namespace red {
// Pure rules for the extension catalog. Manifests and preferences are written
// by extensions and Chrome, so every value read from them is untrusted text.

// Chrome extension IDs: 32 letters a-p.
inline bool IsExtensionId(std::string_view id) {
  return id.size() == 32 && std::all_of(id.begin(), id.end(), [](char c) { return c >= 'a' && c <= 'p'; });
}
// Extensions the person installed: Web Store (1), external (2, 3, 6, 7),
// unpacked (4) and command line (8). Component extensions (5, 10) are part of
// the browser and have no user-facing UI here.
inline bool IsListedLocation(int location) {
  return location == 1 || location == 2 || location == 3 || location == 4 || location == 6 || location == 7 || location == 8;
}
// A path inside the extension package (popup, options page, icon). Returns
// nullopt for absolute paths, parent references, schemes or unusual characters;
// strips one leading slash ("/popup.html" is valid in manifests).
inline std::optional<std::string> ExtensionRelativePath(std::string_view path, bool allow_query = false) {
  if (path.starts_with('/')) path.remove_prefix(1);
  if (path.empty() || path.size() > 512) return std::nullopt;
  std::string_view file = path;
  if (allow_query) file = path.substr(0, std::min(path.find('?'), path.find('#')));
  if (file.empty()) return std::nullopt;
  for (const char c : path) {
    const bool plain = std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '_' || c == '-' || c == '/';
    const bool query = allow_query && (c == '?' || c == '#' || c == '=' || c == '&' || c == '%' || c == '+');
    if (!plain && !query) return std::nullopt;
  }
  size_t start = 0;
  while (start <= file.size()) {
    const size_t end = std::min(file.find('/', start), file.size());
    const auto part = file.substr(start, end - start);
    if (part.empty() || part == "." || part == "..") return std::nullopt;
    start = end + 1;
  }
  return std::string(path);
}
// "__MSG_extName__" -> "extName"; empty when |text| is not a message reference.
inline std::string MessageKey(std::string_view text) {
  if (text.size() <= 8 || !text.starts_with("__MSG_") || !text.ends_with("__")) return {};
  return std::string(text.substr(6, text.size() - 8));
}
// Locale folders to try for |user_locale| ("ru-RU"): ru_RU, ru, then the
// extension's default locale.
inline std::vector<std::string> LocaleCandidates(std::string_view user_locale, std::string_view default_locale) {
  std::vector<std::string> out;
  auto add = [&](std::string value) {
    if (!value.empty() && value.size() <= 16 && std::find(out.begin(), out.end(), value) == out.end() &&
        std::all_of(value.begin(), value.end(), [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }))
      out.push_back(std::move(value));
  };
  std::string full(user_locale); std::replace(full.begin(), full.end(), '-', '_');
  add(full); add(full.substr(0, full.find('_'))); add(std::string(default_locale));
  return out;
}
// Of the declared icon sizes, the smallest one at least |target| pixels, or
// the largest one when all are smaller. Zero when there are none.
inline int PickIconSize(const std::vector<int>& sizes, int target) {
  int best = 0;
  for (const int size : sizes) {
    if (size <= 0) continue;
    if (best == 0 || (size >= target && (best < target || size < best)) || (best < target && size > best)) best = size;
  }
  return best;
}
// The file stays inside |root| after normalization (no symlink resolution:
// the extension directory belongs to the profile).
inline bool InsideDirectory(const std::filesystem::path& root, const std::filesystem::path& file) {
  const auto base = root.lexically_normal(), target = file.lexically_normal();
  auto b = base.begin(), t = target.begin();
  for (; b != base.end() && !b->empty(); ++b, ++t) if (t == target.end() || *b != *t) return false;
  return t != target.end();
}
}  // namespace red
