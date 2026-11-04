#pragma once
#include <string>
#include "app/state.h"
#include "include/cef_permission_handler.h"
namespace red {
// Per-site permissions of the visible page, read and written through CEF's
// public content-setting API. Only http(s) pages have site settings, and only
// the visible page's origin can be changed (the UI never names an origin; the
// one it echoes must still match, so a navigation in between is rejected).
std::string SiteOrigin(CefRefPtr<CefBrowser> browser);
CefRefPtr<CefDictionaryValue> SiteInfo(const std::shared_ptr<State>& state);
int SetSitePermission(const std::shared_ptr<State>& state, const std::string& origin,
                      const std::string& key, const std::string& value, std::string& error);
int ResetSitePermissions(const std::shared_ptr<State>& state, const std::string& origin, std::string& error);
// Answers a camera/microphone request when the site's setting is already
// Allow or Block, so CEF owns the stream and reports it (OnMediaAccessChange
// fires only for streams CEF approved). Ask, and screen capture, return false
// for Chrome's own prompt or picker.
bool DecideMediaAccess(CefRefPtr<CefBrowser> browser, const std::string& requesting_origin,
                       uint32_t requested, CefRefPtr<CefMediaAccessCallback> callback);
// A website frame of a page started or stopped audible playback (renderer detector).
void SetFrameAudible(const std::shared_ptr<State>& state, int browser_id, const std::string& frame, bool audible);
// Camera/microphone use reported by CefDisplayHandler::OnMediaAccessChange.
void SetMediaAccess(const std::shared_ptr<State>& state, int browser_id, bool video, bool audio);
}  // namespace red
