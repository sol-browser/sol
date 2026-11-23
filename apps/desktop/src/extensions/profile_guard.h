#pragma once
#include <memory>
#include "app/state.h"
#include "include/cef_registration.h"
#include "include/cef_request_context.h"

namespace red {
bool ShellProfileHasProtection(const std::filesystem::path& profile);
// After CEF has acquired the singleton for this root, before opening Shell.
bool PrepareShellProfile(const std::filesystem::path& profile);
// Verify the profile and extension settings before creating any trusted view.
bool GuardShellProfile(const std::shared_ptr<State>& state, CefRefPtr<CefRequestContext> shell);
}  // namespace red
