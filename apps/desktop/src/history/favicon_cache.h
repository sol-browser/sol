#pragma once
#include "history/favicon_request.h"
#include <charconv>
#include <atomic>
#include <memory>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
namespace red {
// Live documents select their own icon. A site fallback serves saved entries
// and history. Both share immutable PNGs with the trusted UI's IO requests.
class FaviconCache {
 public:
  struct Update { bool changed = false; bool seeded = false; };

  bool Seed(const std::string& host, const std::string& png) {
    std::lock_guard lock(mutex_);
    auto& site = sites_[host];
    if (site.fallback) return false;
    site.fallback = Intern(site, host, png);
    return true;
  }
  void StartDocument(int browser, const std::string& host) {
    std::lock_guard lock(mutex_);
    auto& page = pages_[browser];
    page.request.NewDocument();
    const auto site = sites_.find(host);
    page.icon = site == sites_.end() ? nullptr : site->second.fallback;
  }
  std::optional<uint64_t> Begin(int browser, const std::string& url) {
    std::lock_guard lock(mutex_);
    return pages_[browser].request.Begin(url);
  }
  Update Complete(int browser, uint64_t token, const std::string& host, const std::string& png) {
    std::lock_guard lock(mutex_);
    const auto found = pages_.find(browser);
    if (found == pages_.end() || !found->second.request.Complete(token) || png.empty()) return {};
    auto& site = sites_[host];
    const auto icon = Intern(site, host, png);
    const Update result{found->second.icon != icon, !site.fallback};
    found->second.icon = icon;
    if (result.seeded) site.fallback = icon;
    return result;
  }
  void ForgetPage(int browser) {
    std::lock_guard lock(mutex_);
    pages_.erase(browser);
  }
  std::optional<std::string> PageUrl(int browser) const {
    std::lock_guard lock(mutex_);
    const auto found = pages_.find(browser);
    return found == pages_.end() ? std::nullopt : IconUrl(found->second.icon);
  }
  std::optional<std::string> Url(const std::string& host) const {
    std::lock_guard lock(mutex_);
    const auto site = sites_.find(host);
    return site == sites_.end() ? std::nullopt : IconUrl(site->second.fallback);
  }
  std::optional<std::string> PngForPath(std::string_view path) const {
    constexpr std::string_view prefix = "/favicon/";
    if (!path.starts_with(prefix)) return std::nullopt;
    path.remove_prefix(prefix.size());
    const auto slash = path.find('/');
    if (slash == std::string_view::npos) return std::nullopt;
    return Png(std::string(path.substr(0, slash)), path.substr(slash + 1));
  }

 private:
  std::optional<std::string> Png(const std::string& host, std::string_view key) const {
    if (key.size() < 2 || key.front() != 'v' || key[1] == '0') return std::nullopt;
    uint64_t version = 0;
    const auto end = key.data() + key.size();
    const auto parsed = std::from_chars(key.data() + 1, end, version);
    if (parsed.ec != std::errc{} || parsed.ptr != end) return std::nullopt;
    std::lock_guard lock(mutex_);
    const auto site = sites_.find(host);
    if (site == sites_.end()) return std::nullopt;
    const auto entry = site->second.icons.find(version);
    const auto icon = entry == site->second.icons.end() ? nullptr : entry->second.lock();
    return icon ? std::optional(icon->png) : std::nullopt;
  }
  struct Icon { std::string png, url; };
  using IconPtr = std::shared_ptr<const Icon>;
  struct Site {
    IconPtr fallback;
    std::map<uint64_t, std::weak_ptr<const Icon>> icons;
  };
  struct Page { FaviconRequest request; IconPtr icon; };
  static std::optional<std::string> IconUrl(const IconPtr& icon) {
    return icon ? std::optional(icon->url) : std::nullopt;
  }
  static IconPtr Intern(Site& site, const std::string& host, const std::string& png) {
    for (auto it = site.icons.begin(); it != site.icons.end();) {
      if (auto existing = it->second.lock()) {
        if (existing->png == png) return existing;
        ++it;
      } else {
        it = site.icons.erase(it);
      }
    }
    const auto version = ++next_version_;
    auto icon = std::make_shared<const Icon>(Icon{png, "sol://ui/favicon/" + host + "/v" + std::to_string(version)});
    site.icons.emplace(version, icon);
    return icon;
  }
  mutable std::mutex mutex_;
  inline static std::atomic<uint64_t> next_version_{0};
  std::map<std::string, Site> sites_;
  std::map<int, Page> pages_;
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
