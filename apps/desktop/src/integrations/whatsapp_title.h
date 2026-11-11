#pragma once
#include <optional>
#include <string_view>

namespace red {
// WhatsApp Web's title carries its unread count: "(3) WhatsApp" -> 3, "WhatsApp" -> 0, anything else
// (loading, other pages) -> nullopt. Used by the hidden check (integrations/whatsapp_unread.h).
inline std::optional<int> WhatsAppTitleCount(std::string_view title) {
  if (title == "WhatsApp") return 0;
  if (title.size() < 4 || title[0] != '(') return std::nullopt;
  const auto close = title.find(')');
  if (close == std::string_view::npos || close == 1 || close > 10 || title.substr(close) != ") WhatsApp") return std::nullopt;
  int value = 0;
  for (size_t i = 1; i < close; ++i) {
    if (title[i] < '0' || title[i] > '9') return std::nullopt;
    value = value * 10 + (title[i] - '0');
  }
  return value;
}
}  // namespace red
