#pragma once
#include <algorithm>
#include <string>

namespace red {
// CEF supplies the canonical target URL. Keep its path/query, hide credentials
// and prevent control characters from changing the direction of the label.
inline std::wstring LinkStatusText(std::wstring value) {
  const auto scheme = value.find(L"://");
  if (scheme != std::wstring::npos) {
    const auto begin = scheme + 3;
    const auto end = value.find_first_of(L"/?#", begin);
    const auto at = value.rfind(L'@', end == std::wstring::npos ? value.size() : end);
    if (at != std::wstring::npos && at >= begin) value.erase(begin, at - begin + 1);
  }
  std::erase_if(value, [](wchar_t c) {
    return c < 0x20 || c == 0x7f || (c >= 0x202a && c <= 0x202e) || (c >= 0x2066 && c <= 0x2069);
  });
  if (value.size() > 4096) { value.resize(4096); value += L'\x2026'; }
  return value;
}
}  // namespace red
