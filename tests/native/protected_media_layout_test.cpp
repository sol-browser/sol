#include "integrations/protected_media_layout.h"
#include "integrations/protected_media_title.h"

#include <windows.h>
#include <cstdio>
#include <fstream>
#include <string>

// Where Sol finds the Protected content helper (docs/codecs.md#protected-content-install).
namespace fs = std::filesystem;
namespace {
void Touch(const fs::path& file) {
  fs::create_directories(file.parent_path());
  std::ofstream(file) << "x";
}
void Helper(const fs::path& folder, bool complete = true) {
  Touch(folder / L"SolProtectedMedia.exe");
  Touch(folder / L"resources" / L"app.asar");
  if (complete) Touch(folder / L"SolProtectedMedia.exe.sig");
}
}  // namespace

int main() {
  int failures = 0;
  auto check = [&](bool condition, const char* description) {
    if (!condition) { std::fprintf(stderr, "FAIL: %s\n", description); ++failures; }
  };
  check(red::ProtectedMediaTrackLabel(u"Пісня", u"  Виконавець ") == u"Пісня — Виконавець", "track and artist");
  check(red::ProtectedMediaTrackLabel(u"Track", u" \t") == u"Track", "track without an artist");
  check(red::ProtectedMediaTrackLabel(u"", u"Artist").empty(), "artist alone is not a track title");
  const std::u16string long_title = std::u16string(118, u'x') + u"\U0001f3b5" + std::u16string(118, u'x');
  const auto long_label = red::ProtectedMediaTrackLabel(long_title, std::u16string(240, u'a'));
  check(long_label.size() == 240 && long_label.substr(118, 4) == u" — a", "long labels keep both names without splitting a surrogate pair");
  wchar_t temp[MAX_PATH];
  GetTempPathW(MAX_PATH, temp);
  const fs::path root = fs::path(temp) / (L"sol-protected-media-test-" + std::to_wstring(GetCurrentProcessId()));
  fs::remove_all(root);

  // Velopack: beside "current" (removed with the root on uninstall); otherwise beside the exe.
  const auto velopack = root / L"SolBrowser";
  Touch(velopack / L"Update.exe");
  check(red::ProtectedMediaBase(velopack / L"current") == velopack / L"protected-media", "Velopack root");
  check(red::ProtectedMediaBase(root / L"build" / L"current") == root / L"build" / L"current" / L"protected-media",
        "a folder named current without Update.exe is a development build");
  check(red::ProtectedMediaBase(root / L"Release") == root / L"Release" / L"protected-media", "development build");

  for (const wchar_t* name : {L"45.2.1-1", L"46.0.0-12", L"0.0.0-0"}) check(red::ProtectedMediaVersionName(name), "version name");
  for (const wchar_t* name : {L"", L"45.2.1", L"45.2-1", L"45.2.1-", L"45..1-1", L"45.2.1-1.zip.part", L"../45.2.1-1",
                              L"45.2.1-1 ", L"1234567.0.0-1", L"45.2.1.1"})
    check(!red::ProtectedMediaVersionName(name), "not a version name");

  const auto base = root / L"base";
  check(red::ProtectedMediaRuntime(base, "45.2.1-1").empty(), "nothing installed");
  Helper(base / L"45.2.1-1", false);
  check(red::ProtectedMediaRuntime(base, "45.2.1-1").empty(), "an unsigned or partial folder is not used");
  Helper(base / L"9.0.0-1");
  Helper(base / L"10.0.0-1");
  Helper(base / L"10.0.0-1.unpacking");
  check(red::ProtectedMediaRuntime(base, "45.2.1-1") == base / L"10.0.0-1", "newest older version until the pinned one arrives");
  Helper(base / L"45.2.1-1");
  check(red::ProtectedMediaRuntime(base, "45.2.1-1") == base / L"45.2.1-1", "pinned version");
  Helper(base);
  check(red::ProtectedMediaRuntime(base, "45.2.1-1") == base, "a helper staged by hand wins");

  for (const char* url : {"https://github.com/sol-browser/sol/releases/download/x/y.zip", "http://127.0.0.1:8123/p.zip",
                          "http://localhost/p.zip"})
    check(red::ProtectedMediaSourceAllowed(url), url);
  for (const char* url : {"", "https://", "http://example.com/p.zip", "http://127.0.0.1.example.com/p.zip", "file:///C:/p.zip",
                          "https://user@example.com/p.zip", "https://example.com/a b", "http://localhost.example/p.zip",
                          "ftp://127.0.0.1/p.zip", "https://example.com\\p.zip"})
    check(!red::ProtectedMediaSourceAllowed(url), url);

  // Link Peek leaves a service's whole site to a tab: the domain and its subdomains only.
  for (const char* host : {"netflix.com", "www.netflix.com", "help.netflix.com", "a.b.netflix.com", "x-1.netflix.com"})
    check(red::ProtectedMediaHostInDomain(host, "netflix.com"), host);
  for (const char* host : {"", "netflix.co", "notnetflix.com", "netflix.com.example.org", "evil-netflix.com",
                           ".netflix.com", "a..netflix.com", "netflix.com.", "a_b.netflix.com", "nflxext.com"})
    check(!red::ProtectedMediaHostInDomain(host, "netflix.com"), host);
  check(!red::ProtectedMediaHostInDomain("tv.apple.com", ""), "no site domain matches nothing");
  // Space-separated host lists match whole entries.
  check(red::ProtectedMediaHostListed("play.hbomax.com", "www.hbomax.com play.hbomax.com"), "second host");
  check(red::ProtectedMediaHostListed("www.hbomax.com", "www.hbomax.com"), "only host");
  for (const char* host : {"", "hbomax.com", "play.hbomax", "lay.hbomax.com", "www.hbomax.com play.hbomax.com"})
    check(!red::ProtectedMediaHostListed(host, "www.hbomax.com play.hbomax.com"), host);
  check(!red::ProtectedMediaHostListed("a", ""), "empty list");

  fs::remove_all(root);
  if (failures) return 1;
  std::puts("protected_media_layout: all checks passed");
  return 0;
}
