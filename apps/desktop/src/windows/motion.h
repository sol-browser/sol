#pragma once
#include <windows.h>
#include <dcomp.h>
#include <dcompanimation.h>
#include <wrl/client.h>

namespace red {
inline constexpr int kPopupOpeningMs = 280;
inline constexpr int kPopupClosingMs = 280;
inline bool NativeMotionEnabled() {
  BOOL enabled = TRUE;
  SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &enabled, 0);
  return enabled != FALSE;
}
// Notification cards and popovers share a uniform 0 -> 1 ease-out cubic.
// |delay| seconds hold |from| first.
inline Microsoft::WRL::ComPtr<IDCompositionAnimation> EaseNativeMotion(
    IDCompositionDevice* device, float from, float to, float duration, float delay = 0) {
  Microsoft::WRL::ComPtr<IDCompositionAnimation> animation;
  const float delta = to - from;
  if (FAILED(device->CreateAnimation(&animation)) || (delay > 0 && FAILED(animation->AddCubic(0, from, 0, 0, 0))) ||
      FAILED(animation->AddCubic(delay, from, 3 * delta / duration, -3 * delta / (duration * duration), delta / (duration * duration * duration))) ||
      FAILED(animation->End(delay + duration, to))) return nullptr;
  return animation;
}
// Time reversal of the opening cubic: 1 -> 0 accelerates into the anchor.
inline Microsoft::WRL::ComPtr<IDCompositionAnimation> ClosePopupMotion(
    IDCompositionDevice* device, float from) {
  Microsoft::WRL::ComPtr<IDCompositionAnimation> animation;
  constexpr float duration = kPopupClosingMs / 1000.f;
  if (FAILED(device->CreateAnimation(&animation)) ||
      FAILED(animation->AddCubic(0, from, 0, 0, -from / (duration * duration * duration))) ||
      FAILED(animation->End(duration, 0.f))) return nullptr;
  return animation;
}
// One restrained overshoot for Peek; the viewport never changes size.
// Link Peek card growth and its single bounce, seconds.
inline constexpr float kPeekGrowth=.32f,kPeekSettle=.12f;
// |delay| seconds hold the starting size first (live Link Peek: the page recedes, then the card).
inline Microsoft::WRL::ComPtr<IDCompositionAnimation> PeekEntryMotion(IDCompositionDevice* device,float bounce,float delay=0) {
  Microsoft::WRL::ComPtr<IDCompositionAnimation> animation;
  constexpr float first=kPeekGrowth,settle=kPeekSettle; const float delta=.975f+bounce,back=-bounce;
  if (FAILED(device->CreateAnimation(&animation)) || (delay>0 && FAILED(animation->AddCubic(0,.025f,0,0,0))) ||
      FAILED(animation->AddCubic(delay,.025f,0,3*delta/(first*first),-2*delta/(first*first*first))) ||
      FAILED(animation->AddCubic(delay+first,1+bounce,0,3*back/(settle*settle),-2*back/(settle*settle*settle))) ||
      FAILED(animation->End(delay+first+settle,1.f))) return nullptr;
  return animation;
}
}  // namespace red
