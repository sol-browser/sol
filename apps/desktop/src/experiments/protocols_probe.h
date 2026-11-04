#pragma once
#include "app/state.h"
namespace red {
// Acceptance (--probe-protocols=<file:/// URL of a local PDF>):
// 1. the fixture's /doc.pdf over HTTP opens in Chrome's PDF viewer;
// 2. the local PDF, opened like a typed path (WorkspaceController::Navigate),
//    opens in the viewer too;
// 3. back on the fixture page, a website's own navigation to file:// is
//    refused by Chrome (the page stays);
// 4. a user-gesture link to an app scheme reaches Chrome's external protocol
//    handler (OnProtocolExecution) without leaving the page; any window Chrome
//    opens for it is recorded and dismissed (no app is launched by the probe);
// 5. the local PDF dropped on the page (DevTools drag events with the file)
//    opens in a new tab with the PDF viewer.
void OnProtocolsProbeFrame(const std::shared_ptr<State>& state, CefRefPtr<CefBrowser> page, const std::string& url, bool main);
}
