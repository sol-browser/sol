#pragma once
#include "app/state.h"
namespace red {
// Experiment (--probe-boost, --probe-url=<http://127.0.0.1:port/plain>): registers a
// style-injecting script with CDP Page.addScriptToEvaluateOnNewDocument on the page's
// DevTools session, then reloads and navigates (same origin, a CSP page, a second page)
// and reports from inside every frame whether the injected stylesheet applied
// (docs/boosts.md, option A).
// --probe-boost=store (--probe-url=http://localhost:port/plain): the product path. Saves a Boost
// for localhost into the session store, refreshes the live page, then navigates and disables it,
// reporting the h1 colour at each step and the saved file's presence.
// --probe-boost=pick: opens the Boost editor window for the page, starts Zap, clicks #ad with CDP
// input (trusted events) and reports the picked selector and whether #ad is hidden afterwards.
void OnBoostProbeLoad(const std::shared_ptr<State>& state, CefRefPtr<CefBrowser> page, const std::string& url);
void ObserveBoostProbe(const std::shared_ptr<State>& state, const std::string& message);
}
