#pragma once
#include <functional>
#include <memory>
#include <string>
#include "include/cef_browser.h"
namespace red {
struct State;
void StartSiteSearchProbe(const std::shared_ptr<State>& state, const std::string& input, std::function<void()> done);
void ObserveSiteSearchProbe(CefRefPtr<CefBrowser> browser, const std::string& message);
}  // namespace red
