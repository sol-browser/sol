#pragma once
#include "app/state.h"
namespace red {
void ObserveBrowsingProbe(const std::shared_ptr<State>& state, CefRefPtr<CefBrowser> browser, const std::string& message);
}
