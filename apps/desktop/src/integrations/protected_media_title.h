#pragma once

#include <algorithm>
#include <string>
#include <string_view>

namespace red {
inline std::u16string ProtectedMediaTrackLabel(std::u16string_view title, std::u16string_view artist) {
  // The UI snapshot bounds pageTitle to 240 UTF-16 code units.
  constexpr size_t limit = 240;
  const auto prefix = [](std::u16string_view text, size_t length) {
    auto part = text.substr(0, length);
    if (!part.empty() && part.back() >= 0xd800 && part.back() <= 0xdbff) part.remove_suffix(1);
    return part;
  };
  constexpr std::u16string_view whitespace = u" \t\r\n\f\v\u00a0";
  const auto first = artist.find_first_not_of(whitespace);
  if (title.empty() || first == std::u16string_view::npos) return std::u16string(prefix(title, limit));
  artist = artist.substr(first, artist.find_last_not_of(whitespace) - first + 1);
  constexpr std::u16string_view separator = u" — ";
  constexpr size_t text_limit = limit - separator.size();
  // Leave room for both names even if a service returns an unusually long title.
  const auto song = prefix(title, text_limit - std::min(artist.size(), text_limit / 2));
  std::u16string label(song);
  label.append(separator);
  label.append(prefix(artist, text_limit - song.size()));
  return label;
}
}  // namespace red
