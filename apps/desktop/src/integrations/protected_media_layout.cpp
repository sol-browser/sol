#include "integrations/protected_media_layout.h"

#include <array>
#include <cwchar>
#include <optional>
#include <string>
#include <system_error>

namespace red {
namespace fs = std::filesystem;
namespace {
// 45.2.1-1 → {45, 2, 1, 1}; nothing for any other shape.
std::optional<std::array<unsigned, 4>> Parts(std::wstring_view name) {
  std::array<unsigned, 4> parts{};
  size_t index = 0, digits = 0;
  for (const wchar_t c : name) {
    if (c >= L'0' && c <= L'9') {
      if (++digits > 6) return std::nullopt;
      parts[index] = parts[index] * 10 + static_cast<unsigned>(c - L'0');
    } else if (digits && ((index < 2 && c == L'.') || (index == 2 && c == L'-'))) {
      ++index; digits = 0;
    } else {
      return std::nullopt;
    }
  }
  if (index != 3 || !digits) return std::nullopt;
  return parts;
}
}  // namespace

fs::path ProtectedMediaBase(const fs::path& exe_dir) {
  std::error_code error;
  if (_wcsicmp(exe_dir.filename().c_str(), L"current") == 0 &&
      fs::is_regular_file(exe_dir.parent_path() / L"Update.exe", error))
    return exe_dir.parent_path() / L"protected-media";
  return exe_dir / L"protected-media";
}

bool ProtectedMediaVersionName(std::wstring_view name) { return Parts(name).has_value(); }

bool ProtectedMediaComplete(const fs::path& runtime) {
  std::error_code error;
  return !runtime.empty() && fs::is_regular_file(runtime / L"SolProtectedMedia.exe", error) &&
      fs::is_regular_file(runtime / L"SolProtectedMedia.exe.sig", error) &&
      fs::is_regular_file(runtime / L"resources" / L"app.asar", error);
}

fs::path ProtectedMediaRuntime(const fs::path& base, std::string_view id) {
  if (ProtectedMediaComplete(base)) return base;
  const auto pinned = base / std::wstring(id.begin(), id.end());
  if (ProtectedMediaComplete(pinned)) return pinned;
  fs::path best;
  std::array<unsigned, 4> newest{};
  std::error_code error;
  for (fs::directory_iterator it(base, error), end; !error && it != end; it.increment(error)) {
    const auto name = it->path().filename().wstring();
    const auto parts = Parts(name);
    if (!parts || !it->is_directory(error) || !ProtectedMediaComplete(it->path())) continue;
    if (best.empty() || *parts > newest) { best = it->path(); newest = *parts; }
  }
  return best;
}

bool ProtectedMediaHostInDomain(std::string_view host, std::string_view domain) {
  if (domain.empty() || host.empty()) return false;
  if (host == domain) return true;
  if (host.size() <= domain.size() + 1 || !host.ends_with(domain) || host[host.size() - domain.size() - 1] != '.') return false;
  const auto label = host.substr(0, host.size() - domain.size() - 1);
  return label.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789-.") == std::string_view::npos &&
      !label.starts_with('.') && !label.ends_with('.') && label.find("..") == std::string_view::npos;
}

bool ProtectedMediaHostListed(std::string_view host, std::string_view hosts) {
  if (host.empty()) return false;
  while (!hosts.empty()) {
    const auto space = hosts.find(' ');
    if (hosts.substr(0, space) == host) return true;
    if (space == std::string_view::npos) break;
    hosts.remove_prefix(space + 1);
  }
  return false;
}

bool ProtectedMediaSourceAllowed(std::string_view url) {
  if (url.size() > 2048) return false;
  for (const char c : url) if (static_cast<unsigned char>(c) <= 0x20 || c == '\\' || c == '@') return false;
  if (url.starts_with("https://") && url.size() > 8) return true;
  for (const std::string_view host : {"http://127.0.0.1:", "http://127.0.0.1/", "http://localhost:", "http://localhost/"})
    if (url.starts_with(host)) return true;
  return false;
}
}  // namespace red
