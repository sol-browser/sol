#pragma once
#include "include/cef_browser.h"
#include "include/cef_values.h"

namespace red {
// Holds or releases a page's animation clock (the document timeline's playback
// rate, through DevTools' Animation domain, as DevTools' speed control does).
// A page loaded by a sidebar hover is held from creation until it is on screen,
// so its entrance animations play when the user sees it (docs/today-auto-archive.md,
// "Preload on hover"). Releasing also disables the domain.
inline void PausePageAnimations(CefRefPtr<CefBrowser> browser, bool paused) {
  if (!browser) return;
  auto rate = CefDictionaryValue::Create();
  rate->SetDouble("playbackRate", paused ? 0.0 : 1.0);
  if (paused) browser->GetHost()->ExecuteDevToolsMethod(0, "Animation.enable", nullptr);
  browser->GetHost()->ExecuteDevToolsMethod(0, "Animation.setPlaybackRate", rate);
  if (!paused) browser->GetHost()->ExecuteDevToolsMethod(0, "Animation.disable", nullptr);
}
}  // namespace red
