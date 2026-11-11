#pragma once
#include "include/cef_request_context.h"

namespace red {
// Chrome's own session restore must never run inside Sol: Sol keeps its tabs
// in the workspace tree. CEF writes `session.restore_on_startup = 1` ("continue
// where you left off") for every context with persist_session_cookies, and
// Chrome's SessionService then restores the profile's current session whenever
// a page opens while that profile has no other page open. Sol adopts each
// restored page as a new Today tab, so the tabs multiplied (2026-11-10).
//
// The startup value only matters when the profile loads: Chrome keeps session
// cookies when the last exit crashed or the value is "last". So the value is
// "new tab" while Sol runs and "last" again on a clean exit.
inline constexpr char kChromeRestoreOnStartup[] = "session.restore_on_startup";
inline constexpr int kChromeRestoreLast = 1;     // SessionStartupPref::kPrefValueLast
inline constexpr int kChromeRestoreNewTab = 5;   // SessionStartupPref::kPrefValueNewTab

inline bool SetChromeRestoreOnStartup(CefRefPtr<CefRequestContext> context, int value) {
  if (!context || !context->CanSetPreference(kChromeRestoreOnStartup)) return false;
  auto pref = CefValue::Create(); pref->SetInt(value);
  CefString error;
  return context->SetPreference(kChromeRestoreOnStartup, pref, error);
}
// While Sol runs: no Chrome session restore.
inline bool SuppressChromeSessionRestore(CefRefPtr<CefRequestContext> context) {
  return SetChromeRestoreOnStartup(context, kChromeRestoreNewTab);
}
// Clean exit: keep session cookies for the next start.
inline bool KeepSessionCookiesForNextStart(CefRefPtr<CefRequestContext> context) {
  return SetChromeRestoreOnStartup(context, kChromeRestoreLast);
}
}  // namespace red
