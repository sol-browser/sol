#pragma once
#include "app/state.h"
namespace red {
// Experiment (--probe-split, scripts/split_view_experiment.py): builds a real
// Split View of two, three and four website panes, applies the 2x2 preset,
// waits for a real pointer drag of its divider, expands, removes and separates,
// recording after each step where every page's native window actually is.
void OnSplitProbeLoad(const std::shared_ptr<State>& state, const std::string& url);
}
