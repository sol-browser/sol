#include "windows/menu_placement.h"
#include <cstdio>
int main() {
  int failures=0;
  auto check=[&](red::MenuBounds r,red::MenuBounds expected,const char* name) {
    if(r.x!=expected.x || r.y!=expected.y || r.width!=expected.width || r.height!=expected.height) {
      std::fprintf(stderr,"%s: %d,%d %dx%d\n",name,r.x,r.y,r.width,r.height);++failures;
    }
  };
  const red::MenuBounds work{0,0,1000,800};
  check(red::PlaceMenu(work,20,20,200,100),{20,20,200,100},"down/right when space exists");
  check(red::PlaceMenu(work,980,20,200,100),{780,20,200,100},"flip only horizontally");
  check(red::PlaceMenu(work,20,780,200,100),{20,680,200,100},"flip only vertically");
  check(red::PlaceMenu(work,980,780,200,100),{780,680,200,100},"flip both axes around click");
  check(red::PlaceMenu(work,999,799,200,100),{799,699,200,100},"last visible pixel stays on the anchor monitor");
  check(red::PlaceMenu(work,800,700,200,100),{800,700,200,100},"exact fit does not flip");
  check(red::PlaceMenu({-1920,-200,1920,1080},-10,870,200,100),{-210,770,200,100},"negative monitor origin");
  check(red::PlaceMenu(work,950,780,200,100,true,700),{498,700,200,100},"flyout flips beside parent and keeps row alignment");
  check(red::PlaceMenu(work,300,200,200,100,true,100),{300,200,200,100},"flyout right side fits");
  check(red::PlaceMenu({0,0,100,80},90,70,200,100),{0,0,100,80},"oversized menu bounded by work area");
  check(red::PlaceMenu(work,-10,-10,200,100),{0,0,200,100},"out-of-area anchor clamped");
  check(red::PlaceMenu(work,80,80,950,750),{0,0,950,750},"neither side fits: clamp fallback");
  return failures ? 1 : 0;
}
