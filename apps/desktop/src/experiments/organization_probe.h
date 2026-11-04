#pragma once
#include "app/state.h"
#include <optional>
namespace red {
void ObserveOrganizationProbe(const std::shared_ptr<State>& state, CefRefPtr<CefBrowser> browser, const std::string& message);
std::optional<bool> OrganizationUnloadDecision(const std::shared_ptr<State>& state, int browser_id);
}
