#include "windows/window_placement.h"
#include "windows/integration_card_bounds.h"
#include <cstdio>
int main() {
  int failures = 0;
  const auto check = [&](CefRect actual, CefRect expected, const char* label) {
    if (actual != expected) { std::fprintf(stderr, "%s: %d,%d %dx%d\n", label, actual.x, actual.y, actual.width, actual.height); ++failures; }
  };
  check(red::CenteredWindowBounds({0,0,1920,1040},{1200,820}),{360,110,1200,820},"center in work area excluding taskbar");
  check(red::CenteredWindowBounds({-1920,40,1920,1040},{1200,820}),{-1560,150,1200,820},"negative display origin and top taskbar");
  check(red::CenteredWindowBounds({200,100,1024,720},{1200,820}),{200,100,1024,720},"fit smaller work area");
  check(red::CenteredWindowBounds({40,20,1001,801},{700,420}),{190,210,700,420},"odd dimensions stay within one DIP of center");
  check(red::CenteredWindowBounds({},{1200,820}),{},"missing display does not invent screen bounds");
  const auto favorite = red::PlaceIntegrationCard({100,70,1100,800},{16,100,52,48},320,64,42,true);
  check(favorite.bounds,
        {168,218,320,64},"hover card touches the favorite's lower right corner");
  check(red::PlaceIntegrationCard({-1200,50,1100,800},{1020,730,52,48},320,220,42,true).bounds,
        {-500,560,320,220},"hover card flips inward at the bottom right");
  const auto pinned = red::PlaceIntegrationCard({100,70,1100,800},{16,100,160,32},320,160,42,false);
  check(pinned.bounds,{284,112,320,160},"pinned card centers on the row with a right gap, clamped at the top");
  const auto left = red::PlaceIntegrationCard({-1200,50,1100,800},{920,730,160,32},320,220,42,false);
  check(left.bounds,{-608,622,320,220},"pinned card flips left and keeps the row in range at the bottom");
  const auto centered = red::PlaceIntegrationCard({0,0,1100,800},{16,280,160,32},320,160,42,false);
  check(centered.bounds,{184,216,320,160},"pinned card centers when there is enough room");
  check(red::PlaceIntegrationCard({0,0,390,600},{16,100,200,32},320,160,42,false).bounds,
        {62,140,320,160},"pinned card falls below when neither side fits");
  if (favorite.origin_x != 0.f || favorite.origin_y != 0.f || pinned.origin_x != 0.f ||
      pinned.origin_y != 74.f/160 || centered.origin_y != .5f || left.origin_x != 1.f || left.origin_y != 174.f/220) {
    std::fprintf(stderr,"zoom starts at the favorite corner or the pinned row edge\n"); ++failures;
  }
  return failures ? 1 : 0;
}
