#pragma once
#include <algorithm>
#include <string>
#include <vector>
namespace red {
struct SiteSearch { std::string name, keyword, url; bool enabled = true; };
inline bool ValidSiteSearchActivation(const std::string& value) { return value == "spaceOrTab" || value == "tab"; }
inline std::vector<SiteSearch> DefaultSiteSearches() {
  return {{"YouTube", "yt", "https://www.youtube.com/results?search_query=%s"},
          {"Wikipedia", "wiki", "https://en.wikipedia.org/w/index.php?search=%s"},
          {"GitHub", "gh", "https://github.com/search?q=%s"}};
}
// Callers also parse the destination using CEF before accepting configuration.
inline bool ValidSiteSearchFields(const SiteSearch& site) {
  if (site.name.empty() || site.name.size() > 80 || site.name.find_first_not_of(' ') == std::string::npos ||
      std::any_of(site.name.begin(), site.name.end(), [](unsigned char c) { return c < 32 || c == 127; }) ||
      site.keyword.empty() || site.keyword.size() > 64) return false;
  const auto letter = [](unsigned char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'); };
  if ((!letter(site.keyword[0]) && site.keyword[0] != '@') || std::any_of(site.keyword.begin(), site.keyword.end(), [&](unsigned char c) {
    return !letter(c) && c != '_' && c != '-' && c != '.' && c != '@';
  })) return false;
  const auto& url = site.url;
  const auto token = url.find("%s"), end = url.find('/', url.find("://") + 3);
  if (url.size() > 2048 || (!url.starts_with("https://") && !url.starts_with("http://")) ||
      token == std::string::npos || end == std::string::npos || token <= end || url.find("%s", token + 2) != std::string::npos ||
      std::any_of(url.begin(), url.end(), [](unsigned char c) { return c < 33 || c > 126 || c == '\\'; })) return false;
  const auto hex = [](unsigned char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); };
  for (size_t i = 0; i < url.size(); ++i) if (url[i] == '%') {
    if (i == token) { ++i; continue; }
    if (i + 2 >= url.size() || !hex(url[i+1]) || !hex(url[i+2])) return false;
    i += 2;
  }
  return true;
}
}  // namespace red
