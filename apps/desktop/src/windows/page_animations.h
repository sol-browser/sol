#pragma once
#include "include/cef_browser.h"

namespace red {
// Holds or releases a page's animations. A page loaded by a sidebar hover is
// held from creation until it is on screen, so its entrance animations play
// when the user sees it (docs/today-auto-archive.md, "Preload on hover").
//
// Each CSS/Web animation is paused as it starts (DevTools' Animation domain:
// `animationStarted` → `Animation.setPaused`) and unpaused on release, which
// also disables the domain. The document timeline itself keeps running: it
// is the page's requestAnimationFrame clock, and DevTools' speed control
// (`Animation.setPlaybackRate`, used here before 2026-11-26) leaves it behind
// performance.now() for the rest of the document by however long the page
// was held. Sites that mix the two clocks then misbehave (user report
// 2026-11-25: mirrorv2.darte.gg's spinner clicked once, then its free-spin
// ticks were all skipped). docs/implementation-plan.md, "Hover preload and
// the page clock".
//
// Pausing again (a new document in the held browser) starts a new hold.
// UI thread only.
void PausePageAnimations(CefRefPtr<CefBrowser> browser, bool paused);
// The browser closed: drop its hold (observer and paused ids).
void ForgetPageAnimations(int browser_id);
// Animations currently paused by a hold (probes).
size_t HeldPageAnimations(int browser_id);
}  // namespace red
