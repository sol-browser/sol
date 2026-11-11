#pragma once
#include "app/state.h"
#include "include/cef_resource_request_handler.h"
namespace red {
CefRefPtr<CefResourceRequestHandler> IntegrationFixtureResource(const std::shared_ptr<State>& state, const std::string& url);
std::string PreviewFixtureUrl(const std::shared_ptr<State>& state, const std::string& canonical);
// The owned Gmail Atom feed for a fixture mailbox (probe only); empty otherwise.
std::string GmailFeedFixture(const std::shared_ptr<State>& state, const std::string& feed_url);
void OnGoogleIntegrationsProbeLoad(const std::shared_ptr<State>& state, CefRefPtr<CefBrowser> browser);
}
