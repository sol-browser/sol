#include "app/release_notes.h"

#include <windows.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

namespace {
int failures = 0;
void Check(bool ok, const char* what) {
  if (!ok) { std::printf("FAIL: %s\n", what); ++failures; }
}

std::string Marker(const std::filesystem::path& dir) {
  std::ifstream in(dir / "last-version");
  std::string text;
  std::getline(in, text);
  return text;
}

void WriteMarker(const std::filesystem::path& dir, const char* text) {
  std::filesystem::create_directories(dir);
  std::ofstream(dir / "last-version") << text << '\n';
}

std::filesystem::path FreshDir(const char* name) {
  wchar_t temp[MAX_PATH];
  GetTempPathW(MAX_PATH, temp);
  auto dir = std::filesystem::path(temp) / (L"sol-release-notes-test-" + std::to_wstring(GetCurrentProcessId())) / name;
  std::error_code error;
  std::filesystem::remove_all(dir, error);
  return dir;
}
}  // namespace

int main() {
  using red::CompareVersions;
  using red::ReleaseNotesAfterUpdate;
  using red::ReleaseNotesUrl;

  // Version order, numerically per part.
  Check(CompareVersions("0.2.0", "0.2.0") == 0, "equal versions");
  Check(CompareVersions("0.1.5", "0.2.0") == -1, "older minor");
  Check(CompareVersions("0.2.10", "0.2.9") == 1, "10 is newer than 9");
  Check(CompareVersions("1.0", "1.0.0") == 0, "missing parts count as zero");
  Check(!CompareVersions("0.2.0-dev", "0.2.0"), "labels are not versions");
  Check(!CompareVersions("", "0.2.0"), "empty is not a version");
  Check(!CompareVersions("0..2", "0.2.0"), "empty part is not a version");

  // A downloaded update is replaced only by a later release (0.1.3 had 0.2.0 waiting when 0.3.0 came out).
  Check(red::UpdateReplacesWaiting("0.2.0", ""), "nothing waits: download");
  Check(!red::UpdateReplacesWaiting("0.2.0", "0.2.0"), "the waiting version is not downloaded again");
  Check(red::UpdateReplacesWaiting("0.3.0", "0.2.0"), "a later release replaces the waiting one");
  Check(!red::UpdateReplacesWaiting("0.2.0", "0.3.0"), "an older feed version does not replace a later one");
  Check(red::UpdateReplacesWaiting("0.3.0-rc", "0.2.0"), "an incomparable version counts as different");

  // Website anchors use dashes.
  Check(ReleaseNotesUrl("0.2.1") == "https://sol-browser.app/changelog/#v0-2-1", "site URL with the 0.2.1 anchor");

  // A new profile is welcomed instead; the version is remembered.
  {
    const auto dir = FreshDir("fresh");
    Check(!ReleaseNotesAfterUpdate(dir, "0.3.0", true), "fresh profile opens nothing");
    Check(Marker(dir) == "0.3.0", "fresh profile remembers the version");
  }
  // An existing profile without a marker ran an older Sol: show the notes once.
  {
    const auto dir = FreshDir("legacy");
    const auto url = ReleaseNotesAfterUpdate(dir, "0.3.0", false);
    Check(url && url->ends_with("#v0-3-0"), "profile from before the marker gets the notes");
    Check(!ReleaseNotesAfterUpdate(dir, "0.3.0", false), "only once");
  }
  // A real update.
  {
    const auto dir = FreshDir("update");
    WriteMarker(dir, "0.2.0");
    const auto url = ReleaseNotesAfterUpdate(dir, "0.2.1", false);
    Check(url && url->ends_with("#v0-2-1"), "update opens the new version's notes");
    Check(Marker(dir) == "0.2.1", "update remembers the new version");
    Check(!ReleaseNotesAfterUpdate(dir, "0.2.1", false), "the next start opens nothing");
  }
  // Same version, downgrade and development builds.
  {
    const auto dir = FreshDir("same");
    WriteMarker(dir, "0.2.1");
    Check(!ReleaseNotesAfterUpdate(dir, "0.2.1", false), "same version opens nothing");
  }
  {
    const auto dir = FreshDir("downgrade");
    WriteMarker(dir, "0.3.0");
    Check(!ReleaseNotesAfterUpdate(dir, "0.2.1", false), "downgrade opens nothing");
    Check(Marker(dir) == "0.2.1", "downgrade remembers the older version");
    const auto url = ReleaseNotesAfterUpdate(dir, "0.3.0", false);
    Check(url.has_value(), "updating again after a downgrade shows the notes again");
  }
  {
    const auto dir = FreshDir("dev");
    WriteMarker(dir, "0.2.0");
    Check(!ReleaseNotesAfterUpdate(dir, "0.0.0-dev", false), "a development label opens nothing");
    Check(Marker(dir) == "0.2.0", "a development label leaves the marker alone");
  }
  {
    const auto dir = FreshDir("garbage");
    WriteMarker(dir, "not a version");
    Check(!ReleaseNotesAfterUpdate(dir, "0.2.1", false), "an unreadable marker opens nothing");
    Check(Marker(dir) == "0.2.1", "an unreadable marker is replaced");
  }

  std::error_code error;
  std::filesystem::remove_all(FreshDir("fresh").parent_path(), error);
  if (failures) return 1;
  std::printf("release_notes: all checks passed\n");
  return 0;
}
