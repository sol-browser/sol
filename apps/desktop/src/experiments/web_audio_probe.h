#pragma once
#include "app/state.h"
namespace red {
// --probe-web-audio: the page /web-audio (scripts/web_audio_smoke.py) plays a
// short generated tick through one AudioContext every ~90 ms, a new
// AudioBufferSourceNode per tick (the pattern of spinner/clicker sites). The
// probe starts it with a user gesture, keeps the tab muted (Chromium measures
// the level before tab mute), and requires every tick to be rendered and the
// tab to stay audible while ticks continue.
void OnWebAudioProbeLoad(const std::shared_ptr<State>& state, CefRefPtr<CefBrowser> page, const std::string& url);
void ObserveWebAudioProbe(const std::shared_ptr<State>& state, const std::string& message);
}
