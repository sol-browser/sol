#pragma once
#include <memory>
#include <windows.h>
#include "windows/page_corners.h"

namespace red {
// Non-activating composition surface for popovers above a separate owned
// sidebar HWND. Owned by the browser, never globally topmost or interactive.
class PopupAnimationHost {
 public:
  ~PopupAnimationHost();
  bool Begin(HWND owner, HWND popup, bool bottom, float origin_x, bool closing, float from_scale);
  bool Active(HWND popup) const { return popup && source_ == popup; }
  bool FadeShadow(HWND popup, int duration_ms);
  void End(HWND popup = nullptr);
 private:
  HWND host_ = nullptr, source_ = nullptr;
  std::unique_ptr<PageCorners> composition_;
};
}  // namespace red
