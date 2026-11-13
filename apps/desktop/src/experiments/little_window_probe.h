#pragma once
#include "app/state.h"
namespace red {
// Experiment (--probe-little-window[=external], --probe-url=http://localhost:port/source): opens /little in a
// Little Sol as "Open link in Little Sol" does, opens a foreground (/blank) and a background
// (/background) link from that page, then sends the page to the Space it came from.
// The server counts document requests: /little must load once (the live page moves).
// Evidence: little_opened, little_loaded, little_links, little_sent, little_closed; external mode: little_external_ready, little_external_loaded, little_pref_off, little_external_tab.
void OnLittleWindowProbeLoad(const std::shared_ptr<State>& state, const std::string& url);
}
