#pragma once
// Page zoom without Chrome's command hook.
//
// Engine 158 no longer calls CefCommandHandler::OnChromeCommand for Chrome-style
// page browsers (IDC_ZOOM_PLUS/MINUS/NORMAL included; docs/cef-patches.md #18),
// so Chrome zoomed pages itself and Sol's zoom toast and address-bar badge never
// appeared. Sol now handles Chrome's zoom keys in its keyboard handler, before
// Chrome's accelerator, and watches Ctrl+wheel: Chrome still zooms the page
// (only when the page did not take the wheel itself), and Sol shows the level
// once it changed. Pure logic here; tests/native/page_zoom_test.cpp.
#include <cmath>
#include <map>
#include <optional>

namespace red {

enum class PageZoomKey { In, Out, Reset };

// Chrome's zoom accelerators (chrome/browser/ui/views/accelerator_table.cc):
// Ctrl+= / Ctrl+Shift+= / Ctrl+numpad +, Ctrl+- / Ctrl+Shift+- / Ctrl+numpad -,
// Ctrl+0 / Ctrl+numpad 0. Never with Alt (AltGr arrives as Ctrl+Alt).
inline std::optional<PageZoomKey> PageZoomKeyFor(int key, bool ctrl, bool shift, bool alt) {
  if (!ctrl || alt) return std::nullopt;
  constexpr int kOemPlus = 0xBB, kOemMinus = 0xBD, kAdd = 0x6B, kSubtract = 0x6D, kNumpad0 = 0x60;
  if (key == kOemPlus || (!shift && key == kAdd)) return PageZoomKey::In;
  if (key == kOemMinus || (!shift && key == kSubtract)) return PageZoomKey::Out;
  if (!shift && (key == '0' || key == kNumpad0)) return PageZoomKey::Reset;
  return std::nullopt;
}

// Chrome's zoom level is a power of 1.2: level 1 is 120%.
inline int PageZoomPercent(double level) {
  return static_cast<int>(std::lround(100 * std::pow(1.2, level)));
}

// The address bar's badge: the percent when the page is not at 100%.
inline std::optional<int> PageZoomBadge(double level) {
  const int percent = PageZoomPercent(level);
  if (percent == 100 || percent <= 0 || percent > 1000) return std::nullopt;
  return percent;
}

// Ctrl+wheel over a page: the level before Chrome handled the wheel, then
// checks after it. A change is reported once per new level, so a burst of
// wheel steps shows the level it reached and never repeats it.
class PageZoomWatch {
 public:
  // A Ctrl+wheel reached page |id| at |level|. Keeps the level it started from
  // while earlier checks for this page are still pending.
  void Begin(int id, double level) {
    auto& watch = pages_[id];
    if (watch.pending == 0) watch.shown = level;
    ++watch.pending;
  }
  // A check: true when |level| differs from what was last shown for |id|.
  bool Changed(int id, double level) {
    const auto watch = pages_.find(id);
    if (watch == pages_.end() || Same(watch->second.shown, level)) return false;
    watch->second.shown = level;
    return true;
  }
  // The last check of one Begin.
  void End(int id) {
    const auto watch = pages_.find(id);
    if (watch != pages_.end() && --watch->second.pending <= 0) pages_.erase(watch);
  }
  // Sol showed |level| itself (keys, the badge, the old command route): a
  // pending wheel check must not show it again.
  void Shown(int id, double level) {
    if (const auto watch = pages_.find(id); watch != pages_.end()) watch->second.shown = level;
  }
  bool Watching(int id) const { return pages_.contains(id); }

 private:
  static bool Same(double a, double b) { return std::abs(a - b) < 1e-6; }
  struct Watch { double shown = 0; int pending = 0; };
  std::map<int, Watch> pages_;
};

}  // namespace red
