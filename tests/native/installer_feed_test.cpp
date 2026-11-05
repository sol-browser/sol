// The installer's reading of a GitHub release (apps/installer/src/feed.h).
#include "feed.h"
#include <cstdio>

namespace {
int failures = 0;
void check(bool ok, const char* name) {
  if (!ok) { std::fprintf(stderr, "FAIL: %s\n", name); ++failures; }
}
using sol_installer::Version;
}  // namespace

int main() {
  using namespace sol_installer;
  // Versions.
  check(ParseVersion("0.1.1") == Version{0, 1, 1}, "plain version");
  check(ParseVersion("12.0.345") == Version{12, 0, 345}, "multi-digit version");
  for (const char* bad : {"", "1", "1.2", "1.2.3.4", "01.2.3", "1.2.x", "v1.2.3", "1.2.3-beta", " 1.2.3", "1..3", "123456.0.0"})
    check(!ParseVersion(bad), bad);
  check(Version{0, 2, 0} > Version{0, 1, 9}, "minor beats patch");
  check(ToString({1, 20, 3}) == "1.20.3", "version text");

  // Package ids name a folder: letters and digits only.
  check(ValidPackageId("SolBrowser"), "product id");
  for (const char* bad : {"", "..", "Sol Browser", "Sol\\x", "Sol/x", "a.b"}) check(!ValidPackageId(bad), bad);

  // The feed as vpk 1.2.161 writes it (release 0.1.1).
  const char* real = R"({"Assets":[{"PackageId":"SolBrowser","Version":"0.1.1","Type":"Full","FileName":"SolBrowser-0.1.1-full.nupkg","SHA1":"E723185B1B893A4143F2DEC5E6DC13C398278BF5","SHA256":"B845AB689195F81C05CE011EE5647B7BBF02A277B9B5A4D1EE2509B7D5B93733","Size":196090229}]})";
  const auto release = LatestFullRelease(real);
  check(release && release->package_id == "SolBrowser" && release->version == Version{0, 1, 1}, "real feed");

  // Several versions, deltas and notes: the highest full package wins.
  const char* mixed = R"( { "Assets" : [
    {"PackageId":"SolBrowser","Version":"0.1.1","Type":"Full","NotesMarkdown":"Fixes \"quotes\", {braces} and é"},
    {"PackageId":"SolBrowser","Version":"0.2.0","Type":"Delta"},
    {"PackageId":"SolBrowser","Version":"0.1.10","Type":"Full","Size":1.5e3,"Extra":[null,true,false,{}]},
    {"PackageId":"SolBrowser","Version":"bad","Type":"Full"}
  ] } )";
  const auto latest = LatestFullRelease(mixed);
  check(latest && latest->version == Version{0, 1, 10}, "highest full version, deltas ignored");

  for (const char* bad : {"", "[]", "{}", R"({"Assets":{}})", R"({"Assets":[]})", R"({"Assets":[{"Type":"Full"}]})",
                          R"({"Assets":[{"PackageId":"../x","Version":"1.0.0","Type":"Full"}]})",
                          R"({"Assets":[{"PackageId":"A","Version":"1.0.0","Type":"Full"},{"PackageId":"B","Version":"2.0.0","Type":"Full"}]})",
                          R"({"Assets":[{"PackageId":"A","Version":"1.0.0","Type":"Full"}]} trailing)",
                          R"({"Assets":[{"PackageId":"A","Version":"1.0.0","Type":"Full",}]})", "{\"Assets\":[{\"PackageId\":\"A\nB\"}]}"})
    check(!LatestFullRelease(bad), bad);
  std::string deep;
  for (int i = 0; i < 100; ++i) deep += "[";
  check(!LatestFullRelease(deep), "deep nesting is refused");

  // The checksum file as pack_release.ps1 writes it.
  const char* sums =
      "8ed406de07795ebecb92e4fe8f096c486007bc8263aa0987afac76dc8978ae0c  releases.win.json\r\n"
      "298D7ED44FA359C08DEABC27BD0EA52976436965863EEABB4F259A40108E19DF  Sol-Setup-0.1.1.exe\r\n"
      "b845ab689195f81c05ce011ee5647b7bbf02a277b9b5a4d1ee2509b7d5b93733  SolBrowser-0.1.1-full.nupkg\r\n";
  const auto setup = SetupChecksum(sums, {0, 1, 1});
  check(setup && setup->file == "Sol-Setup-0.1.1.exe" &&
            setup->sha256 == "298d7ed44fa359c08deabc27bd0ea52976436965863eeabb4f259a40108e19df", "setup checksum, lowercased");
  check(!SetupChecksum(sums, {0, 1, 2}), "other version has no line");
  check(!SetupChecksum("298d7ed4  Sol-Setup-0.1.1.exe\n", {0, 1, 1}), "short hash");
  check(!SetupChecksum("zz8d7ed44fa359c08deabc27bd0ea52976436965863eeabb4f259a40108e19df  Sol-Setup-0.1.1.exe\n", {0, 1, 1}), "non-hex hash");
  check(!SetupChecksum("298d7ed44fa359c08deabc27bd0ea52976436965863eeabb4f259a40108e19df  ..\\Sol-Setup-0.1.1.exe\n", {0, 1, 1}), "path is refused");

  if (!failures) std::puts("installer feed: all checks passed");
  return failures ? 1 : 0;
}
