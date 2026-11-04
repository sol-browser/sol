#pragma once
#include "app/state.h"
namespace red {
// Experiment (--probe-ua-brand=<name>|none, --probe-url=<Web Store item>):
// reports the Chrome Web Store install button label when the page sees
// <name> in navigator.userAgentData brands (DevTools emulation, this page
// only), to decide whether a "Red" brand would be shown by the store.
void OnUaBrandProbeLoad(const std::shared_ptr<State>& state, CefRefPtr<CefBrowser> page, const std::string& url);
void ObserveUaBrandProbe(const std::shared_ptr<State>& state, const std::string& message);
}
