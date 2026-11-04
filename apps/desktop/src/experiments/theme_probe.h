#pragma once
#include "app/state.h"
namespace red {
// Acceptance (--probe-theme): on the /theme fixture, applies a sequence of
// themes through the controller (as the theme popover does), recording each
// with the resolved mode and the themed window background; the smoke script
// screenshots the window between steps. Ends with the theme popover open.
void OnThemeProbeLoad(const std::shared_ptr<State>& state, const std::string& url);
}
