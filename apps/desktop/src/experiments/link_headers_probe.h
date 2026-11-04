#pragma once
#include "app/state.h"
namespace red {
// Experiment (--probe-link-headers): on /source, a real click on a fixed
// cross-site target=_blank link (or, on /source?peek, a plain link from the
// pinned tab: Link Peek); the test server records the request headers/cookies
// of the opened page (Referer, Sec-Fetch-Site, SameSite).
void OnLinkHeadersProbeLoad(const std::shared_ptr<State>& state, CefRefPtr<CefBrowser> page, const std::string& url);
}
