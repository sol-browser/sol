#pragma once
#include "app/state.h"
namespace red {
// Native CDP observation only; no website IPC or externally reachable debug port.
CefRefPtr<CefRegistration> TracePlatformCodecs(const std::shared_ptr<State>& state, CefRefPtr<CefBrowser> page);
void StartPlatformCodecs(const std::shared_ptr<State>& state, CefRefPtr<CefBrowser> page);
}
