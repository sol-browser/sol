#pragma once
#include "app/state.h"
namespace red {
void StartPasswordsProbe(const std::shared_ptr<State>& state, CefRefPtr<CefBrowser> page, const std::string& url);
}
