#pragma once
#include "app/state.h"
#include "include/wrapper/cef_message_router.h"
#include <windows.h>

namespace red {
// Settings → Import: Zen, Chrome, Edge and Arc profiles plus user-made
// password CSV / bookmark HTML exports. Nothing is read from another browser
// until the person confirms ("import.read"); nothing is written to Red until
// they confirm the preview ("import.commit"). Read data, passwords included,
// lives only in native memory and is wiped on cancel, commit or close.
class BrowserImport;
using ImportCallback = CefRefPtr<CefMessageRouterBrowserSide::Callback>;
// Handles "import.*" Settings queries; false when `method` is not one of them.
bool HandleImportQuery(const std::shared_ptr<State>& state, HWND owner, std::shared_ptr<BrowserImport>& session,
                       const std::string& method, CefRefPtr<CefDictionaryValue> data, ImportCallback callback);
void CancelBrowserImport(std::shared_ptr<BrowserImport>& session);
// Probe-only (--probe-import=<source>|<path>|<types>): read + commit without UI.
void RunImportProbe(const std::shared_ptr<State>& state);
}  // namespace red
