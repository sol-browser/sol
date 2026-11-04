#pragma once
#include "app/state.h"
#include "include/cef_client.h"
namespace red {
inline constexpr int kInspectElementMenu = 26510;
void ConfigurePageDevTools(const std::shared_ptr<State>& state, int target, CefWindowInfo& info);
CefRefPtr<CefClient> DevToolsClient(const std::shared_ptr<State>& state, int target);
bool ShowPageDevTools(const std::shared_ptr<State>& state, CefRefPtr<CefBrowser> page,
                     const CefPoint& inspect = CefPoint(), bool toggle = false);
void ClosePageDevTools(const std::shared_ptr<State>& state, int target);
}  // namespace red
