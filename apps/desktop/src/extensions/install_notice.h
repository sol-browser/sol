#pragma once
#include <memory>
#include "app/state.h"
#include "include/cef_request_context.h"

namespace red {
// Refreshes extension pins and tracks new installs so Chrome's transient
// post-install new-tab window can be suppressed.
CefRefPtr<CefRegistration> WatchExtensionInstalls(const std::shared_ptr<State>& state, CefRefPtr<CefRequestContext> content);
}  // namespace red
