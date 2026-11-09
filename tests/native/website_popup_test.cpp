#include "windows/website_popup.h"

#include <cstdio>

namespace {
int failures = 0;
void Check(bool ok, const char* what) {
  if (!ok) { std::printf("FAIL: %s\n", what); ++failures; }
}
bool Same(const CefRect& a, int x, int y, int w, int h) { return a.x == x && a.y == y && a.width == w && a.height == h; }
}  // namespace

int main() {
  using red::WebsitePopupBounds;
  using red::WebsitePopupContentSize;
  const CefSize screen(1920, 1040);

  // Content size: the page's request, or the default.
  {
    const auto s = WebsitePopupContentSize(true, 600, true, 700, screen);
    Check(s.width == 600 && s.height == 700, "requested size is kept");
    const auto d = WebsitePopupContentSize(false, 0, false, 0, screen);
    Check(d.width == red::kWebsitePopupWidth && d.height == red::kWebsitePopupHeight, "default size");
    const auto tiny = WebsitePopupContentSize(true, 10, true, 5, screen);
    Check(tiny.width == red::kWebsitePopupMinimum && tiny.height == red::kWebsitePopupMinimum, "tiny requests grow to the minimum");
    const auto huge = WebsitePopupContentSize(true, 9000, true, 9000, screen);
    Check(huge.width == 1920 && huge.height == 1040, "huge requests stop at the work area");
  }

  // Placement: centred over Sol on Sol's monitor, even when that is the second monitor.
  {
    const CefRect second_monitor(1920, 0, 2560, 1400);
    const CefRect sol(2200, 100, 1600, 1000);
    const auto r = WebsitePopupBounds(sol, second_monitor, CefSize(520, 700));
    Check(Same(r, 2200 + (1600 - 520) / 2, 100 + (1000 - 700) / 2, 520, 700), "centred over Sol on the second monitor");
  }
  {
    // Monitor to the left of the primary one (negative coordinates).
    const CefRect left_monitor(-1920, 0, 1920, 1040);
    const CefRect sol(-1800, 50, 1200, 900);
    const auto r = WebsitePopupBounds(sol, left_monitor, CefSize(500, 650));
    Check(r.x >= -1920 && r.x + r.width <= 0 && r.y >= 0, "stays on a monitor with negative coordinates");
  }
  {
    // Sol partly off its monitor: the popup moves back inside the work area.
    const CefRect work(0, 0, 1920, 1040);
    const CefRect sol(1500, 700, 1200, 900);
    const auto r = WebsitePopupBounds(sol, work, CefSize(500, 650));
    Check(r.x + r.width <= 1920 && r.y + r.height <= 1040, "kept inside the work area");
    Check(r.x >= 0 && r.y >= 0, "never above or left of the work area");
  }
  {
    // Larger than the work area: fills it.
    const CefRect work(0, 0, 1280, 680);
    const auto r = WebsitePopupBounds(CefRect(0, 0, 1280, 680), work, CefSize(2000, 2000));
    Check(Same(r, 0, 0, 1280, 680), "fills a small work area");
  }

  if (failures) return 1;
  std::printf("website_popup: all checks passed\n");
  return 0;
}
