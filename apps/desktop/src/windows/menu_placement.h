#pragma once
#include <algorithm>
namespace red {
struct MenuBounds { int x,y,width,height; };
// All coordinates are screen DIPs, including negative monitor origins.
inline MenuBounds PlaceMenu(MenuBounds work,int x,int y,int width,int height,bool submenu=false,int parent_left=0) {
  width=std::min(width,std::max(1,work.width));
  height=std::min(height,std::max(1,work.height));
  if(x+width>work.x+work.width) x=submenu ? parent_left-width-2 : x-width;
  if(!submenu && y+height>work.y+work.height) y-=height;
  return {std::clamp(x,work.x,std::max(work.x,work.x+work.width-width)),
          std::clamp(y,work.y,std::max(work.y,work.y+work.height-height)),width,height};
}
}
