#pragma once
#include "app/state.h"
namespace red {
// --probe-tab-actions: duplicate the visible tab, mute and unmute the copy,
// close it (Today tabs archive) and reopen it via the Ctrl+Shift+T path.
void OnTabActionsProbeLoad(const std::shared_ptr<State>& state, const std::string& url);
}
