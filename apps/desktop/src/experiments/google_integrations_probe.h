#pragma once
#include "app/state.h"
#include "include/cef_resource_request_handler.h"
namespace red {
CefRefPtr<CefResourceRequestHandler> IntegrationFixtureResource(const std::shared_ptr<State>& state, const std::string& url);
std::string PreviewFixtureUrl(const std::shared_ptr<State>& state, const std::string& canonical);
void OnGoogleIntegrationsProbeLoad(const std::shared_ptr<State>& state, CefRefPtr<CefBrowser> browser);
}
