#pragma once
#include "app/state.h"
namespace red {
// Experiment (--probe-sidebar-perf=<tabs>): runs the shell's performance
// scenarios (apps/browser-ui/src/perf-probe.ts) with that many saved tabs:
// sidebar show, live resize, tab switching, open/close cycles and idle, with
// frame gaps, long animation frames and this window's snapshot counters.
// Records "sidebar_perf" and closes the window (docs/performance-audit.md).
void OnSidebarPerfProbeLoad(const std::shared_ptr<State>& state, const std::string& url);
void ObserveSidebarPerfProbe(const std::shared_ptr<State>& state, const std::string& message);
}
