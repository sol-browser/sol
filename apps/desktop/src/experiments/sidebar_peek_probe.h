#pragma once
#include "app/state.h"
namespace red {
// Acceptance (--probe-sidebar-peek): with tabs /a and /b and the docked
// sidebar hidden, open the floating sidebar (the edge-hover trigger itself
// needs a real pointer), then from its document: list the rows, try a
// non-sidebar command (must be refused), click row "a" (must activate it);
// finally hide it (the window must end hidden).
void OnSidebarPeekProbeLoad(const std::shared_ptr<State>& state, const std::string& url);
}
