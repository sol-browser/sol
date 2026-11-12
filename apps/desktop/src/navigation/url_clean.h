#pragma once
#include <algorithm>
#include <array>
#include <cctype>
#include <string>
#include <string_view>
#include "web_url.h"

namespace red {
// "Super Copy" (Ctrl+Shift+C): the page address without the parameters that only identify who
// shared or clicked a link. The list is conservative: names that tell people or ad networks apart and
// that no website needs to show the same page. Generic names (ref, source, id, si, t) are never removed.
inline bool IsTrackingParameter(std::string_view name) {
  std::string lower(name);
  std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  for (const std::string_view prefix : {"utm_", "hsa_", "pk_", "mtm_", "piwik_"})
    if (lower.starts_with(prefix)) return true;
  static constexpr std::array<std::string_view, 29> names = {
      "fbclid", "gclid", "gclsrc", "dclid", "gbraid", "wbraid", "gad_source", "gad_campaignid", "msclkid", "twclid", "ttclid", "li_fat_id",
      "igshid", "igsh", "yclid", "mc_cid", "mc_eid", "_hsenc", "_hsmi", "mkt_tok", "vero_id", "wickedid", "oly_enc_id", "oly_anon_id",
      "_ga", "_gl", "irclickid", "ref_src", "s_kwcid"};
  return std::find(names.begin(), names.end(), lower) != names.end();
}

// Only http(s) addresses change. The fragment, the order and the spelling of the other parameters stay.
inline std::string StripTrackingParameters(std::string_view url, bool* changed = nullptr) {
  if (changed) *changed = false;
  const bool web = HasWebScheme(url);
  const auto query = url.find('?');
  if (!web || query == std::string_view::npos) return std::string(url);
  const auto hash = url.find('#');
  if (hash != std::string_view::npos && hash < query) return std::string(url);
  const auto end = hash == std::string_view::npos ? url.size() : hash;
  std::string kept;
  bool removed = false;
  for (size_t at = query + 1; at <= end;) {
    auto next = url.find('&', at);
    if (next == std::string_view::npos || next > end) next = end;
    const auto part = url.substr(at, next - at);
    const auto name = part.substr(0, part.find('='));
    if (!part.empty()) {
      if (IsTrackingParameter(name)) removed = true;
      else { if (!kept.empty()) kept += '&'; kept += part; }
    }
    at = next + 1;
  }
  if (!removed) return std::string(url);
  if (changed) *changed = true;
  std::string result(url.substr(0, query));
  if (!kept.empty()) result += '?' + kept;
  if (end < url.size()) result += url.substr(end);
  return result;
}
}  // namespace red
