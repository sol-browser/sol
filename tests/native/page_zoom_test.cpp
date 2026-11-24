#include "navigation/page_zoom.h"
#include <cstdio>

// Page zoom without Chrome's command hook (engine 158): the keys Sol handles,
// the level shown in the toast and the address bar's badge, and the Ctrl+wheel
// watch that shows a level Chrome applied on its own.
namespace {
int failures = 0;
void Expect(bool ok, const char* what) {
  if (!ok) { std::printf("FAIL %s\n", what); ++failures; }
}
bool Is(std::optional<red::PageZoomKey> got, red::PageZoomKey want) { return got && *got == want; }
}  // namespace

int main() {
  using red::PageZoomKey;
  // Chrome's zoom accelerators.
  Expect(Is(red::PageZoomKeyFor(0xBB, true, false, false), PageZoomKey::In), "Ctrl+= zooms in");
  Expect(Is(red::PageZoomKeyFor(0xBB, true, true, false), PageZoomKey::In), "Ctrl+Shift+= (Ctrl++) zooms in");
  Expect(Is(red::PageZoomKeyFor(0x6B, true, false, false), PageZoomKey::In), "Ctrl+numpad + zooms in");
  Expect(Is(red::PageZoomKeyFor(0xBD, true, false, false), PageZoomKey::Out), "Ctrl+- zooms out");
  Expect(Is(red::PageZoomKeyFor(0xBD, true, true, false), PageZoomKey::Out), "Ctrl+Shift+- zooms out");
  Expect(Is(red::PageZoomKeyFor(0x6D, true, false, false), PageZoomKey::Out), "Ctrl+numpad - zooms out");
  Expect(Is(red::PageZoomKeyFor('0', true, false, false), PageZoomKey::Reset), "Ctrl+0 resets");
  Expect(Is(red::PageZoomKeyFor(0x60, true, false, false), PageZoomKey::Reset), "Ctrl+numpad 0 resets");
  // Not zoom keys.
  Expect(!red::PageZoomKeyFor(0xBB, false, false, false), "= alone types");
  Expect(!red::PageZoomKeyFor(0xBB, true, false, true), "Ctrl+Alt+= (AltGr) is not zoom");
  Expect(!red::PageZoomKeyFor('0', true, true, false), "Ctrl+Shift+0 is not zoom");
  Expect(!red::PageZoomKeyFor('1', true, false, false), "Ctrl+1 is a Space, not zoom");
  Expect(!red::PageZoomKeyFor(0x6B, true, true, false), "Ctrl+Shift+numpad + is not zoom");

  // Levels: Chrome's 1.2 power steps.
  Expect(red::PageZoomPercent(0) == 100, "level 0 is 100%");
  Expect(red::PageZoomPercent(0.5227586988632231) == 110, "one step in is 110%");
  Expect(red::PageZoomPercent(1.2239010857415449) == 125, "two wheel steps are 125%");
  Expect(red::PageZoomPercent(-0.5778829311823857) == 90, "one step out is 90%");
  // The badge: hidden at 100%, shown otherwise.
  Expect(!red::PageZoomBadge(0), "no badge at 100%");
  Expect(!red::PageZoomBadge(1e-9), "no badge for a rounding error around 100%");
  Expect(red::PageZoomBadge(1.2239010857415449) == 125, "badge at 125%");
  Expect(red::PageZoomBadge(-0.5778829311823857) == 90, "badge at 90%");
  Expect(!red::PageZoomBadge(40), "no badge for an absurd level");

  {
    // One Ctrl+wheel step: Chrome zooms after the message, the check sees it once.
    red::PageZoomWatch watch;
    watch.Begin(7, 0);
    Expect(!watch.Changed(7, 0), "nothing before Chrome zoomed");
    Expect(watch.Changed(7, 0.52), "the new level is reported");
    Expect(!watch.Changed(7, 0.52), "and only once");
    watch.End(7);
    Expect(!watch.Watching(7), "the watch ends with its last check");
    Expect(!watch.Changed(7, 1.0), "no report without a wheel");
  }
  {
    // A burst: the second step starts while the first is pending; the level reached shows once.
    red::PageZoomWatch watch;
    watch.Begin(3, 0);
    watch.Begin(3, 0.52);  // Chrome already applied step 1; the start stays 0.
    Expect(watch.Changed(3, 1.22), "the level the burst reached is reported");
    Expect(!watch.Changed(3, 1.22), "the second step's check does not repeat it");
    watch.End(3);
    Expect(watch.Watching(3), "still watching for the second step");
    watch.End(3);
    Expect(!watch.Watching(3), "done after both");
  }
  {
    // The page took the wheel itself (maps, editors): the level never changes, no toast.
    red::PageZoomWatch watch;
    watch.Begin(5, 0.52);
    Expect(!watch.Changed(5, 0.52), "a page that used Ctrl+wheel itself shows nothing");
    watch.End(5);
  }
  {
    // Sol showed the level itself (Ctrl+=) while a wheel check was pending: no second toast.
    red::PageZoomWatch watch;
    watch.Begin(9, 0);
    watch.Shown(9, 0.52);
    Expect(!watch.Changed(9, 0.52), "a level Sol already showed is not shown again");
    Expect(watch.Changed(9, 0), "a later change still is");
    watch.End(9);
    watch.Shown(9, 2);  // Not watched: nothing to update.
    Expect(!watch.Watching(9), "Shown does not start a watch");
  }
  {
    // Pages are independent (split view).
    red::PageZoomWatch watch;
    watch.Begin(1, 0); watch.Begin(2, 0);
    Expect(watch.Changed(2, 0.52) && !watch.Changed(1, 0), "only the pane that zoomed is reported");
  }
  if (failures) return 1;
  std::printf("page_zoom: all passed\n");
  return 0;
}
