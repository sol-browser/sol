#pragma once
// What Sol's small installer reads from a GitHub release (docs/installer.md):
//   releases.win.json   Velopack's feed; the highest full package gives the
//                       package id and version.
//   Sol-<v>.sha256      "<sha256>  <file>" lines; one names Sol-Setup-<v>.exe,
//                       the Velopack setup the installer downloads and runs.
// Pure functions, unit-tested (tests/native/installer_feed_test.cpp).
#include <optional>
#include <string>
#include <string_view>

namespace sol_installer {
struct Version {
  int major = 0, minor = 0, patch = 0;
  friend auto operator<=>(const Version&, const Version&) = default;
};
// "major.minor.patch" with plain decimal parts; nothing else.
std::optional<Version> ParseVersion(std::string_view text);
std::string ToString(const Version& version);

// Letters and digits only, 1 to 64 characters (it names the install folder).
bool ValidPackageId(std::string_view id);

struct Release {
  std::string package_id;
  Version version;
};
// The highest "Full" asset of a Velopack releases.win.json; nullopt when the
// JSON is malformed or lists none.
std::optional<Release> LatestFullRelease(std::string_view json);

struct Checksum {
  std::string file;
  std::string sha256;  // 64 lowercase hex digits.
};
// The line for Sol-Setup-<version>.exe in a "<sha256>  <file>" list.
std::optional<Checksum> SetupChecksum(std::string_view sums, const Version& version);
}  // namespace sol_installer
