#pragma once
#include "app/state.h"
namespace red {
// --probe-audible: play an in-document <audio>, pause it, play a detached
// new Audio(), mute that element, and check the page gained no globals; the
// tab's "audible" snapshot flag must follow. Output is muted (tab mute, or --mute-audio for the renderer detector).
void OnAudibleProbeLoad(const std::shared_ptr<State>& state, CefRefPtr<CefBrowser> page, const std::string& url);
void ObserveAudibleProbe(const std::shared_ptr<State>& state, const std::string& message);
}
