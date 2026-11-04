#pragma once
#include "app/state.h"
namespace red {
// Experiment (--probe-close-motion): three Today tabs (/a, /b, /c), /b active;
// the shell records every animation frame of the sidebar (row positions,
// active highlight, opacity, transforms, exit ghosts) while the real close
// button of /b is clicked. Records "motion_frame" rows for frame-by-frame review.
void OnCloseMotionProbeLoad(const std::shared_ptr<State>& state, const std::string& url);
void ObserveCloseMotionProbe(const std::shared_ptr<State>& state, const std::string& message);
}
