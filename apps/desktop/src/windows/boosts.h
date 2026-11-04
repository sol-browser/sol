#pragma once
#include <filesystem>
#include <functional>
#include <memory>
#include "app/state.h"
#include "include/cef_browser.h"
#include "site/boost_store.h"

namespace red {
// Boosts (docs/boosts.md). UI thread only.
// Reads the profile's Boosts into |session| (once); false only when a file exists but is unreadable.
bool LoadBoosts(AppSession& session, const std::filesystem::path& file);
// A Boost as a dictionary ({enabled, css, look, zaps, focus}) and back. Reading checks only the
// types (false on a wrong one); ranges and selectors are BoostStore::Set's.
bool ReadBoost(CefRefPtr<CefDictionaryValue> dictionary, Boost& boost);
CefRefPtr<CefDictionaryValue> WriteBoost(const Boost& boost);
// Writes the Boosts atomically (temp file, then replace); error text on failure, empty on success.
std::string SaveBoosts(const AppSession& session);
// Installs (or renews) the Boost loader in an isolated world of a website page: called when
// the page is created and after each main-frame load, because a registration made before a
// cross-process navigation is lost with the old process.
void InstallBoosts(const std::shared_ptr<State>& state, CefRefPtr<CefBrowser> page);
// Pushes the current Boosts into every installed page, applying edits without a reload.
void RefreshBoosts(AppSession& session);
void ForgetBoostPage(AppSession& session, int browser_id);
// Element picker on one page ("zap" or "focus"). |picked| receives each pick ({mode, selector}
// or {cancel: true}) on the UI thread until StopBoostPick. False when the page has no Boost slot.
bool StartBoostPick(AppSession& session, int browser_id, const std::string& mode,
                    std::function<void(CefRefPtr<CefDictionaryValue>)> picked);
void StopBoostPick(AppSession& session, int browser_id);
}  // namespace red
