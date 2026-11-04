// Parsers for other browsers' profiles, against synthetic fixtures made by
// tests/fixtures/import/make_fixtures.mjs (no real profile data).
#include "import/formats/chromium_profile.h"
#include "import/formats/favicons.h"
#include "import/formats/firefox_logins.h"
#include "import/formats/import_crypto.h"
#include "import/formats/import_tree.h"
#include "import/formats/json_lite.h"
#include "import/formats/password_csv.h"
#include <windows.h>
#include <wincrypt.h>
#include <dpapi.h>
#include <cstdio>

namespace {
int failures = 0;
void Check(bool test, const char* name) { if (!test) { ++failures; std::fprintf(stderr, "FAIL: %s\n", name); } }
const std::filesystem::path kFixtures = RED_IMPORT_FIXTURES;
std::string File(const std::filesystem::path& path) { std::string s; red::import::ReadSmallFile(path, 1 << 20, s); return s; }
const red::import::Login* Find(const std::vector<red::import::Login>& logins, const char* url) {
  for (const auto& l : logins) if (l.url == url) return &l;
  return nullptr;
}
std::string Base64(const std::string& bytes) {
  DWORD size = 0; CryptBinaryToStringA(reinterpret_cast<const BYTE*>(bytes.data()), static_cast<DWORD>(bytes.size()), CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, nullptr, &size);
  std::string out(size, '\0'); CryptBinaryToStringA(reinterpret_cast<const BYTE*>(bytes.data()), static_cast<DWORD>(bytes.size()), CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, out.data(), &size);
  out.resize(size); return out;
}
}  // namespace

// Smoke helper: `--count-logins <User Data dir>` lists a disposable Red
// profile's saved sign-ins as origin + username (never passwords) and whether
// each password is one of the synthetic fixture values.
int CountLogins(const std::filesystem::path& root) {
  using namespace red::import;
  std::string json, key, error; bool app_bound = false;
  if (!ReadSmallFile(root / "Local State", 1 << 24, json) || !ChromiumMasterKey(json, key, app_bound, error)) { std::printf("{\"error\":\"no key\"}\n"); return 1; }
  const char* known[] = {"p,w\"1", "line1\nline2", "correct-horse-1", "p\xc3\xa4ssw\xc3\xb6rd \xe2\x9c\x93", "aes-secret", "des-\xc3\xabxtra", "ff-pass"};
  std::printf("{\"logins\":[");
  bool first = true; int count = 0;
  for (const char* store : {"Login Data", "Login Data For Account"}) {
    auto r = ReadChromiumLogins(root / "Default" / store, key);
    for (const auto& l : r.logins) {
      bool synthetic = false; for (const char* k : known) synthetic |= l.password == k;
      std::printf("%s{\"origin\":\"%s\",\"username\":\"%s\",\"synthetic_password\":%s}", first ? "" : ",", l.url.c_str(), l.username.c_str(), synthetic ? "true" : "false");
      first = false; ++count;
    }
  }
  std::printf("],\"count\":%d,\"app_bound\":%s}\n", count, app_bound ? "true" : "false");
  Wipe(key); return 0;
}

int wmain(int argc, wchar_t** argv) {
  using namespace red::import;
  if (argc == 3 && std::wstring_view(argv[1]) == L"--count-logins") return CountLogins(argv[2]);
  {  // JSON reader
    Check(ParseJson(R"({"a":[1,"\u00e9\ud83d\ude00",true,null],"b":{"c":"13400000000000000"}})").has_value(), "json parses");
    const auto doc = ParseJson(R"({"a":"\u00e9\ud83d\ude00","n":"13400000000000000"})");
    Check(doc && doc->Str("a") == "\xc3\xa9\xf0\x9f\x98\x80" && doc->Int("n") == 13400000000000000, "json escapes and string integers");
    Check(!ParseJson("{\"a\":1} x") && !ParseJson("{\"a\":\"\\ud800\"}") && !ParseJson("[1,]") && !ParseJson(std::string(200, '[')), "json rejects malformed input");
  }
  {  // URL policy
    Check(IsWebUrl("https://a.test/x") && IsWebUrl("http://a.test"), "web URLs accepted");
    Check(!IsWebUrl("javascript:alert(1)") && !IsWebUrl("https://u:p@a.test/") && !IsWebUrl("android://h@app/") && !IsWebUrl("https:///x") && !IsWebUrl("https://a.test/\n"), "non-web URLs refused");
  }
  {  // Chromium master key through a real DPAPI round trip (user-bound, so made here).
    std::string key(32, '\0'); for (int i = 0; i < 32; ++i) key[i] = static_cast<char>(i);
    DATA_BLOB in{32, reinterpret_cast<BYTE*>(key.data())}, out{};
    Check(CryptProtectData(&in, nullptr, nullptr, nullptr, nullptr, 0, &out) != FALSE, "DPAPI protect for fixture");
    const std::string wrapped = "DPAPI" + std::string(reinterpret_cast<const char*>(out.pbData), out.cbData); LocalFree(out.pbData);
    std::string unwrapped; bool app_bound = true; std::string error;
    Check(ChromiumMasterKey(R"({"os_crypt":{"encrypted_key":")" + Base64(wrapped) + R"("}})", unwrapped, app_bound, error) && unwrapped == key && !app_bound, "Local State key unwrap");
    Check(ChromiumMasterKey(R"({"os_crypt":{"encrypted_key":")" + Base64(wrapped) + R"(","app_bound_encrypted_key":"QVBQQg=="}})", unwrapped, app_bound, error) && app_bound, "app-bound key reported");
    Check(!ChromiumMasterKey(R"({"os_crypt":{"encrypted_key":"bm9wZQ=="}})", unwrapped, app_bound, error) && unwrapped.empty(), "non-DPAPI key refused");
    Check(!ChromiumMasterKey("{}", unwrapped, app_bound, error), "missing os_crypt refused");
    const auto names = ChromiumProfileNames(R"({"profile":{"info_cache":{"Default":{"name":"Work — Работа"},"Profile 2":{"name":"\u0001"},"Profile 3":{"name":"Home"}}}})");
    Check(names.size() == 2 && names.at("Default") == "Work \xe2\x80\x94 \xd0\xa0\xd0\xb0\xd0\xb1\xd0\xbe\xd1\x82\xd0\xb0" && names.at("Profile 3") == "Home", "profile display names");
    Check(ChromiumProfileNames("{}").empty() && ChromiumProfileNames("not json").empty(), "no profile names");

    const auto logins = ReadChromiumLogins(kFixtures / "chromium" / "Login Data", key);
    Check(logins.error.empty(), "Login Data readable");
    Check(logins.counts.found == 7 && logins.counts.decrypted == 2 && logins.counts.app_bound == 1 && logins.counts.unreadable == 1 && logins.counts.skipped == 3, "Login Data counts");
    const auto* alice = Find(logins.logins, "https://login.example.test/signin");
    Check(alice && alice->username == "alice@example.test" && alice->password == "correct-horse-1", "v10 password decrypted");
    const auto* bob = Find(logins.logins, "https://mail.example.test/");
    Check(bob && bob->password == "p\xc3\xa4ssw\xc3\xb6rd \xe2\x9c\x93", "UTF-8 password intact");
    Check(!Find(logins.logins, "https://bank.example.test/"), "v20 app-bound value is not guessed");
    std::string wrong(32, 'x');
    const auto bad = ReadChromiumLogins(kFixtures / "chromium" / "Login Data", wrong);
    Check(bad.logins.empty() && bad.counts.unreadable == 3, "wrong key decrypts nothing");
    Check(ReadChromiumLogins(kFixtures / "chromium" / "missing", key).counts.found == 0, "absent Login Data is empty, not an error");
  }
  {
    const auto b = ParseChromiumBookmarks(File(kFixtures / "chromium" / "Bookmarks"));
    Check(b.error.empty() && b.urls == 3, "bookmarks: three web URLs (javascript: dropped)");
    bool bar = false, nested = false, other = false;
    for (const auto& x : b.items) { bar |= x.bar && x.folder && x.title == "Bookmarks bar"; nested |= x.url == "https://tracker.example.test/?q=a&b=1" && x.parent == "6"; other |= x.title == "Other bookmarks"; }
    Check(bar && nested && !other, "bookmarks keep nesting, drop empty roots");
    Check(!ParseChromiumBookmarks("{\"roots\":5}").error.empty(), "damaged bookmarks refused");
  }
  {
    const auto h = ReadChromiumHistory(kFixtures / "chromium" / "History", 1'500'000'000'000, 100);
    Check(h.error.empty() && h.visits.size() == 2, "history: recent web visits only");
    Check(h.visits.size() == 2 && h.visits[0].visited_ms == 1'800'000'060'000 && h.visits[0].title == "News A", "history time converted, newest first");
    Check(ReadChromiumHistory(kFixtures / "chromium" / "History", 0, 1).visits.size() == 1, "history limit");
  }
  {
    const auto s = ReadChromiumSession(kFixtures / "chromium");
    Check(s.error.empty() && s.tabs.size() == 2, "session: two open web tabs");
    Check(s.tabs.size() == 2 && s.tabs[0].url == "https://pinned.example.test/" && s.tabs[0].pinned, "pinned tab, window order");
    Check(s.tabs.size() == 2 && s.tabs[1].url == "https://second.example.test/" && s.tabs[1].title == "Second \xf0\x9f\x98\x80" && !s.tabs[1].pinned, "selected navigation + UTF-16 title");
    Check(s.skipped == 1, "internal page skipped");
    Check(!ParseSnss("SNSS\x02\0\0\0").error.empty() && !ParseSnss("nope").error.empty(), "encrypted/foreign session refused");
  }
  {
    const auto f = ReadFirefoxLogins(kFixtures / "firefox", "");
    Check(f.status == FirefoxStatus::Ok && f.error.empty(), "Firefox logins readable");
    Check(f.counts.found == 5 && f.counts.decrypted == 2 && f.counts.skipped == 2 && f.counts.unreadable == 1, "Firefox counts");
    const auto* aes = Find(f.logins, "https://zen.example.test");
    const auto* des = Find(f.logins, "https://legacy.example.test");
    Check(aes && aes->username == "zoe" && aes->password == "aes-secret", "AES-256-CBC login");
    Check(des && des->username == "yuri" && des->password == "des-\xc3\xabxtra", "3DES login");
    Check(ReadFirefoxLogins(kFixtures / "firefox-primary", "").status == FirefoxStatus::PrimaryPasswordRequired, "primary password detected");
    Check(ReadFirefoxLogins(kFixtures / "firefox-primary", "wrong").status == FirefoxStatus::WrongPrimaryPassword, "wrong primary password");
    const auto p = ReadFirefoxLogins(kFixtures / "firefox-primary", "correct horse");
    Check(p.status == FirefoxStatus::Ok && p.counts.decrypted == 2, "primary password unlocks");
    Check(ReadFirefoxLogins(kFixtures / "chromium", "").status == FirefoxStatus::NoLogins, "no logins.json");
    // Older NSS SHA1/3DES PBE key wrapping (3DES-only logins; the AES ones stay unreadable).
    Check(ReadFirefoxLogins(kFixtures / "firefox-legacy", "").status == FirefoxStatus::PrimaryPasswordRequired, "legacy PBE: primary password detected");
    const auto legacy = ReadFirefoxLogins(kFixtures / "firefox-legacy", "legacy pass");
    const auto* old_des = Find(legacy.logins, "https://legacy.example.test");
    Check(legacy.status == FirefoxStatus::Ok && old_des && old_des->password == "des-ëxtra", "legacy PBE unlocks 3DES logins");
  }
  {
    const auto c = ParsePasswordCsv(File(kFixtures / "exports" / "chrome.csv"));
    Check(c.error.empty() && c.counts.found == 5 && c.counts.decrypted == 2 && c.counts.skipped == 2 && c.counts.unreadable == 1, "Chrome CSV counts");
    Check(c.logins.size() == 2 && c.logins[0].password == "p,w\"1" && c.logins[1].password == "line1\nline2", "CSV quoting");
    const auto f = ParsePasswordCsv(File(kFixtures / "exports" / "firefox.csv"));
    Check(f.error.empty() && f.logins.size() == 1 && f.logins[0].url == "https://ff.example.test" && f.logins[0].username == "fay", "Firefox CSV");
    Check(!ParsePasswordCsv(File(kFixtures / "exports" / "broken.csv")).error.empty(), "unterminated quote refused");
    Check(!ParsePasswordCsv(File(kFixtures / "exports" / "notes.csv")).error.empty(), "non-password CSV refused");
    Check(!ParsePasswordCsv("").error.empty(), "empty CSV refused");
  }
  {
    const auto h = ParseBookmarksHtml(File(kFixtures / "exports" / "bookmarks.html"));
    Check(h.error.empty() && h.urls == 3, "bookmarks HTML: three links");
    std::string nested_parent, folder_id;
    for (const auto& x : h.items) { if (x.title == "Nested") folder_id = x.id; if (x.url == "https://nested.example.test/") nested_parent = x.parent; }
    Check(!folder_id.empty() && nested_parent == folder_id, "bookmarks HTML nesting");
    bool unescaped = false; for (const auto& x : h.items) unescaped |= x.url == "https://arc.example.test/?a=1&b=2" && x.title == "Arc <one>";
    Check(unescaped, "bookmarks HTML entities");
    Check(!ParseBookmarksHtml("<html></html>").error.empty(), "non-bookmark HTML refused");
  }
  {  // Site icons.
    Check(UrlHost("https://Docs.Example.test:8443/a?b") == "docs.example.test" && UrlHost("https://u@x.test/").empty() && UrlHost("ftp://x.test/").empty(), "icon hosts");
    const auto chrome = ReadChromiumFavicons(kFixtures / "chromium" / "Favicons", {"docs.example.test", "tracker.example.test"});
    Check(chrome.size() == 1 && chrome.contains("docs.example.test"), "Chromium icons: only requested hosts, PNG only");
    const auto docs = chrome.find("docs.example.test");
    Check(docs != chrome.end() && docs->second.starts_with("\x89PNG") && docs->second.find("IHDR\0\0\0\x20", 0, 8) != std::string::npos, "32 px bitmap preferred");
    const auto zen = ReadFirefoxFavicons(kFixtures / "firefox" / "favicons.sqlite", {"zen.example.test", "root.example.test", "absent.example.test"});
    Check(zen.size() == 2, "Firefox icons: page icon + root favicon.ico");
    const auto ico = zen.find("zen.example.test");
    Check(ico != zen.end() && ico->second.starts_with("\x89PNG") && ico->second.find("IHDR\0\0\0\x20", 0, 8) != std::string::npos, "PNG extracted from ICO, 32 px");
    Check(!IconPng("GIF89a").has_value() && !IconPng(std::string("\0\0\1\0\1\0", 6)).has_value(), "unsupported icon bytes refused");
    Check(ReadChromiumFavicons(kFixtures / "chromium" / "missing", {"docs.example.test"}).empty(), "absent icon DB");
  }
  {  // Tree for the workspace import.
    TreeInput in; in.source = "c:\\profile|tabs,bookmarks"; in.label = "Chrome";
    in.tabs = ReadChromiumSession(kFixtures / "chromium").tabs;
    in.bookmarks = ParseChromiumBookmarks(File(kFixtures / "chromium" / "Bookmarks")).items;
    const auto out = BuildImportTree(in);
    Check(out.error.empty() && out.tabs == 2 && out.bookmarks == 3 && out.tree.spaces.size() == 1 && out.tree.spaces[0].name == "Chrome", "import tree built");
    Check(red::TreeStore::ValidateSnapshot(out.tree), "import tree valid");
    Check(BuildImportTree(in).tree.nodes == out.tree.nodes, "import ids stable");
    in.source += "x"; Check(BuildImportTree(in).tree.spaces[0].id != out.tree.spaces[0].id, "different source, different ids");
  }
  {
    std::string secret = "hunter2"; Wipe(secret); Check(secret.empty(), "wipe clears");
  }
  if (failures) std::fprintf(stderr, "%d failure(s)\n", failures); else std::puts("browser_import_test: OK");
  return failures ? 1 : 0;
}
