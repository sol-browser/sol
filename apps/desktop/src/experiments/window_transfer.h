#pragma once
#include "app/state.h"
namespace red {
// Standalone opt-in feasibility host, never attached to the daily workspace.
void StartWindowTransfer(const std::shared_ptr<State>& state, const std::string& url);
}
