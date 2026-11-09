#pragma once
#include "app/state.h"
namespace red {
bool IntegrationLiveCheckEnabled(const std::shared_ptr<State>& state);
void ScheduleIntegrationLiveCheck(const std::shared_ptr<State>& state,CefRefPtr<CefBrowser> browser);
void RecordIntegrationPreviewCheck(const std::shared_ptr<State>& state, const std::string& service,
                                   CefRefPtr<CefDictionaryValue> result, bool foreground);
}
