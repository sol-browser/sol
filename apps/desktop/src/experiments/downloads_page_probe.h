#pragma once
#include "app/state.h"
namespace red {
// --probe-downloads-page: download notes.txt from /alpha, open the downloads
// page through the Ctrl+J path, check it runs as trusted UI (shell commands
// refused), lists the file, removes it from the list, cannot navigate away.
void OnDownloadsPageProbeLoad(const std::shared_ptr<State>& state, CefRefPtr<CefBrowser> page, const std::string& url);
void ObserveDownloadsPageProbe(const std::shared_ptr<State>& state, const std::string& message);
}
