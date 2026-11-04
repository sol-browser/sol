#pragma once
#include "app/preferences.h"
#include "navigation/shortcuts.h"
namespace red {
bool ConfigureShortcut(Preferences& prefs, const std::string& action, const KeyBinding* binding, std::string& error);
std::optional<KeyBinding> PrimaryShortcut(const Preferences& prefs, Shortcut action);
CefRefPtr<CefListValue> DescribeShortcuts(const Preferences& prefs);
CefRefPtr<CefDictionaryValue> ShortcutLabels(const Preferences& prefs);
}  // namespace red
