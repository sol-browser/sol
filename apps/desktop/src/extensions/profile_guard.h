#pragma once
#include <memory>
#include "app/state.h"
#include "include/cef_registration.h"
#include "include/cef_request_context.h"

namespace red {
// The trusted UI's Shell profile must never run third-party extension code.
// Checks Chromium's own "extensions.settings" preference through the public
// preference API now and on every change. Outside probe mode a violation that
// persists for 3 seconds closes Sol, which then deletes the Shell profile and
// starts again (shell_reset.h; at most once per 10 minutes). Returns false (do not create the trusted
// UI) when the preference cannot be read outside probe mode.
bool GuardShellProfile(const std::shared_ptr<State>& state, CefRefPtr<CefRequestContext> shell);
}  // namespace red
