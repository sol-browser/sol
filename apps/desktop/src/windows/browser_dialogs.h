#pragma once
#include "app/state.h"
namespace red {
// Close Chromium-owned bubbles before releasing their BrowserWindowInterface.
// Preserve CEF's real beforeunload flow; never destroy HWNDs or force consent.
void CloseBrowserWithDialogs(std::weak_ptr<State> state, CefRefPtr<CefBrowser> browser, bool force);
}
