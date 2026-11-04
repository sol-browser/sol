#pragma once
#include "include/cef_frame.h"
#include "include/cef_v8.h"
namespace red {
// Renderer process, website frames only (http/https). CEF exposes no audible
// state for Chrome-style browsers, so a small detector watches <video>/<audio>
// playback and reports one boolean per frame to the browser process
// ("red.audible"). The page gets no new global: the native report function is
// passed into a closure, and the script captures built-ins before page scripts
// run. Limits: Web Audio and WebRTC audio are not detected; a page can fake its
// own indicator (harmless). See docs/tab-actions.md.
void InstallAudibleDetector(CefRefPtr<CefFrame> frame, CefRefPtr<CefV8Context> context);
void ReleaseAudibleDetector(CefRefPtr<CefFrame> frame);
}  // namespace red
