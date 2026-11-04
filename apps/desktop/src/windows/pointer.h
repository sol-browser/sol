#pragma once
#include <windows.h>
namespace red {
// Whether the primary mouse button is held right now. GetAsyncKeyState reads
// physical buttons: with "switch primary and secondary buttons" the primary
// one is the physical right button (VK_RBUTTON).
inline bool PrimaryButtonDown() {
  return (GetAsyncKeyState(GetSystemMetrics(SM_SWAPBUTTON) ? VK_RBUTTON : VK_LBUTTON) & 0x8000) != 0;
}
}  // namespace red
