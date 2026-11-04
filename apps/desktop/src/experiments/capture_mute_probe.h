#pragma once
#include "app/state.h"
namespace red {
// Acceptance (--probe-capture-mute, with --load-extension of the owned capture
// fixture): a page plays a quiet tone, the fixture's popup captures the tab's
// audio and reports its level; our tab mute must silence the capture too
// (engine patch 06) and unmuting must restore it.
void OnCaptureMuteProbeLoad(const std::shared_ptr<State>& state, CefRefPtr<CefBrowser> page, const std::string& url);
// Level reports from the fixture popup ("RED_CAPTURE_LEVEL:<rms>").
void ObserveCaptureMuteProbe(const std::shared_ptr<State>& state, const std::string& message);
}
