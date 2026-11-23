#pragma once
#include <windows.h>
#include <filesystem>
#include <optional>
#include <string_view>
#include <string>
#include "include/cef_values.h"
#include "windows/default_browser_rules.h"
namespace red {
// Stable per-user install, never a development/temporary executable.
// The Velopack install (%LOCALAPPDATA%\SolBrowser\current\sol.exe) or the older Inno one.
std::filesystem::path InstalledBrowserPath();
// Offers the installed Sol to Windows (Start menu shortcut, Default Apps) if it
// is not offered yet; never changes the person's default browser.
bool EnsureRegistered();
// Chromium registers itself as "Chromium" in Default Apps when a site becomes a
// link-type handler (mailto: for Gmail) or someone accepts its default-browser
// or PDF prompts (docs/settings.md). Removes such registrations that start
// |executable| (the installed sol.exe); returns how many it removed.
int RemoveChromiumRegistrations(const std::filesystem::path& executable);
// Velopack uninstall: removes those offers and the Start menu shortcut.
void Unregister();
CefRefPtr<CefDictionaryValue> DefaultBrowserStatus();
bool OpenDefaultBrowserSettings(HWND owner, std::string& error);
// Red -> Sol: if Windows still sends web links to Red's ProgIDs (Red was the
// default browser), point those ProgIDs at the installed Sol, so links keep
// opening with the moved profile until Sol is chosen in Default Apps.
void AdoptRedLinks();
}  // namespace red
