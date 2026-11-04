#pragma once
#include "app/state.h"
namespace red {
// --probe-history=record: visit /alpha then /beta, download /notes.txt, then
// remove "Alpha page" through the real history popover.
// --probe-history=restart (same profile): Beta persists, Alpha stays removed,
// the download is restored, then "Clear history: All time" empties history.
void OnHistoryProbeLoad(const std::shared_ptr<State>& state, CefRefPtr<CefBrowser> page, const std::string& url);
void ObserveHistoryProbe(const std::shared_ptr<State>& state, const std::string& message);
}
