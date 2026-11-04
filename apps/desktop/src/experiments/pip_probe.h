#pragma once
#include "app/state.h"
namespace red {
// Experiment (--probe-pip): can the host put a page's playing video into
// Picture-in-Picture itself (Arc-style automatic PiP on tab switch) with the
// public API? The fixture plays a canvas stream in a <video>. The probe asks
// for PiP with a DevTools user gesture (1) on the visible page, (2) after
// the page was hidden by switching tabs, and records the page's answer and
// the top-level windows Chrome shows.
void OnPipProbeLoad(const std::shared_ptr<State>& state, CefRefPtr<CefBrowser> page, const std::string& url);
void ObservePipProbe(const std::shared_ptr<State>& state, const std::string& message);
}
