#include "navigation/shortcut_settings.h"
#include "app/session.h"
#include "interface_strings.h"
#include <algorithm>
#include <vector>
namespace red {
namespace {
struct Entry { Shortcut action; KeyBinding key; };
const std::vector<Entry>& Defaults() {
  static const std::vector<Entry> entries = [] {
    std::vector<Entry> result = {
      {Shortcut::Address, {'L',true}}, {Shortcut::NewTab,{'T',true}}, {Shortcut::CloseTab,{'W',true}},
      {Shortcut::NewWindow,{'N',true}}, {Shortcut::NewPrivateWindow,{'N',true,true}}, {Shortcut::RecentTab,{9,true}}, {Shortcut::RecentTabBack,{9,true,true}}, {Shortcut::NextTab,{34,true}}, {Shortcut::PreviousTab,{33,true}},
      {Shortcut::Sidebar,{'S',true}}, {Shortcut::Pin,{'D',true}}, {Shortcut::Back,{37,false,false,true}},
      {Shortcut::Forward,{39,false,false,true}}, {Shortcut::Reload,{'R',true}}, {Shortcut::Reload,{116}},
      {Shortcut::ReloadBypass,{'R',true,true}}, {Shortcut::ReloadBypass,{116,false,true}}, {Shortcut::Stop,{27}},
      {Shortcut::CloseWindow,{115,false,false,true}}, {Shortcut::DevTools,{123}}, {Shortcut::DevTools,{'I',true,true}},
      {Shortcut::Fullscreen,{122}}, {Shortcut::History,{'H',true}}, {Shortcut::Downloads,{'J',true}},
      {Shortcut::HistoryPage,{'H',true,true}}, {Shortcut::ReopenClosed,{'T',true,true}}, {Shortcut::Settings,{188,true}},
      {Shortcut::PictureInPicture,{'P',false,false,true}}, {Shortcut::Print,{'P',true}}, {Shortcut::CopyUrl,{'C',true,true}}, {Shortcut::Capture,{'5',true,true}},
      {Shortcut::SplitFocusLeft,{37,true,false,true}}, {Shortcut::SplitFocusRight,{39,true,false,true}}, {Shortcut::SplitFocusUp,{38,true,false,true}},
      {Shortcut::SplitFocusDown,{40,true,false,true}}, {Shortcut::SplitExpand,{13,true,false,true}},
    };
    for (int i = 0; i < 9; ++i) result.push_back({static_cast<Shortcut>(static_cast<int>(Shortcut::Space1)+i), {'1'+i,true}});
    return result;
  }(); return entries;
}
const Preferences& Current(const std::shared_ptr<State>& state) {
  static const Preferences defaults;
  return state && state->session && state->session->preferences ? state->session->preferences->Get() : defaults;
}
std::vector<Entry> Effective(const Preferences& prefs) {
  std::vector<Entry> entries;
  for (const auto& entry : Defaults()) if (!prefs.shortcuts.contains(std::string(kShortcutNames[static_cast<int>(entry.action)]))) entries.push_back(entry);
  for (const auto& [name,key] : prefs.shortcuts) if (const auto action = ShortcutNamed(name)) entries.push_back({*action,key});
  return entries;
}
bool Editable(Shortcut action) {
  return action != Shortcut::Stop && action != Shortcut::CloseWindow && action != Shortcut::FocusPage &&
      (action < Shortcut::Space1 || action > Shortcut::Space9);
}
std::string Title(Shortcut action, std::string_view locale = "en") {
  switch(action) {
    case Shortcut::Address:return InterfaceText(locale, InterfaceMessage::ShortcutAddress); case Shortcut::NewTab:return InterfaceText(locale, InterfaceMessage::ShortcutNewTab);
    case Shortcut::NewWindow:return InterfaceText(locale, InterfaceMessage::ShortcutNewWindow); case Shortcut::NewPrivateWindow:return InterfaceText(locale, InterfaceMessage::ShortcutNewPrivateWindow); case Shortcut::CloseTab:return InterfaceText(locale, InterfaceMessage::ShortcutCloseTab);
    case Shortcut::NextTab:return InterfaceText(locale, InterfaceMessage::ShortcutNextTab); case Shortcut::PreviousTab:return InterfaceText(locale, InterfaceMessage::ShortcutPreviousTab);
    case Shortcut::Sidebar:return InterfaceText(locale, InterfaceMessage::ShortcutSidebar); case Shortcut::Pin:return InterfaceText(locale, InterfaceMessage::ShortcutPin);
    case Shortcut::Back:return InterfaceText(locale, InterfaceMessage::ShortcutBack); case Shortcut::Forward:return InterfaceText(locale, InterfaceMessage::ShortcutForward);
    case Shortcut::Reload:return InterfaceText(locale, InterfaceMessage::ShortcutReload); case Shortcut::ReloadBypass:return InterfaceText(locale, InterfaceMessage::ShortcutReloadBypass);
    case Shortcut::Stop:return InterfaceText(locale, InterfaceMessage::ShortcutStop); case Shortcut::CloseWindow:return InterfaceText(locale, InterfaceMessage::ShortcutCloseWindow);
    case Shortcut::DevTools:return InterfaceText(locale, InterfaceMessage::ShortcutDevTools); case Shortcut::History:return InterfaceText(locale, InterfaceMessage::ShortcutHistory);
    case Shortcut::Downloads:return InterfaceText(locale, InterfaceMessage::ShortcutDownloads); case Shortcut::HistoryPage:return InterfaceText(locale, InterfaceMessage::ShortcutHistoryPage);
    case Shortcut::ReopenClosed:return InterfaceText(locale, InterfaceMessage::ShortcutReopenClosed); case Shortcut::Fullscreen:return InterfaceText(locale, InterfaceMessage::ShortcutFullscreen);
    case Shortcut::Settings:return InterfaceText(locale, InterfaceMessage::ShortcutSettings); case Shortcut::PictureInPicture:return InterfaceText(locale, InterfaceMessage::ShortcutPictureInPicture);
    case Shortcut::Print:return InterfaceText(locale, InterfaceMessage::ShortcutPrint); case Shortcut::CopyUrl:return InterfaceText(locale, InterfaceMessage::ShortcutCopyUrl);
    case Shortcut::Capture:return InterfaceText(locale, InterfaceMessage::ShortcutCapture); case Shortcut::Screenshot:return InterfaceText(locale, InterfaceMessage::ShortcutScreenshot); case Shortcut::FullPageScreenshot:return InterfaceText(locale, InterfaceMessage::ShortcutFullPageScreenshot);
    case Shortcut::RecentTab:return InterfaceText(locale, InterfaceMessage::ShortcutRecentTab); case Shortcut::RecentTabBack:return InterfaceText(locale, InterfaceMessage::ShortcutRecentTabBack);
    case Shortcut::SplitFocusLeft:return InterfaceText(locale, InterfaceMessage::ShortcutSplitFocusLeft); case Shortcut::SplitFocusRight:return InterfaceText(locale, InterfaceMessage::ShortcutSplitFocusRight);
    case Shortcut::SplitFocusUp:return InterfaceText(locale, InterfaceMessage::ShortcutSplitFocusUp); case Shortcut::SplitFocusDown:return InterfaceText(locale, InterfaceMessage::ShortcutSplitFocusDown);
    case Shortcut::SplitExpand:return InterfaceText(locale, InterfaceMessage::ShortcutSplitExpand);
    case Shortcut::FocusPage:return InterfaceText(locale, InterfaceMessage::ShortcutFocusPage);
    default:return InterfaceTextWithName(locale, InterfaceMessage::ShortcutSpace,
        std::to_string(static_cast<int>(action)-static_cast<int>(Shortcut::Space1)+1));
  }
}
std::string Label(const KeyBinding& key) {
  std::string result = key.ctrl ? "Ctrl + " : ""; if (key.alt) result += "Alt + "; if (key.shift) result += "Shift + ";
  if ((key.key >= 'A' && key.key <= 'Z') || (key.key >= '0' && key.key <= '9')) result += static_cast<char>(key.key);
  else if (key.key >= 112 && key.key <= 123) result += "F" + std::to_string(key.key-111);
  else if (key.key == 33) result += "Page Up"; else if (key.key == 34) result += "Page Down";
  else if (key.key == 9) result += "Tab"; else if (key.key == 27) result += "Esc";
  else if (key.key == 37) result += "Left"; else if (key.key == 39) result += "Right";
  else if (key.key == 38) result += "Up"; else if (key.key == 40) result += "Down";
  else if (key.key == 188) result += ","; else if (key.key == 32) result += "Space"; else if (key.key == 13) result += "Enter";
  else result += "Key " + std::to_string(key.key);
  return result;
}
std::string Labels(const std::vector<Entry>& entries, Shortcut action) {
  std::string result;
  for (const auto& e : entries) if (e.action == action) { if (!result.empty()) result += " / "; result += Label(e.key); }
  return result;
}
}
bool ConfigureShortcut(Preferences& prefs, const std::string& name, const KeyBinding* binding, std::string& error) {
  const auto action = ShortcutNamed(name);
  if (!action || !Editable(*action)) { error = "This shortcut is fixed."; return false; }
  if (!binding) {
    auto candidate = prefs; candidate.shortcuts.erase(name); const auto effective = Effective(candidate);
    for (const auto& restored : effective) if (restored.action == *action)
      for (const auto& other : effective) if (other.action != *action && other.key == restored.key) {
        error = "Default is already used for " + Title(other.action) + ". Reset all shortcuts or change it first."; return false;
      }
    prefs = std::move(candidate); return true;
  }
  const auto& key = *binding;
  const bool supported = (key.key >= 'A' && key.key <= 'Z') || (key.key >= '0' && key.key <= '9') ||
      (key.key >= 112 && key.key <= 123) || key.key == 9 || key.key == 188 || (key.key >= 37 && key.key <= 40);
  const bool editing = key.ctrl && (key.key == 'A' || key.key == 'C' || key.key == 'V' || key.key == 'X' ||
      key.key == 'Y' || key.key == 'Z' || key.key == 'F' || key.key == 'P' || key.key == 'O' || key.key == '0');
  if (!supported || editing || (key.ctrl && key.alt) || (!key.ctrl && !key.alt && (key.key < 112 || key.key > 123)) ||
      (key.alt && (key.key == 9 || key.key == 115))) { error = "Use Ctrl/Alt with a key, or an F-key. Windows shortcuts are reserved."; return false; }
  for (const auto& entry : Effective(prefs)) if (entry.action != *action && entry.key == key) {
    error = "Already used for " + Title(entry.action) + ". Change that shortcut first."; return false;
  }
  prefs.shortcuts[name] = key; return true;
}
// Settings rejects a longer list (MAX_SHORTCUTS in browser-contracts, the binding limit in preferences.cpp):
// with 41 shortcuts its window never came up. Raise all three together.
static_assert(std::size(kShortcutNames) <= 64, "more shortcuts than Settings accepts");
CefRefPtr<CefListValue> DescribeShortcuts(const Preferences& prefs, std::string_view locale) {
  auto rows = CefListValue::Create(); const auto effective = Effective(prefs);
  size_t index = 0;
  for (int i = 0; i < static_cast<int>(std::size(kShortcutNames)); ++i) {
    const auto action = static_cast<Shortcut>(i); const auto label = Labels(effective,action); if (label.empty()) continue;
    auto row = CefDictionaryValue::Create(); const auto name = std::string(kShortcutNames[i]);
    row->SetString("action",name); row->SetString("title",Title(action, locale)); row->SetString("label",label);
    row->SetString("defaultLabel",Labels(Defaults(),action)); row->SetBool("custom",prefs.shortcuts.contains(name));
    row->SetBool("editable",Editable(action)); rows->SetDictionary(index++,row);
  } return rows;
}
std::optional<KeyBinding> PrimaryShortcut(const Preferences& prefs, Shortcut action) {
  for (const auto& entry : Effective(prefs)) if (entry.action == action) return entry.key;
  return std::nullopt;
}
CefRefPtr<CefDictionaryValue> ShortcutLabels(const Preferences& prefs) {
  auto labels = CefDictionaryValue::Create(); const auto effective = Effective(prefs);
  for (int i = 0; i < static_cast<int>(std::size(kShortcutNames)); ++i) labels->SetString(std::string(kShortcutNames[i]),Labels(effective,static_cast<Shortcut>(i)));
  return labels;
}
std::optional<Shortcut> ShortcutKeyFor(const std::shared_ptr<State>& state, int key, bool ctrl, bool shift, bool alt) {
  if (!ctrl && !alt && (key < 112 || key > 123)) return ShortcutKey(key,ctrl,shift,alt);
  const auto& prefs = Current(state); const KeyBinding chord{key,ctrl,shift,alt};
  for (const auto& entry : Effective(prefs)) if (entry.key == chord) return entry.action;
  const auto original = ShortcutKey(key,ctrl,shift,alt);
  if (original && prefs.shortcuts.contains(std::string(kShortcutNames[static_cast<int>(*original)]))) return Shortcut::Suppressed;
  return original;
}
void RefreshShortcuts(const std::shared_ptr<State>& state) {
  if (!state || !state->window) return;
  state->window->RemoveAllAccelerators();
  const auto entries = Effective(Current(state));
  for (size_t i = 0; i < entries.size(); ++i) {
    const auto& key = entries[i].key;
    state->window->SetAccelerator(60000+static_cast<int>(i),key.key,key.shift,key.ctrl,key.alt,true);
  }
}
bool ExecuteAccelerator(const std::shared_ptr<State>& state, int id) {
  const auto entries = Effective(Current(state));
  return id >= 60000 && id-60000 < static_cast<int>(entries.size()) && ExecuteShortcut(state,entries[id-60000].action);
}
}  // namespace red
