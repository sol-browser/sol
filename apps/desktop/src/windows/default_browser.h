#pragma once
#include <windows.h>
#include <filesystem>
#include <optional>
#include <string>
#include "include/cef_values.h"
namespace red {
// Stable per-user install, never a development/temporary executable.
// The Velopack install (%LOCALAPPDATA%\SolBrowser\current\sol.exe) or the older Inno one.
std::filesystem::path InstalledBrowserPath();
// Offers the installed Sol to Windows (Start menu shortcut, Default Apps) if it
// is not offered yet; never changes the person's default browser.
bool EnsureRegistered();
// Velopack uninstall: removes those offers and the Start menu shortcut.
void Unregister();
CefRefPtr<CefDictionaryValue> DefaultBrowserStatus();
bool OpenDefaultBrowserSettings(HWND owner, std::string& error);
// Red -> Sol: if Windows still sends web links to Red's ProgIDs (Red was the
// default browser), point those ProgIDs at the installed Sol, so links keep
// opening with the moved profile until Sol is chosen in Default Apps.
void AdoptRedLinks();
}  // namespace red
