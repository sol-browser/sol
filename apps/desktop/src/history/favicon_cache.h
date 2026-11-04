#pragma once
#include <map>
#include <mutex>
#include <optional>
#include <string>
namespace red {
// Site icons (PNG) by host, shared between the UI thread (snapshots, new
// icons) and the IO thread (sol://ui/favicon/<host>/<version> requests from
// trusted UI documents). Persisted by HistoryService.
class FaviconCache {
 public:
  // Returns the version for |host|: a new one only when the icon changed, so
  // a page re-downloading the same icon keeps its sol://ui/favicon/ URL (the
  // UI's image and tile tint stay put instead of reloading).
  int Put(const std::string& host, std::string png) {
    std::lock_guard lock(mutex_);
    auto& entry = icons_[host];
    if (entry.version && entry.png == png) return entry.version;
    entry.png = std::move(png);
    return ++entry.version;
  }
  // sol://ui/favicon/<host>/<version> for a cached host, else nothing.
  std::optional<std::string> Url(const std::string& host) const {
    std::lock_guard lock(mutex_);
    const auto found = icons_.find(host);
    if (found == icons_.end()) return std::nullopt;
    return "sol://ui/favicon/" + host + "/" + std::to_string(found->second.version);
  }
  std::optional<std::string> Png(const std::string& host) const {
    std::lock_guard lock(mutex_);
    const auto found = icons_.find(host);
    if (found == icons_.end()) return std::nullopt;
    return found->second.png;
  }
 private:
  struct Entry { std::string png; int version = 0; };
  mutable std::mutex mutex_;
  std::map<std::string, Entry> icons_;
};
// Lower-case DNS host as used for cache keys; empty for anything else.
inline std::string FaviconHost(const std::string& host) {
  if (host.empty() || host.size() > 253) return "";
  std::string out;
  for (const char c : host) {
    const char lower = (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
    if (!((lower >= 'a' && lower <= 'z') || (lower >= '0' && lower <= '9') || lower == '.' || lower == '-')) return "";
    out.push_back(lower);
  }
  return out;
}
}  // namespace red
