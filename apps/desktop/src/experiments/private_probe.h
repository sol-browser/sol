#pragma once
#include "app/state.h"
namespace red {
// Experiment (--probe-private, --probe-url=http://localhost:port/plain): the normal page gets a
// cookie from the test server; the probe opens a private window and loads /private there, then
// loads /check in the normal window and closes the private one. The server sees which cookies each
// request carried (isolation both ways); the probe reports whether /private reached history.
// Evidence: private_window_opened, private_loaded, private_history, private_closed.
void OnPrivateProbeLoad(const std::shared_ptr<State>& state, const std::string& url);
}
