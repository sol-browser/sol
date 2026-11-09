#pragma once
#include "navigation/shortcuts.h"
#include "integrations/providers.h"
namespace red {
inline std::string_view CommandDocumentUrl(std::string_view name, std::string_view notes_provider = "googleDocs") {
  if (name == "newNote") return notes_provider == "word" ? "https://word.new/" : "https://docs.new/";
  if (name == "newGoogleDoc") return "https://docs.new/";
  if (name == "newWordDoc") return "https://word.new/";
  if (name == "newFigmaDesign") return "https://figma.new/";
  return {};
}
inline std::string_view EnabledDocumentUrl(std::string_view name, std::string_view notes_provider, const IntegrationSettings& settings) {
  if (name == "newNote") {
    if (IntegrationEnabled(settings, notes_provider)) return CommandDocumentUrl(name, notes_provider);
    const auto fallback = notes_provider == "word" ? "googleDocs" : "word";
    return IntegrationEnabled(settings, fallback) ? CommandDocumentUrl(name, fallback) : std::string_view{};
  }
  const auto provider = name == "newGoogleDoc" ? "googleDocs" : name == "newWordDoc" ? "word" : "figma";
  return IntegrationEnabled(settings, provider) ? CommandDocumentUrl(name) : std::string_view{};
}
// This is deliberately narrower than browser.shortcut. The command renderer
// cannot acquire arbitrary Shell IPC or reopen itself through an action.
inline std::optional<Shortcut> CommandShortcut(std::string_view name) {
  if (name == "siteSearchSettings") return Shortcut::Settings;
  if (!CommandDocumentUrl(name).empty()) return Shortcut::NewTab;
  const auto action = ShortcutNamed(name);
  if (!action) return std::nullopt;
  switch (*action) {
    case Shortcut::Settings: case Shortcut::Downloads: case Shortcut::HistoryPage:
    case Shortcut::ReopenClosed: case Shortcut::Sidebar: case Shortcut::NewWindow:
    case Shortcut::NewPrivateWindow: case Shortcut::Reload: case Shortcut::ReloadBypass:
    case Shortcut::Pin: case Shortcut::Back: case Shortcut::Forward: case Shortcut::DevTools:
    case Shortcut::Print: case Shortcut::Fullscreen: return action;
    default: return std::nullopt;
  }
}
inline bool CommandNeedsPage(Shortcut action) {
  return action == Shortcut::Reload || action == Shortcut::ReloadBypass || action == Shortcut::Pin ||
      action == Shortcut::Back || action == Shortcut::Forward || action == Shortcut::DevTools || action == Shortcut::Print;
}
inline bool CommandAvailable(Shortcut action, bool page, bool standalone, bool back, bool forward) {
  return (!CommandNeedsPage(action) || page) &&
      ((action != Shortcut::Pin && action != Shortcut::Sidebar) || !standalone) &&
      (action != Shortcut::Back || back) && (action != Shortcut::Forward || forward);
}
}  // namespace red
