#pragma once
#include "app/state.h"
namespace red {
// Experiment: request one permission type from a real page with a user gesture
// and record CEF callbacks, Chrome prompt windows and the page outcome.
bool IsPermissionsProbeType(const std::string& type);
void StartPermissionsProbe(const std::shared_ptr<State>& state, CefRefPtr<CefBrowser> page);
void ObservePermissionsProbe(const std::shared_ptr<State>& state, CefRefPtr<CefBrowser> page, const std::string& message);
void RecordPermissionRequest(const std::shared_ptr<State>& state, CefRefPtr<CefBrowser> browser,
                             const char* kind, const std::string& origin, uint32_t permissions);
}
