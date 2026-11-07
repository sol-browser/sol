#pragma once
#include "app/state.h"
namespace red {
void StartProfileSettingsProbe(const std::shared_ptr<State>& state);
void ObserveProfileSettingsProbe(CefRefPtr<CefBrowser> browser,const std::string& message);
void CloseProfileSettingsProbe(int browser);
void CaptureProfileEditor(const std::shared_ptr<State>& state,int attempts=100);
}
