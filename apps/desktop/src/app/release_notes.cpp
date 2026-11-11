#include "app/release_notes.h"

#include <fstream>
#include <system_error>
#include <vector>

namespace red {
namespace {
// The website's changelog (repository sol-browser/landing). Release anchors are v<major>-<minor>-<patch>.
constexpr std::string_view kChangelogPage = "https://sol-browser.app/changelog/";
constexpr char kMarker[] = "last-version";

std::optional<std::vector<long>> Parts(std::string_view version) {
  std::vector<long> parts;
  if (version.empty()) return std::nullopt;
  long value = 0;
  bool digits = false;
  for (const char c : version) {
    if (c >= '0' && c <= '9') {
      if (value > 100000) return std::nullopt;
      value = value * 10 + (c - '0');
      digits = true;
    } else if (c == '.' && digits) {
      parts.push_back(value);
      value = 0;
      digits = false;
    } else {
      return std::nullopt;
    }
  }
  if (!digits) return std::nullopt;
  parts.push_back(value);
  return parts;
}

std::string ReadMarker(const std::filesystem::path& file) {
  std::ifstream in(file, std::ios::binary);
  std::string text;
  if (in) std::getline(in, text);
  while (!text.empty() && (text.back() == '\r' || text.back() == ' ' || text.back() == '\n')) text.pop_back();
  return text.size() <= 32 ? text : std::string();
}

void WriteMarker(const std::filesystem::path& directory, std::string_view version) {
  std::error_code error;
  std::filesystem::create_directories(directory, error);
  if (error) return;
  // Write then rename, so a crash never leaves a half-written marker behind.
  const auto file = directory / kMarker;
  const auto temp = directory / (std::string(kMarker) + ".tmp");
  {
    std::ofstream out(temp, std::ios::binary | std::ios::trunc);
    if (!out) return;
    out << version << '\n';
    if (!out) return;
  }
  std::filesystem::rename(temp, file, error);
  if (error) std::filesystem::remove(temp, error);
}
}  // namespace

std::string ReleaseNotesUrl(std::string_view version) {
  std::string anchor(version);
  for (auto& c : anchor) if (c == '.') c = '-';
  return std::string(kChangelogPage) + "#v" + anchor;
}

std::optional<int> CompareVersions(std::string_view a, std::string_view b) {
  const auto left = Parts(a), right = Parts(b);
  if (!left || !right) return std::nullopt;
  const size_t size = left->size() > right->size() ? left->size() : right->size();
  for (size_t i = 0; i < size; ++i) {
    const long l = i < left->size() ? (*left)[i] : 0, r = i < right->size() ? (*right)[i] : 0;
    if (l != r) return l < r ? -1 : 1;
  }
  return 0;
}

bool UpdateReplacesWaiting(std::string_view latest, std::string_view waiting) {
  if (waiting.empty()) return true;
  if (latest == waiting) return false;
  const auto order = CompareVersions(latest, waiting);
  return !order || *order > 0;
}

std::optional<std::string> ReleaseNotesAfterUpdate(const std::filesystem::path& browser_data,
                                                   std::string_view current_version, bool fresh_profile) {
  if (!Parts(current_version)) return std::nullopt;  // Development label: leave the marker alone.
  const std::string previous = ReadMarker(browser_data / kMarker);
  if (previous == current_version) return std::nullopt;
  WriteMarker(browser_data, current_version);
  if (fresh_profile) return std::nullopt;
  // No marker on an existing profile: it ran a version from before this marker, so it updated.
  if (previous.empty()) return ReleaseNotesUrl(current_version);
  const auto order = CompareVersions(previous, current_version);
  if (!order || *order >= 0) return std::nullopt;  // Unknown or a downgrade.
  return ReleaseNotesUrl(current_version);
}
}  // namespace red
