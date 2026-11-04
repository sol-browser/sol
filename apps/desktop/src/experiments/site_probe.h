#pragma once
#include "app/state.h"
namespace red {
// Drives the real site settings popover: block location, allow the (fake)
// camera and use it, then reset; checks Chromium's stored settings, what the
// page sees and the camera indicator in the snapshot.
void StartSiteProbe(const std::shared_ptr<State>& state, CefRefPtr<CefBrowser> page);
void ObserveSiteProbe(const std::shared_ptr<State>& state, CefRefPtr<CefBrowser> page, const std::string& message);
void OnSiteProbeMedia(const std::shared_ptr<State>& state, bool video, bool audio);
}
