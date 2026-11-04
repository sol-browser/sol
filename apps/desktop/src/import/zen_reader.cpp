#include "import/zen_reader.h"
#include "include/cef_parser.h"
#include <windows.h>
#include <bcrypt.h>
#include <sqlite3.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <fstream>
#include <map>
#include <set>
#include <cwctype>

namespace red {
namespace {
using Dict = CefRefPtr<CefDictionaryValue>;
constexpr size_t kMaxSession = 64 * 1024 * 1024;
std::string Str(Dict d, const char* key) { return d && d->GetType(key) == VTYPE_STRING ? d->GetString(key).ToString() : ""; }
bool Flag(Dict d, const char* key) { return d && d->GetType(key) == VTYPE_BOOL && d->GetBool(key); }
int Int(Dict d, const char* key, int fallback = 0) { return d && d->GetType(key) == VTYPE_INT ? d->GetInt(key) : fallback; }
Dict Object(Dict d, const char* key) { return d && d->GetType(key) == VTYPE_DICTIONARY ? d->GetDictionary(key) : nullptr; }
CefRefPtr<CefListValue> List(Dict d, const char* key) { return d && d->GetType(key) == VTYPE_LIST ? d->GetList(key) : nullptr; }
std::string Label(std::string s, size_t max, const char* fallback) {
  std::erase_if(s, [](unsigned char c) { return c < 32 || c == 127; });
  if (s.size() > max) { size_t end = max; while (end && (static_cast<unsigned char>(s[end]) & 0xc0) == 0x80) --end; s.resize(end); }
  return s.find_first_not_of(" \t") == std::string::npos ? fallback : s;
}
std::string Id(const std::string& source, const std::string& key) {
  const auto data = source + "\n" + key; std::array<UCHAR, 32> hash{}; BCRYPT_ALG_HANDLE alg = nullptr;
  if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0) return "";
  const auto status = BCryptHash(alg, nullptr, 0, reinterpret_cast<PUCHAR>(const_cast<char*>(data.data())), static_cast<ULONG>(data.size()), hash.data(), static_cast<ULONG>(hash.size()));
  BCryptCloseAlgorithmProvider(alg, 0); if (status != 0) return "";
  hash[6] = (hash[6] & 15) | 80; hash[8] = (hash[8] & 63) | 128;
  constexpr char hex[] = "0123456789abcdef"; std::string id;
  for (size_t i = 0; i < 16; ++i) { if (i == 4 || i == 6 || i == 8 || i == 10) id += '-'; id += hex[hash[i] >> 4]; id += hex[hash[i] & 15]; }
  return id;
}
bool WebUrl(const std::string& url) {
  if (url.empty() || url.size() > 4096 || std::any_of(url.begin(), url.end(), [](unsigned char c) { return c < 32 || c == 127; })) return false;
  CefURLParts p; return CefParseURL(url, p) && p.host.length && !p.username.length && !p.password.length &&
    (CefString(&p.scheme) == "https" || CefString(&p.scheme) == "http");
}
// Mozilla's mozLz4 header + little-endian decoded size + raw LZ4 block.
// Bounds checked before every extension, copy and overlapping back-reference.
Dict Session(const std::filesystem::path& path) {
  std::error_code ec; const auto size = std::filesystem::file_size(path, ec);
  if (ec || size < 12 || size > kMaxSession) return nullptr;
  std::ifstream in(path, std::ios::binary); std::string b(static_cast<size_t>(size), '\0');
  if (!in.read(b.data(), static_cast<std::streamsize>(b.size())) || b.compare(0, 8, std::string("mozLz40\0", 8)) != 0) return nullptr;
  auto byte = [&](size_t i) { return static_cast<unsigned char>(b[i]); };
  const size_t expected = byte(8) | size_t(byte(9)) << 8 | size_t(byte(10)) << 16 | size_t(byte(11)) << 24;
  if (!expected || expected > kMaxSession) return nullptr;
  std::string out; out.reserve(expected); size_t i = 12;
  auto length = [&](size_t& n) { if (n != 15) return true; unsigned x; do { if (i >= b.size()) return false; x = byte(i++); if (n > expected || x > expected - n) return false; n += x; } while (x == 255); return true; };
  while (i < b.size()) {
    const unsigned token = byte(i++); size_t n = token >> 4;
    if (!length(n) || n > b.size() - i || n > expected - out.size()) return nullptr;
    out.append(b.data() + i, n); i += n; if (i == b.size()) break;
    if (b.size() - i < 2) return nullptr;
    const size_t offset = byte(i) | size_t(byte(i + 1)) << 8; i += 2; n = token & 15;
    if (!offset || offset > out.size() || !length(n) || n > expected - out.size() || expected - out.size() - n < 4) return nullptr;
    n += 4; while (n--) out += out[out.size() - offset];
  }
  if (out.size() != expected) return nullptr;
  auto value = CefParseJSON(out, JSON_PARSER_RFC); return value && value->GetType() == VTYPE_DICTIONARY ? value->GetDictionary() : nullptr;
}
class SourceDb {
 public:
  explicit SourceDb(const std::filesystem::path& path) {
    const auto utf = path.u8string();
    if (sqlite3_open_v2(reinterpret_cast<const char*>(utf.c_str()), &db, SQLITE_OPEN_READONLY | SQLITE_OPEN_NOMUTEX, nullptr) != SQLITE_OK) { sqlite3_close(db); db = nullptr; }
    if (db) { sqlite3_busy_timeout(db, 1500); sqlite3_exec(db, "PRAGMA query_only=ON; BEGIN", nullptr, nullptr, nullptr); }
  }
  ~SourceDb() { if (db) sqlite3_close(db); }
  sqlite3* db = nullptr;
};
class Query {
 public:
  Query(sqlite3* db, const char* sql) { if (db) sqlite3_prepare_v2(db, sql, -1, &s, nullptr); }
  ~Query() { sqlite3_finalize(s); }
  bool Row() { return s && (code = sqlite3_step(s)) == SQLITE_ROW; }
  std::string Text(int col) { const auto* p = sqlite3_column_text(s, col); return p ? std::string(reinterpret_cast<const char*>(p), sqlite3_column_bytes(s, col)) : ""; }
  int64_t Number(int col) { return sqlite3_column_int64(s, col); }
  sqlite3_stmt* s = nullptr; int code = SQLITE_DONE;
};
int64_t Now() { return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count(); }
bool DefaultAttributes(Dict d) {
  if (!d) return true;
  CefDictionaryValue::KeyList keys; d->GetKeys(keys);
  for (const auto& key : keys) if ((d->GetType(key) != VTYPE_INT || d->GetInt(key) != 0) && (d->GetType(key) != VTYPE_STRING || !d->GetString(key).empty())) return false;
  return true;
}
bool MakeCookie(ZenImport& out, const std::string& host, const std::string& name, const std::string& value, const std::string& path, bool secure, bool http, int same, int64_t expiry) {
  if (out.cookies.size() >= 100000 || host.empty() || host.size() > 253 || name.size() + value.size() > 4096 || path.size() > 4096 ||
      host.find_first_of("/\\@?# \r\n\t") != std::string::npos || (expiry && expiry <= Now())) { ++out.skipped_cookies; return false; }
  const auto domain = host.starts_with('.') ? host.substr(1) : host;
  const auto url = "https://" + domain + "/"; if (!WebUrl(url)) { ++out.skipped_cookies; return false; }
  ZenCookie result; result.url = url;
  CefString(&result.cookie.name) = name; CefString(&result.cookie.value) = value;
  if (host.starts_with('.')) CefString(&result.cookie.domain) = host;
  CefString(&result.cookie.path) = path.empty() ? "/" : path;
  result.cookie.secure = secure; result.cookie.httponly = http;
  result.cookie.same_site = same == 2 ? CEF_COOKIE_SAME_SITE_STRICT_MODE : same == 1 ? CEF_COOKIE_SAME_SITE_LAX_MODE : same == 0 ? CEF_COOKIE_SAME_SITE_NO_RESTRICTION : CEF_COOKIE_SAME_SITE_UNSPECIFIED;
  result.cookie.has_expires = expiry != 0;
  if (expiry) {
    cef_time_t time{};
    if (!cef_time_from_timet(static_cast<time_t>(expiry), &time) || !cef_time_to_basetime(&time, &result.cookie.expires)) { ++out.skipped_cookies; return false; }
  }
  out.cookies.push_back(std::move(result)); return true;
}
}
std::vector<ZenProfile> DiscoverZenProfiles() {
  std::vector<ZenProfile> result; wchar_t roaming[32768]{};
  if (!GetEnvironmentVariableW(L"APPDATA", roaming, std::size(roaming))) return result;
  const auto root = std::filesystem::path(roaming) / "zen";
  // Firefox writes UTF-8 INI files; Win32's INI API assumes the system ANSI
  // codepage unless there is a UTF-16 BOM, breaking non-ASCII profile paths.
  std::error_code ec; const auto bytes = std::filesystem::file_size(root / "profiles.ini", ec);
  if (ec || bytes > 1024 * 1024) return result;
  std::ifstream ini(root / "profiles.ini", std::ios::binary); std::string line, section;
  std::map<std::string,std::map<std::string,std::string>> entries;
  while (std::getline(ini,line)) {
    if (line.starts_with("\xef\xbb\xbf")) line.erase(0,3);
    if (!line.empty() && line.back()=='\r') line.pop_back();
    if (line.size()>32768) continue;
    if (line.starts_with('[') && line.ends_with(']')) { section=line.substr(1,line.size()-2); continue; }
    const auto at=line.find('='); if (section.starts_with("Profile") && at!=std::string::npos) entries[section][line.substr(0,at)]=line.substr(at+1);
  }
  for (auto& [section_name,fields] : entries) {
    if (fields["Path"].empty() || result.size()>=100) continue;
    const auto path=std::filesystem::path(CefString(fields["Path"]).ToWString());
    const auto profile=fields["IsRelative"]=="0" ? path : root/path;
    if (std::filesystem::is_regular_file(profile/"cookies.sqlite",ec) || std::filesystem::is_regular_file(profile/"sessionstore.jsonlz4",ec) ||
        std::filesystem::is_regular_file(profile/"sessionstore-backups/recovery.jsonlz4",ec))
      result.push_back({profile,CefString(Label(fields["Name"],512,section_name.c_str())).ToWString()});
  }
  return result;
}
ZenImport ReadZenProfile(const std::filesystem::path& profile) {
  ZenImport out; std::error_code ec; auto normalized = std::filesystem::weakly_canonical(profile, ec).wstring();
  if (ec) { out.error = "Cannot read the selected profile folder"; return out; }
  std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
  const auto source = CefString(normalized).ToString();
  std::vector<std::filesystem::path> candidates;
  for (const auto* name : {L"sessionstore.jsonlz4", L"sessionstore-backups/recovery.jsonlz4", L"sessionstore-backups/recovery.baklz4", L"sessionstore-backups/previous.jsonlz4"})
    if (std::filesystem::is_regular_file(profile / name, ec)) candidates.push_back(profile / name);
  std::sort(candidates.begin(), candidates.end(), [&](const auto& a, const auto& b) { std::error_code x, y; return std::filesystem::last_write_time(a, x) > std::filesystem::last_write_time(b, y); });
  Dict session; for (const auto& candidate : candidates) { session = Session(candidate); if (session) break; out.used_backup = true; }
  if (!session) { out.error = "No readable Zen session found. Close Zen normally and try again."; return out; }
  auto windows = List(session, "windows"); if (!windows || windows->GetSize() > 100) { out.error = "Invalid Zen session windows"; return out; }
  std::map<std::string, std::string> spaces; std::set<std::string> ids;
  auto space = [&](const std::string& old, const std::string& name, const std::string& icon) {
    if (spaces.contains(old)) return spaces[old];
    Space s; s.id = Id(source, "space:" + old); s.name = Label(name, 40, "Zen"); s.icon = Label(icon, 16, "");
    if (s.icon.find(' ') != std::string::npos) s.icon.clear();
    s.position = static_cast<int>(out.tree.spaces.size()); out.tree.spaces.push_back(s); return spaces[old] = s.id;
  };
  for (size_t wi = 0; wi < windows->GetSize(); ++wi) {
    auto w = windows->GetDictionary(wi); if (!w || Flag(w, "isPrivate")) continue;
    if (auto ss = List(w, "spaces")) for (size_t i = 0; i < ss->GetSize(); ++i) {
      auto s = ss->GetDictionary(i); if (s) space(Str(s, "uuid"), Str(s, "name"), Str(s, "icon"));
    }
  }
  const auto fallback = out.tree.spaces.empty() ? space("", "Zen", "") : out.tree.spaces.front().id;
  auto owner = [&](const std::string& old) { return spaces.contains(old) ? spaces[old] : fallback; };
  for (size_t wi = 0; wi < windows->GetSize(); ++wi) {
    auto w = windows->GetDictionary(wi); if (!w || Flag(w, "isPrivate")) continue;
    auto folders = List(w, "folders"); if (!folders) folders = List(w, "groups");
    if (folders) for (size_t i = 0; i < folders->GetSize(); ++i) {
      auto f = folders->GetDictionary(i); const auto old = Str(f, "id"); if (old.empty()) continue;
      TreeNode n; n.id = Id(source, "folder:" + old); if (!ids.insert(n.id).second) continue;
      n.folder = n.pinned = true; n.title = Label(Str(f, "name"), 240, "Folder"); n.expanded = !Flag(f, "collapsed");
      const auto parent = Str(f, "parentId"); if (!parent.empty()) n.parent = Id(source, "folder:" + parent);
      n.space = n.parent.empty() ? owner(Str(f, "workspaceId")) : "";
      out.tree.nodes.push_back(n); ++out.folders;
    }
    auto tabs = List(w, "tabs"); if (!tabs || tabs->GetSize() > 10000) continue;
    for (size_t i = 0; i < tabs->GetSize(); ++i) {
      auto t = tabs->GetDictionary(i); auto entries = List(t, "entries");
      if (!entries || entries->GetSize() == 0 || Flag(t, "zenIsEmpty") || Flag(t, "zenIsGlance") || Flag(t, "isPrivate")) { ++out.skipped_tabs; continue; }
      const auto index = std::clamp(Int(t, "index", static_cast<int>(entries->GetSize())) - 1, 0, static_cast<int>(entries->GetSize()) - 1);
      auto entry = entries->GetDictionary(index); const auto url = Str(entry, "url");
      if (!WebUrl(url)) { ++out.skipped_tabs; continue; }
      auto old = Str(t, "zenSyncId"); if (old.empty()) old = std::to_string(wi) + ":" + std::to_string(i);
      TreeNode n; n.id = Id(source, "tab:" + old); if (!ids.insert(n.id).second) continue;
      n.url = url; n.page_title = Label(Str(entry, "title"), 240, "");
      n.custom_title = !Str(t, "zenStaticLabel").empty(); n.title = Label(n.custom_title ? Str(t, "zenStaticLabel") : n.page_title, 240, "Imported tab");
      n.favorite = Flag(t, "zenEssential"); n.pinned = Flag(t, "pinned") || n.favorite;
      const auto group = Id(source, "folder:" + Str(t, "groupId"));
      if (!n.favorite && ids.contains(group)) { n.parent = group; n.pinned = true; }
      n.space = n.parent.empty() && !n.favorite ? owner(Str(t, "zenWorkspace")) : "";
      out.tree.nodes.push_back(n); ++out.tabs;
    }
  }
  // Broken/removed folder parents are flattened; valid nesting is retained.
  for (auto& n : out.tree.nodes) if (!n.parent.empty() && !ids.contains(n.parent)) { n.parent.clear(); n.space = fallback; }
  SourceDb places(profile / "places.sqlite");
  if (!places.db && std::filesystem::exists(profile / "places.sqlite", ec)) { out.error = "Cannot read Zen bookmarks/history. Close Zen and retry."; return out; }
  if (places.db) {
    const auto root_id = Id(source, "bookmarks"); std::vector<TreeNode> bookmarks;
    Query q(places.db, "SELECT b.id,b.parent,b.type,b.title,p.url FROM moz_bookmarks b LEFT JOIN moz_places p ON p.id=b.fk ORDER BY b.parent,b.position LIMIT 10001");
    if (q.s) while (q.Row()) {
      const auto kind = q.Number(2); if (kind != 1 && kind != 2) continue;
      TreeNode n; n.id = Id(source, "bookmark:" + std::to_string(q.Number(0))); n.parent = Id(source, "bookmark:" + std::to_string(q.Number(1)));
      n.folder = kind == 2; n.pinned = true; n.url = n.folder ? "" : q.Text(4); if (!n.folder && !WebUrl(n.url)) continue;
      n.title = Label(q.Text(3), 240, n.folder ? "Bookmarks" : "Bookmark"); n.custom_title = true;
      bookmarks.push_back(n);
    }
    std::set<std::string> bid; for (const auto& n : bookmarks) if (n.folder) bid.insert(n.id);
    if (!bookmarks.empty()) {
      TreeNode root; root.id = root_id; root.title = "Zen bookmarks"; root.folder = root.pinned = true; root.expanded = false; root.space = fallback; out.tree.nodes.push_back(root);
      for (auto& n : bookmarks) { if (!bid.contains(n.parent)) n.parent = root_id; out.tree.nodes.push_back(n); if (!n.folder) ++out.bookmarks; }
    }
    Query h(places.db, "SELECT p.url,p.title,v.visit_date/1000 FROM moz_historyvisits v JOIN moz_places p ON p.id=v.place_id WHERE v.visit_date > (strftime('%s','now')-7776000)*1000000 ORDER BY v.visit_date DESC LIMIT 50000");
    if (!q.s || !h.s) { out.error = "Unsupported Zen bookmarks/history database format"; return out; }
    if (h.s) while (h.Row()) if (WebUrl(h.Text(0))) out.history.push_back({0,h.Text(0),Label(h.Text(1),240,""),h.Number(2)});
    if ((q.s && q.code != SQLITE_DONE) || (h.s && h.code != SQLITE_DONE)) { out.error = "Zen bookmarks/history could not be read consistently. Retry after closing Zen."; return out; }
  }
  SourceDb cookies(profile / "cookies.sqlite");
  if (!cookies.db && std::filesystem::exists(profile / "cookies.sqlite", ec)) { out.error = "Cannot read Zen cookies. Close Zen and retry."; return out; }
  Query c(cookies.db, "SELECT host,name,value,path,isSecure,isHttpOnly,sameSite,expiry,originAttributes,isPartitionedAttributeSet FROM moz_cookies ORDER BY lastAccessed DESC LIMIT 100001");
  if (cookies.db && !c.s) { out.error = "Unsupported Zen cookies database format"; return out; }
  if (c.s) while (c.Row()) {
    if (!c.Text(8).empty() || c.Number(9)) { ++out.skipped_cookies; continue; }
    MakeCookie(out,c.Text(0),c.Text(1),c.Text(2),c.Text(3),c.Number(4)!=0,c.Number(5)!=0,static_cast<int>(c.Number(6)),c.Number(7));
  }
  if (c.s && c.code != SQLITE_DONE) { out.error = "Zen cookies database is busy or unreadable. Retry after closing Zen."; return out; }
  if (auto sc = List(session, "cookies")) for (size_t i=0;i<sc->GetSize() && i<100000;++i) {
    auto item = sc->GetDictionary(i); if (!item || !DefaultAttributes(Object(item,"originAttributes"))) { ++out.skipped_cookies; continue; }
    MakeCookie(out,Str(item,"host"),Str(item,"name"),Str(item,"value"),Str(item,"path"),Flag(item,"secure"),Flag(item,"httponly"),Int(item,"sameSite",256),0);
  }
  std::map<std::string,int> positions; int favorites = 0;
  for (auto& n : out.tree.nodes) {
    if (n.favorite && ++favorites > 12) { n.favorite = false; n.space = fallback; }
    n.position = positions[n.parent]++;
  }
  out.tree.active_space = fallback;
  if (out.tree.nodes.size() > 10000 || out.tree.spaces.size() > kMaxSpaces) out.error = "Profile exceeds Sol's limits (10,000 entries / 24 Spaces). Nothing imported.";
  if (!out.tabs && !out.bookmarks && out.error.empty()) out.error = "This profile has no supported website tabs or bookmarks";
  if (out.error.empty() && !TreeStore::ValidateSnapshot(out.tree)) out.error = "Unsupported or damaged Zen tab hierarchy. Nothing imported.";
  return out;
}
}
