#pragma once
#include <algorithm>
#include "include/internal/cef_types_wrappers.h"

namespace red {
// Website popups (sign-in with Google, payment windows and other window.open() calls that ask for
// a popup) open as a plain window that belongs to the Sol window: no tabs, no Chrome toolbar, on the
// same monitor as Sol, centred over it.

// Default content size when the page does not ask for one (CSS pixels).
inline constexpr int kWebsitePopupWidth = 500;
inline constexpr int kWebsitePopupHeight = 650;
// Smallest content size Sol accepts from a page (CSS pixels).
inline constexpr int kWebsitePopupMinimum = 240;

// The content size to use, in CSS pixels: the page's request when it made one, kept between the
// minimum and |limit| (the work area, in CSS pixels); the default otherwise.
inline CefSize WebsitePopupContentSize(bool width_set, int width, bool height_set, int height, const CefSize& limit) {
  const int w = width_set ? width : kWebsitePopupWidth;
  const int h = height_set ? height : kWebsitePopupHeight;
  return CefSize(std::clamp(w, kWebsitePopupMinimum, std::max(kWebsitePopupMinimum, limit.width)),
                 std::clamp(h, kWebsitePopupMinimum, std::max(kWebsitePopupMinimum, limit.height)));
}

// Window bounds in screen pixels: |size| (the outer window size) centred over |owner| and moved
// fully inside |work|, the work area of the owner's monitor. Larger than the work area: fills it.
inline CefRect WebsitePopupBounds(const CefRect& owner, const CefRect& work, const CefSize& size) {
  const int width = std::min(std::max(1, size.width), work.width);
  const int height = std::min(std::max(1, size.height), work.height);
  int x = owner.x + (owner.width - width) / 2;
  int y = owner.y + (owner.height - height) / 2;
  x = std::clamp(x, work.x, work.x + work.width - width);
  y = std::clamp(y, work.y, work.y + work.height - height);
  return CefRect(x, y, width, height);
}
}  // namespace red
