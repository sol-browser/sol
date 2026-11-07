#pragma once
#include <string>
#include "app/state.h"
#include "include/wrapper/cef_message_router.h"
namespace red {
// IPC of the full downloads page (sol://ui/downloads.html):
//   downloads.page {before}            active session downloads + saved ones
//   downloads.file {id, action}        open | show | copy | recycle | remove (saved ID)
//   downloads.control {id, action}     pause | resume | cancel (active session ID)
//   downloads.clearAll {}              forget all saved and finished downloads
// File system paths never reach the page. Returns false for other methods.
bool HandleDownloadsPageCommand(const std::shared_ptr<State>& state, const std::string& method,
                                CefRefPtr<CefDictionaryValue> d, CefRefPtr<CefMessageRouterBrowserSide::Callback> cb,
                                std::shared_ptr<BrowsingProfile> profile = nullptr);
}  // namespace red
