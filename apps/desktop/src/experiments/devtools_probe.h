#pragma once
#include "app/state.h"
namespace red {
CefRefPtr<CefRegistration> TraceDevToolsProbe(const std::shared_ptr<State>& state, CefRefPtr<CefBrowser> page);
void StartDevToolsProbe(const std::shared_ptr<State>& state, CefRefPtr<CefBrowser> page);
}
