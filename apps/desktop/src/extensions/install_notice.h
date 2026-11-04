#pragma once
#include <memory>
#include "app/state.h"
#include "include/cef_request_context.h"

namespace red {
// Chrome's "extension added" bubble anchors to a toolbar the workspace does not
// have. Watches the content profile's "extensions.settings" and shows a
// workspace notice when a store (or chrome://extensions unpacked) install
// becomes enabled. Extensions present at startup are not announced.
void WatchExtensionInstalls(const std::shared_ptr<State>& state, CefRefPtr<CefRequestContext> content);
}  // namespace red
