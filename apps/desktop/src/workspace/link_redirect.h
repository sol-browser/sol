#pragma once
#include <algorithm>
#include <array>
#include <cctype>
#include <string>
#include <string_view>
#include "web_url.h"

namespace red {
// The address an out-link redirector on the link's own site leads to.
// YouTube description links are youtube.com/redirect?event=...&q=<address>; the
// clicked URL is on the source's own site and only the server's redirect reaches
// the other site. Link Peek decides from the clicked URL, so it reads the
// destination from the query. The parameter names are the ones redirectors use for
// the destination; login return addresses (next, continue, return_to, redirect_uri)
// are deliberately absent. A UI heuristic only: what loads is the clicked URL, so
// the site's own redirect (or its "leaving this site" page) still runs.
inline std::string LinkRedirectTarget(std::string_view url) {
  if (!HasWebScheme(url) || url.size() > 4096) return {};
  const auto query = url.find('?');
  if (query == std::string_view::npos) return {};
  const auto hash = url.find('#');
  if (hash != std::string_view::npos && hash < query) return {};
  const auto end = hash == std::string_view::npos ? url.size() : hash;
  static constexpr std::array<std::string_view, 11> names = {
      "q", "url", "u", "to", "target", "dest", "destination", "link", "out", "away", "goto"};
  auto hex = [](char c) {
    return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
  };
  for (size_t at = query + 1; at < end;) {
    auto next = url.find('&', at);
    if (next == std::string_view::npos || next > end) next = end;
    const auto part = url.substr(at, next - at);
    at = next + 1;
    const auto equals = part.find('=');
    if (equals == std::string_view::npos) continue;
    std::string name(part.substr(0, equals));
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (std::find(names.begin(), names.end(), name) == names.end()) continue;
    const auto raw = part.substr(equals + 1);
    std::string value;
    value.reserve(raw.size());
    for (size_t i = 0; i < raw.size(); ++i) {
      if (raw[i] == '%' && i + 2 < raw.size() && hex(raw[i + 1]) >= 0 && hex(raw[i + 2]) >= 0) {
        value += static_cast<char>(hex(raw[i + 1]) * 16 + hex(raw[i + 2]));
        i += 2;
      } else {
        value += raw[i];
      }
    }
    if (HasWebScheme(value)) return value;
  }
  return {};
}
}  // namespace red
