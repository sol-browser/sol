#pragma once
#include <algorithm>
#include "include/internal/cef_types_wrappers.h"

namespace red {
struct IntegrationCardPlacement {
  CefRect bounds;
  float origin_x, origin_y;
};
// Anchor coordinates are relative to the owner's client area, in DIP.
inline IntegrationCardPlacement PlaceIntegrationCard(const CefRect& root, const CefRect& anchor, int width,
                                                     int height, int top, bool favorite) {
  const auto position = [](int before, int after, int size, int min, int max) {
    const int limit = std::max(min, max - size);
    if (after >= min && after <= limit) return after;
    if (after > limit && before - size >= min) return before - size;
    return std::clamp(after, min, limit);
  };
  const int right = anchor.x + anchor.width, bottom = anchor.y + anchor.height;
  const int gap = favorite ? 0 : 8;
  const int x = position(anchor.x - gap, right + gap, width, 8, root.width - 8);
  const bool left = x + width <= anchor.x - gap, beside = x >= right + gap || left;
  const bool aligned = !favorite && beside;
  const float row_center = anchor.y + anchor.height / 2.f;
  const int y = aligned ? std::clamp(anchor.y + (anchor.height - height) / 2, top, std::max(top, root.height - 8 - height)) :
      position(anchor.y - gap, bottom + gap, height, top, root.height - 8);
  const float pivot_x = left ? static_cast<float>(anchor.x) : beside || favorite ? static_cast<float>(right) : anchor.x + anchor.width / 2.f;
  const float pivot_y = aligned ? row_center : static_cast<float>(y + height <= anchor.y ? anchor.y : bottom);
  return {{root.x + x, root.y + y, width, height}, std::clamp((pivot_x - x) / width, 0.f, 1.f),
      std::clamp((pivot_y - y) / height, 0.f, 1.f)};
}
}  // namespace red
