#include "import/formats/favicons.h"
#include "import/formats/chromium_profile.h"
#include "web_url.h"
#include "import/formats/source_db.h"
#include "history/favicon_cache.h"
#include <windows.h>
#include <bcrypt.h>
#include <array>
#include <cstring>

namespace red::importer {
namespace {
constexpr size_t kMaxPng = 12 * 1024;  // Same cap as icons downloaded by pages.
constexpr std::string_view kPngSignature("\x89PNG\r\n\x1a\n", 8);
// Lower is better: exactly 32 px, then larger (downscaled crisply), then smaller.
int Score(int width) { return width >= 32 ? width - 32 : (32 - width) * 3; }
struct Best { std::string png; int score = 1 << 30; };
void Offer(std::map<std::string, Best>& best, const std::string& host, std::string_view bytes, int width) {
  if (host.empty()) return;
  auto png = IconPng(bytes); if (!png || png->size() > kMaxPng) return;
  auto& slot = best[host]; const int score = Score(width > 0 ? width : 16);
  if (score < slot.score) { slot.png = std::move(*png); slot.score = score; }
}
Favicons Collect(std::map<std::string, Best>& best) {
  Favicons out; for (auto& [host, b] : best) if (!b.png.empty()) out[host] = std::move(b.png);
  return out;
}

// Arc uses this digest as a filename, with no security purpose.
std::string ArcIconFilename(const std::string& id) {
  if (id.empty() || id.size() > 256) return {};
  BCRYPT_ALG_HANDLE algorithm = nullptr;
  if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_MD5_ALGORITHM, nullptr, 0) != 0) return {};
  std::array<UCHAR, 16> digest{};
  const auto result = BCryptHash(algorithm, nullptr, 0, reinterpret_cast<PUCHAR>(const_cast<char*>(id.data())),
                                static_cast<ULONG>(id.size()), digest.data(), static_cast<ULONG>(digest.size()));
  BCryptCloseAlgorithmProvider(algorithm, 0);
  if (result != 0) return {};
  constexpr char hex[] = "0123456789abcdef";
  std::string filename;
  for (const auto byte : digest) { filename += hex[byte >> 4]; filename += hex[byte & 15]; }
  return filename;
}
}  // namespace

std::optional<std::string> ReadArcItemFavicon(const std::filesystem::path& directory, const std::string& item_id) {
  const auto filename = ArcIconFilename(item_id);
  if (filename.empty()) return std::nullopt;
  const auto expected = directory / filename;
  std::error_code ec;
  if (!std::filesystem::is_regular_file(expected, ec)) return std::nullopt;
  const auto resolved = std::filesystem::canonical(expected, ec);
  if (ec || CompareStringOrdinal(resolved.c_str(), -1, expected.c_str(), -1, TRUE) != CSTR_EQUAL) return std::nullopt;
  std::string bytes;
  if (!ReadSmallFile(resolved, kMaxPng, bytes) || !bytes.starts_with(kPngSignature)) return std::nullopt;
  return bytes;
}

std::string UrlHost(std::string_view url) {
  const size_t start = WebSchemeLength(url);
  if (!start) return "";
  auto end = url.find_first_of("/?#", start); if (end == std::string_view::npos) end = url.size();
  auto authority = url.substr(start, end - start);
  if (authority.find('@') != std::string_view::npos || authority.starts_with('[')) return "";
  if (const auto port = authority.find(':'); port != std::string_view::npos) authority = authority.substr(0, port);
  return FaviconHost(std::string(authority));
}

std::optional<std::string> IconPng(std::string_view b) {
  if (b.starts_with(kPngSignature)) return std::string(b);
  // ICONDIR: reserved 0, type 1, count; 16-byte entries (width 0 = 256, size, offset).
  if (b.size() < 6 || b[0] || b[1] || b[2] != 1 || b[3]) return std::nullopt;
  const size_t count = static_cast<unsigned char>(b[4]) | static_cast<unsigned char>(b[5]) << 8;
  if (!count || count > 64 || b.size() < 6 + count * 16) return std::nullopt;
  std::optional<std::string> best; int best_score = 1 << 30;
  for (size_t i = 0; i < count; ++i) {
    const char* e = b.data() + 6 + i * 16;
    const int width = static_cast<unsigned char>(e[0]) ? static_cast<unsigned char>(e[0]) : 256;
    uint32_t size = 0, offset = 0; std::memcpy(&size, e + 8, 4); std::memcpy(&offset, e + 12, 4);
    if (offset > b.size() || size > b.size() - offset) continue;
    const auto image = b.substr(offset, size);
    if (!image.starts_with(kPngSignature) || Score(width) >= best_score) continue;  // BMP icons are left to the page.
    best = std::string(image); best_score = Score(width);
  }
  return best;
}

Favicons ReadChromiumFavicons(const std::filesystem::path& path, const std::set<std::string>& hosts) {
  std::error_code ec; if (hosts.empty() || !std::filesystem::is_regular_file(path, ec)) return {};
  SourceDb db(path); if (!db.db) return {};
  Query q(db.db, "SELECT m.page_url,b.image_data,b.width FROM icon_mapping m JOIN favicon_bitmaps b ON b.icon_id=m.icon_id "
                 "WHERE length(b.image_data)>0 AND length(b.image_data)<=12288 LIMIT 200000");
  std::map<std::string, Best> best;
  while (q.Row()) { const auto host = UrlHost(q.Text(0)); if (hosts.contains(host)) Offer(best, host, q.Text(1), static_cast<int>(q.Number(2))); }
  return Collect(best);
}

Favicons ReadFirefoxFavicons(const std::filesystem::path& path, const std::set<std::string>& hosts) {
  std::error_code ec; if (hosts.empty() || !std::filesystem::is_regular_file(path, ec)) return {};
  SourceDb db(path); if (!db.db) return {};
  Query q(db.db, "SELECT p.page_url,i.data,i.width FROM moz_pages_w_icons p JOIN moz_icons_to_pages t ON t.page_id=p.id "
                 "JOIN moz_icons i ON i.id=t.icon_id WHERE length(i.data)>0 AND length(i.data)<=65536 LIMIT 200000");
  std::map<std::string, Best> best;
  while (q.Row()) { const auto host = UrlHost(q.Text(0)); if (hosts.contains(host)) Offer(best, host, q.Text(1), static_cast<int>(q.Number(2))); }
  // Site-wide /favicon.ico icons ("root" icons) are not mapped to pages.
  Query root(db.db, "SELECT icon_url,data,width FROM moz_icons WHERE root=1 AND length(data)>0 AND length(data)<=65536 LIMIT 50000");
  while (root.Row()) { const auto host = UrlHost(root.Text(0)); if (hosts.contains(host)) Offer(best, host, root.Text(1), static_cast<int>(root.Number(2))); }
  return Collect(best);
}
}  // namespace red::importer
