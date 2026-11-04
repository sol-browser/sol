#pragma once
#include "app/state.h"
namespace red {
// Local fixture only, selected by --probe-link-headers and /source?background.
void ProbePageBackground(std::weak_ptr<State> state, CefRefPtr<CefBrowser> page);
void ProbePageContainerMotion(std::weak_ptr<State> state, CefRefPtr<CefBrowser> page);
}
