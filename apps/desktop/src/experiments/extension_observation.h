#pragma once
#include "app/state.h"

namespace red {
// Probe-only, read-only observations. Console markers never authorize commands.
void ObserveExtensionProbe(const std::shared_ptr<State>& state,
                           CefRefPtr<CefBrowser> source, const std::string& message);
void SignalWorkspaceProbeReady(const std::shared_ptr<State>& state);
}  // namespace red
