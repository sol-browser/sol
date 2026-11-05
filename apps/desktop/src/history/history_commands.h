#pragma once
#include <functional>
#include <string>
#include "app/state.h"
#include "include/wrapper/cef_message_router.h"
namespace red {
// History IPC shared by the history popover and the full history page:
//   history.list {before, query}   history.days {query}
//   history.delete {id}   history.deleteMany {ids}
//   history.clear {range}          history.open {url}    history.openPage {}
// Returns false when |method| is not a history command (caller rejects it).
// |opened| runs after history.open / history.openPage succeeded.
// Whether History and Downloads show times with AM/PM (Settings > General).
bool HourClock(const std::shared_ptr<State>& state);
bool HandleHistoryCommand(const std::shared_ptr<State>& state, const std::string& method,
                          CefRefPtr<CefDictionaryValue> d, CefRefPtr<CefMessageRouterBrowserSide::Callback> cb,
                          std::function<void()> opened);
}  // namespace red
