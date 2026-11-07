#pragma once
#include "app/state.h"
#include "include/wrapper/cef_message_router.h"
namespace red {
// Both callers must first authenticate their registered browser/main frame and
// exact document URL. This dispatcher handles shared sidebar actions only;
// subscriptions, geometry and host-local file drops stay with the host.
bool HandleSidebarAction(const std::shared_ptr<State>& state, CefRefPtr<CefBrowser> source,
    const std::string& method, CefRefPtr<CefDictionaryValue> data, bool persistent,
    CefRefPtr<CefMessageRouterBrowserSide::Callback> callback);
}
