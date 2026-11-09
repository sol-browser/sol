#pragma once
#include <string_view>

namespace red {
// Web apps such as Figma and Gmail change the address in place (history.replaceState) and only then
// set the real title: Figma records "Figma" at load, then becomes ".../Abyss-Cup?node-id=…" titled
// "Abyss Cup – Figma". History has no visit for the new address, so the title would be lost.
//
// A title for such an address belongs to the page's recorded visit when it is the same part of the
// site: same scheme and host, and the path equals the visited one or extends it by whole segments.
// The visited path needs at least two segments: on "/watch" or "/search" an in-place change is
// usually a different page (another video, another query), whose title must not rename the visit.
inline bool TitleBelongsToVisit(std::string_view visit, std::string_view current) {
  const auto split = [](std::string_view url, std::string_view& origin, std::string_view& path) {
    const auto scheme = url.find("://");
    if (scheme == url.npos) return false;
    const auto start = url.find_first_of("/?#", scheme + 3);
    origin = url.substr(0, start);
    if (start == url.npos || url[start] != '/') { path = "/"; return true; }
    const auto end = url.find_first_of("?#", start);
    path = url.substr(start, end == url.npos ? url.npos : end - start);
    return true;
  };
  std::string_view visit_origin, visit_path, current_origin, current_path;
  if (visit.empty() || visit == current || !split(visit, visit_origin, visit_path) ||
      !split(current, current_origin, current_path) || visit_origin != current_origin) return false;
  while (visit_path.size() > 1 && visit_path.ends_with('/')) visit_path.remove_suffix(1);
  int segments = 0;
  for (size_t i = 0; i < visit_path.size(); ++i)
    if (visit_path[i] == '/' && i + 1 < visit_path.size() && visit_path[i + 1] != '/') ++segments;
  if (segments < 2) return false;
  return current_path == visit_path ||
         (current_path.starts_with(visit_path) && current_path[visit_path.size()] == '/');
}
}  // namespace red
