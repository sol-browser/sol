#pragma once
#include "app/state.h"
namespace red {
// Drives page fullscreen through the real page (Fullscreen API with a user
// gesture, then Escape) and measures native window geometry at each step.
void StartFullscreenProbe(const std::shared_ptr<State>& state, CefRefPtr<CefBrowser> page);
void ObserveFullscreenProbe(const std::shared_ptr<State>& state, CefRefPtr<CefBrowser> browser, const std::string& message);
void OnFullscreenProbeChange(const std::shared_ptr<State>& state, CefRefPtr<CefBrowser> browser, bool fullscreen);
}
