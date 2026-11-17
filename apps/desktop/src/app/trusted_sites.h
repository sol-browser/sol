#pragma once
#include <algorithm>
#include <sstream>
#include <string>
#include <vector>

namespace red {
// Sites where Sol's ad blocker is off: uBlock Origin's trusted-site list, one
// lower-case hostname per line (app/content_blocker_settings.h).
inline bool ValidTrustedSite(const std::string& site) {
  if (site.empty() || site.size() > 253 || site.front() == '.' || site.back() == '.' || site.find("..") != std::string::npos) return false;
  return std::all_of(site.begin(), site.end(), [](unsigned char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '-'; });
}
// |list| with |host| added (trusted) or removed, keeping the other entries.
inline std::string WithTrustedSite(const std::string& list, const std::string& host, bool trusted) {
  std::vector<std::string> lines; std::istringstream input(list); std::string line;
  bool present = false;
  while (std::getline(input, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line == host) { if (!trusted || present) continue; present = true; }
    if (!line.empty()) lines.push_back(line);
  }
  if (trusted && !present) lines.push_back(host);
  std::string out;
  for (const auto& entry : lines) out += entry + "\n";
  return out;
}
}  // namespace red
