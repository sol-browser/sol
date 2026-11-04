#pragma once
#include "app/state.h"

namespace red {
// Only attaches for explicit --trace-tab-switches diagnostic launches.
CefRefPtr<CefRegistration> TracePageLoad(const std::shared_ptr<State>& state, CefRefPtr<CefBrowser> browser);
void SamplePageLoad(const std::shared_ptr<State>& state, CefRefPtr<CefBrowser> browser);
}
