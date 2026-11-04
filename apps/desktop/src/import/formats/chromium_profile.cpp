#include "import/formats/chromium_profile.h"
#include "import/formats/import_crypto.h"
#include "import/formats/json_lite.h"
#include "import/formats/source_db.h"
#include <algorithm>
#include <cstring>
#include <fstream>
#include <map>
#include <set>

namespace red::import {
namespace {
// Chromium times: microseconds since 1601-01-01 UTC.
constexpr int64_t kWindowsToUnixMs = 11644473600000;
int64_t ChromiumTimeToUnixMs(int64_t t) { return t / 1000 - kWindowsToUnixMs; }
int64_t UnixMsToChromiumTime(int64_t ms) { return (ms + kWindowsToUnixMs) * 1000; }
}  // namespace

bool ReadSmallFile(const std::filesystem::path& path, size_t max, std::string& out) {
  out.clear(); std::error_code ec; const auto size = std::filesystem::file_size(path, ec);
  if (ec || size > max) return false;
  std::ifstream in(path, std::ios::binary); if (!in) return false;
  out.resize(static_cast<size_t>(size));
  return size == 0 || static_cast<bool>(in.read(out.data(), static_cast<std::streamsize>(out.size())));
}

bool ChromiumMasterKey(std::string_view local_state_json, std::string& key, bool& app_bound, std::string& error) {
  key.clear(); app_bound = false;
  const auto state = ParseJson(local_state_json);
  const auto* crypt = state ? state->Get("os_crypt") : nullptr;
  if (!crypt || !crypt->IsObject()) { error = "The browser's Local State file is missing or damaged"; return false; }
  app_bound = !crypt->Str("app_bound_encrypted_key").empty();
  std::string wrapped;
  if (!Base64Decode(crypt->Str("encrypted_key"), wrapped) || !wrapped.starts_with("DPAPI")) { error = "The browser's password key is missing"; return false; }
  std::string unwrapped;
  if (!DpapiUnprotect(std::string_view(wrapped).substr(5), unwrapped) || unwrapped.size() != 32) {
    Wipe(unwrapped); error = "Windows could not unlock this browser's password key (different Windows user?)"; return false;
  }
  key = std::move(unwrapped); return true;
}

std::map<std::string, std::string> ChromiumProfileNames(std::string_view local_state_json) {
  std::map<std::string, std::string> names;
  const auto state = ParseJson(local_state_json);
  const auto* profile = state ? state->Get("profile") : nullptr;
  const auto* cache = profile ? profile->Get("info_cache") : nullptr;
  if (!cache || !cache->IsObject()) return names;
  for (const auto& [folder, info] : cache->fields) {
    if (names.size() >= 50 || folder.size() > 64) break;
    const auto name = CleanLabel(info.Str("name"), 80, "");
    if (!name.empty()) names[folder] = name;
  }
  return names;
}

Decrypted DecryptChromiumValue(std::string_view key, std::string_view blob, std::string& out) {
  out.clear();
  if (blob.starts_with("v20")) return Decrypted::AppBound;
  if (blob.starts_with("v10") || blob.starts_with("v11")) {
    if (blob.size() < 3 + 12 + 16) return Decrypted::Unreadable;
    return AesGcmDecrypt(key, blob.substr(3, 12), blob.substr(15), out) ? Decrypted::Ok : Decrypted::Unreadable;
  }
  // Pre-80 Chromium stored each value as its own DPAPI blob.
  if (blob.size() > 4 && blob.substr(0, 4) == std::string_view("\x01\x00\x00\x00", 4)) return DpapiUnprotect(blob, out) ? Decrypted::Ok : Decrypted::Unreadable;
  return Decrypted::Unreadable;
}

ChromiumLogins ReadChromiumLogins(const std::filesystem::path& login_data, std::string_view key) {
  ChromiumLogins out; std::error_code ec;
  if (!std::filesystem::is_regular_file(login_data, ec)) return out;  // No saved passwords.
  SourceDb db(login_data);
  if (!db.db) { out.error = "Cannot open saved passwords. Close the browser and try again."; return out; }
  // scheme 0 = HTML form sign-in. Blocklisted ("never save") rows have no password.
  Query q(db.db, "SELECT origin_url,username_value,password_value,blacklisted_by_user,scheme FROM logins LIMIT 50001");
  if (!q.s) { out.error = "Unsupported saved-passwords database format"; return out; }
  while (q.Row()) {
    ++out.counts.found;
    if (q.Number(3) || q.Number(4) != 0 || out.logins.size() >= 50000) { ++out.counts.skipped; continue; }
    auto url = q.Text(0); auto user = q.Text(1); const auto blob = q.Text(2);
    if (!IsWebUrl(url) || blob.empty() || user.size() > 4096) { ++out.counts.skipped; continue; }
    std::string password;
    switch (DecryptChromiumValue(key, blob, password)) {
      case Decrypted::AppBound: ++out.counts.app_bound; continue;
      case Decrypted::Unreadable: ++out.counts.unreadable; continue;
      case Decrypted::Ok: break;
    }
    if (password.empty() || password.size() > 4096) { Wipe(password); ++out.counts.skipped; continue; }
    out.logins.emplace_back(std::move(url), std::move(user), std::move(password)); ++out.counts.decrypted;
  }
  if (!q.Done()) { WipeAll(out.logins); out.error = "Saved passwords are locked. Close the browser and try again."; }
  return out;
}

namespace {
void Walk(const Json& node, const std::string& parent, int depth, ChromiumBookmarks& out, bool bar) {
  if (depth > 64 || out.items.size() >= 10000 || !node.IsObject()) return;
  const auto type = node.Str("type"); const auto id = node.Str("id").empty() ? std::to_string(out.items.size()) : node.Str("id");
  if (type == "url") {
    if (!IsWebUrl(node.Str("url"))) return;
    out.items.push_back({id, parent, CleanLabel(node.Str("name"), 240, "Bookmark"), node.Str("url"), false, bar}); ++out.urls; return;
  }
  if (type != "folder") return;
  const auto* children = node.Get("children"); if (!children || !children->IsArray()) return;
  out.items.push_back({id, parent, CleanLabel(node.Str("name"), 240, "Folder"), "", true, bar});
  for (const auto& child : children->items) Walk(child, id, depth + 1, out, false);
}
}  // namespace

ChromiumBookmarks ParseChromiumBookmarks(std::string_view json) {
  ChromiumBookmarks out;
  const auto doc = ParseJson(json); const auto* roots = doc ? doc->Get("roots") : nullptr;
  if (!roots || !roots->IsObject()) { out.error = "The bookmarks file is damaged"; return out; }
  for (const char* name : {"bookmark_bar", "other", "synced"})
    if (const auto* root = roots->Get(name)) Walk(*root, "", 0, out, std::string_view(name) == "bookmark_bar");
  // Empty root folders add nothing.
  std::set<std::string> used; for (const auto& b : out.items) used.insert(b.parent);
  std::erase_if(out.items, [&](const Bookmark& b) { return b.folder && b.parent.empty() && !used.contains(b.id); });
  return out;
}

namespace {
std::string Lower(std::string_view s) { std::string r(s); for (auto& c : r) c = static_cast<char>(c >= 'A' && c <= 'Z' ? c + 32 : c); return r; }
std::string Unescape(std::string_view s) {
  std::string out; out.reserve(s.size());
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] != '&') { out += s[i]; continue; }
    const auto end = s.find(';', i); if (end == std::string_view::npos || end - i > 10) { out += '&'; continue; }
    const auto name = s.substr(i + 1, end - i - 1);
    if (name == "amp") out += '&'; else if (name == "lt") out += '<'; else if (name == "gt") out += '>';
    else if (name == "quot") out += '"'; else if (name == "#39" || name == "apos") out += '\''; else { out += '&'; continue; }
    i = end;
  }
  return out;
}
std::string Attribute(std::string_view tag, std::string_view name) {
  const auto lower = Lower(tag); const auto key = std::string(name) + "=\"";
  const auto at = lower.find(key); if (at == std::string::npos) return "";
  const auto start = at + key.size(); const auto end = tag.find('"', start);
  return end == std::string_view::npos ? "" : Unescape(tag.substr(start, end - start));
}
}  // namespace

ChromiumBookmarks ParseBookmarksHtml(std::string_view html) {
  ChromiumBookmarks out;
  if (Lower(html.substr(0, 256)).find("netscape-bookmark-file") == std::string::npos) { out.error = "This is not a bookmarks HTML export"; return out; }
  std::vector<std::string> stack{""}; std::string pending;  // Folder named by <H3>, opened by the next <DL>.
  int next = 0; size_t i = 0;
  auto text_until = [&](std::string_view close) {
    const auto end = Lower(html.substr(i, 64 * 1024)).find(close);
    const auto text = end == std::string::npos ? std::string_view() : html.substr(i, end); i += end == std::string::npos ? 0 : end + close.size();
    return Unescape(text);
  };
  while (i < html.size() && out.items.size() < 10000) {
    const auto open = html.find('<', i); if (open == std::string_view::npos) break;
    const auto close = html.find('>', open); if (close == std::string_view::npos) break;
    const auto tag = html.substr(open + 1, close - open - 1); const auto name = Lower(tag.substr(0, tag.find_first_of(" \t\r\n")));
    i = close + 1;
    if (name == "h3") {
      Bookmark f; f.id = "h" + std::to_string(next++); f.parent = stack.back(); f.folder = true;
      f.bar = Lower(tag).find("personal_toolbar_folder=\"true\"") != std::string::npos;
      f.title = CleanLabel(text_until("</h3>"), 240, "Folder"); pending = f.id; out.items.push_back(std::move(f));
    } else if (name == "dl") {
      if (stack.size() > 64) { out.error = "Bookmarks are nested too deeply"; return out; }
      if (!pending.empty()) { stack.push_back(pending); pending.clear(); } else if (stack.size() > 1 || next) stack.push_back(stack.back());
      ++next;
    } else if (name == "/dl") {
      if (stack.size() > 1) stack.pop_back();
    } else if (name == "a") {
      const auto href = Attribute(tag, "href"); const auto title = text_until("</a>");
      if (!IsWebUrl(href)) continue;
      out.items.push_back({"a" + std::to_string(next++), stack.back(), CleanLabel(title, 240, "Bookmark"), href, false, false}); ++out.urls;
    }
  }
  if (!out.urls) out.error = "No website bookmarks found in this file";
  return out;
}

ChromiumHistory ReadChromiumHistory(const std::filesystem::path& history, int64_t since_ms, size_t limit) {
  ChromiumHistory out; std::error_code ec;
  if (!std::filesystem::is_regular_file(history, ec)) return out;
  SourceDb db(history);
  if (!db.db) { out.error = "Cannot open history. Close the browser and try again."; return out; }
  Query q(db.db, "SELECT u.url,u.title,v.visit_time FROM visits v JOIN urls u ON u.id=v.url WHERE v.visit_time>? AND (u.url LIKE 'http:%' OR u.url LIKE 'https:%') ORDER BY v.visit_time DESC LIMIT ?");
  if (!q.s) { out.error = "Unsupported history database format"; return out; }
  sqlite3_bind_int64(q.s, 1, UnixMsToChromiumTime(since_ms)); sqlite3_bind_int64(q.s, 2, static_cast<sqlite3_int64>(limit));
  while (q.Row()) {
    const auto url = q.Text(0); if (!IsWebUrl(url)) continue;
    out.visits.push_back({url, CleanLabel(q.Text(1), 240, ""), ChromiumTimeToUnixMs(q.Number(2))});
  }
  if (!q.Done()) { out.visits.clear(); out.error = "History is locked. Close the browser and try again."; }
  return out;
}

namespace {
// base::Pickle reader: uint32 payload size header, 4-byte aligned fields.
class Pickle {
 public:
  explicit Pickle(std::string_view data) {
    if (data.size() < 4) return; uint32_t size = 0; std::memcpy(&size, data.data(), 4);
    if (size > data.size() - 4) return; data_ = data.substr(4, size); ok_ = true;
  }
  bool Int(int32_t& v) { if (!Take(4)) return false; std::memcpy(&v, data_.data() + at_ - 4, 4); return true; }
  bool String(std::string& v) {
    int32_t n = 0; if (!Int(n) || n < 0 || static_cast<size_t>(n) > data_.size() - at_) return false;
    v.assign(data_.data() + at_, n); return Skip(n);
  }
  bool String16(std::string& v) {
    int32_t n = 0; if (!Int(n) || n < 0 || static_cast<size_t>(n) > (data_.size() - at_) / 2) return false;
    std::u16string w(n, u'\0'); std::memcpy(w.data(), data_.data() + at_, static_cast<size_t>(n) * 2);
    v.clear();
    for (size_t k = 0; k < w.size(); ++k) {
      uint32_t c = w[k];
      if (c >= 0xd800 && c < 0xdc00 && k + 1 < w.size() && w[k + 1] >= 0xdc00 && w[k + 1] < 0xe000) c = 0x10000 + ((c - 0xd800) << 10) + (w[++k] - 0xdc00);
      else if (c >= 0xd800 && c < 0xe000) c = 0xfffd;
      if (c < 0x80) v += static_cast<char>(c);
      else if (c < 0x800) { v += static_cast<char>(0xc0 | c >> 6); v += static_cast<char>(0x80 | (c & 63)); }
      else if (c < 0x10000) { v += static_cast<char>(0xe0 | c >> 12); v += static_cast<char>(0x80 | (c >> 6 & 63)); v += static_cast<char>(0x80 | (c & 63)); }
      else { v += static_cast<char>(0xf0 | c >> 18); v += static_cast<char>(0x80 | (c >> 12 & 63)); v += static_cast<char>(0x80 | (c >> 6 & 63)); v += static_cast<char>(0x80 | (c & 63)); }
    }
    return Skip(static_cast<size_t>(n) * 2);
  }
  bool ok() const { return ok_; }

 private:
  bool Take(size_t n) { if (!ok_ || n > data_.size() - at_) return false; at_ += n; return true; }
  bool Skip(size_t n) { at_ += n; at_ = (at_ + 3) & ~size_t(3); if (at_ > data_.size()) at_ = data_.size(); return true; }
  std::string_view data_; size_t at_ = 0; bool ok_ = false;
};
int32_t I32(std::string_view p, size_t at) { int32_t v = 0; if (p.size() >= at + 4) std::memcpy(&v, p.data() + at, 4); return v; }
struct SessionTab {
  int window = -1, index_in_window = 0, selected = -1;
  bool pinned = false, closed = false;
  std::map<int, std::pair<std::string, std::string>> navigations;  // index → url, title
};
}  // namespace

ChromiumSession ParseSnss(std::string_view b) {
  ChromiumSession out;
  if (b.size() < 8 || b.substr(0, 4) != "SNSS") { out.error = "Not a Chromium session file"; return out; }
  const auto version = I32(b, 4);
  if (version == 2 || version == 4) { out.error = "This session file is encrypted"; return out; }
  if (version != 1 && version != 3) { out.error = "Unsupported session file version"; return out; }
  std::map<int, SessionTab> tabs; std::set<int> closed_windows;
  size_t i = 8;
  while (b.size() - i >= 2) {
    uint16_t size = 0; std::memcpy(&size, b.data() + i, 2); i += 2;
    if (!size || size > b.size() - i) break;  // A torn final write ends the stream.
    const auto id = static_cast<unsigned char>(b[i]); const auto p = b.substr(i + 1, size - 1); i += size;
    switch (id) {
      case 0: if (p.size() >= 8) tabs[I32(p, 4)].window = I32(p, 0); break;                // SetTabWindow
      case 2: if (p.size() >= 8) tabs[I32(p, 0)].index_in_window = I32(p, 4); break;       // SetTabIndexInWindow
      case 5: if (p.size() >= 8) { auto& n = tabs[I32(p, 0)].navigations; n.erase(n.lower_bound(I32(p, 4)), n.end()); } break;  // PrunedFromBack
      case 6: {                                                                             // UpdateTabNavigation
        Pickle pickle(p); int32_t tab = 0, index = 0; std::string url, title;
        if (pickle.ok() && pickle.Int(tab) && pickle.Int(index) && pickle.String(url) && pickle.String16(title) && index >= 0 && index < 10000)
          tabs[tab].navigations[index] = {std::move(url), std::move(title)};
        break;
      }
      case 7: if (p.size() >= 8) tabs[I32(p, 0)].selected = I32(p, 4); break;              // SetSelectedNavigationIndex
      case 12: if (p.size() >= 5) tabs[I32(p, 0)].pinned = p[4] != 0; break;               // SetPinnedState
      case 16: if (p.size() >= 4) tabs[I32(p, 0)].closed = true; break;                    // TabClosed
      case 17: if (p.size() >= 4) closed_windows.insert(I32(p, 0)); break;                 // WindowClosed
      default: break;
    }
    if (tabs.size() > 10000) { out.error = "The session has too many tabs"; return out; }
  }
  std::vector<std::pair<std::pair<int, int>, OpenTab>> ordered;
  for (auto& [tab_id, t] : tabs) {
    if (t.closed || t.window < 0 || closed_windows.contains(t.window) || t.navigations.empty()) continue;
    auto found = t.navigations.find(t.selected); if (found == t.navigations.end()) found = std::prev(t.navigations.end());
    const auto& [url, title] = found->second;
    if (!IsWebUrl(url)) { ++out.skipped; continue; }
    ordered.push_back({{t.window, t.index_in_window}, {url, CleanLabel(title, 240, ""), t.pinned, t.window}});
  }
  std::stable_sort(ordered.begin(), ordered.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
  for (auto& [key, tab] : ordered) out.tabs.push_back(std::move(tab));
  return out;
}

ChromiumSession ReadChromiumSession(const std::filesystem::path& profile) {
  std::vector<std::filesystem::path> files; std::error_code ec;
  for (std::filesystem::directory_iterator it(profile / "Sessions", ec), end; !ec && it != end; it.increment(ec))
    if (it->path().filename().wstring().starts_with(L"Session_") && it->is_regular_file(ec)) files.push_back(it->path());
  if (std::filesystem::is_regular_file(profile / "Current Session", ec)) files.push_back(profile / "Current Session");
  std::sort(files.begin(), files.end(), [](const auto& a, const auto& b) { std::error_code x, y; return std::filesystem::last_write_time(a, x) > std::filesystem::last_write_time(b, y); });
  ChromiumSession last; last.error = files.empty() ? "" : "Open tabs could not be read";
  for (const auto& file : files) {
    std::string bytes; if (!ReadSmallFile(file, 64 * 1024 * 1024, bytes)) continue;
    auto session = ParseSnss(bytes);
    if (session.error.empty() && !session.tabs.empty()) return session;
    if (session.error.empty()) last = std::move(session);
  }
  return last;
}
}  // namespace red::import
