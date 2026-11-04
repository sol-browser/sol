#pragma once
#include "app/state.h"
namespace red {
void ObserveLifecycleProbe(const std::shared_ptr<State>& state, CefRefPtr<CefBrowser> browser, const std::string& message);
std::optional<bool> LifecycleUnloadDecision(const std::shared_ptr<State>& state, int browser);
}
