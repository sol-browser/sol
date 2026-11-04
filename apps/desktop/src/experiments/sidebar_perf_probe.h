#pragma once
#include "app/state.h"
namespace red {
// Experiment (--probe-sidebar-perf): with 80 saved tabs in the sidebar, hide
// the docked sidebar, then show it while the shell records every animation
// frame's gap (requestAnimationFrame) during the 220 ms content slide. Records
// "sidebar_perf" with frame gaps, so the cost of a show can be compared.
void OnSidebarPerfProbeLoad(const std::shared_ptr<State>& state, const std::string& url);
void ObserveSidebarPerfProbe(const std::shared_ptr<State>& state, const std::string& message);
}
