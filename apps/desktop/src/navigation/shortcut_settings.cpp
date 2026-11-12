#include "navigation/shortcut_settings.h"
#include "app/session.h"
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
std::string Title(Shortcut action) {
  switch(action) {
    case Shortcut::Address:return "Address bar"; case Shortcut::NewTab:return "New tab";
    case Shortcut::NewWindow:return "New window"; case Shortcut::NewPrivateWindow:return "New private window"; case Shortcut::CloseTab:return "Close tab";
    case Shortcut::NextTab:return "Next tab"; case Shortcut::PreviousTab:return "Previous tab";
    case Shortcut::Sidebar:return "Toggle sidebar"; case Shortcut::Pin:return "Pin tab";
    case Shortcut::Back:return "Back"; case Shortcut::Forward:return "Forward";
    case Shortcut::Reload:return "Reload"; case Shortcut::ReloadBypass:return "Reload without cache";
    case Shortcut::Stop:return "Stop / Escape"; case Shortcut::CloseWindow:return "Close window";
    case Shortcut::DevTools:return "Developer tools"; case Shortcut::History:return "History";
    case Shortcut::Downloads:return "Downloads"; case Shortcut::HistoryPage:return "Full history";
    case Shortcut::ReopenClosed:return "Reopen closed tab"; case Shortcut::Fullscreen:return "Fullscreen";
    case Shortcut::Settings:return "Settings"; case Shortcut::PictureInPicture:return "Picture-in-picture";
    case Shortcut::Print:return "Print"; case Shortcut::CopyUrl:return "Copy address without trackers";
    case Shortcut::Capture:return "Capture (area or element)"; case Shortcut::Screenshot:return "Screenshot"; case Shortcut::FullPageScreenshot:return "Full page screenshot";
    case Shortcut::RecentTab:return "Recent tab"; case Shortcut::RecentTabBack:return "Recent tab (back)";
    case Shortcut::SplitFocusLeft:return "Split View: pane to the left"; case Shortcut::SplitFocusRight:return "Split View: pane to the right";
    case Shortcut::SplitFocusUp:return "Split View: pane above"; case Shortcut::SplitFocusDown:return "Split View: pane below";
    case Shortcut::SplitExpand:return "Split View: expand or restore pane";
    default:return "Space " + std::to_string(static_cast<int>(action)-static_cast<int>(Shortcut::Space1)+1);
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
CefRefPtr<CefListValue> DescribeShortcuts(const Preferences& prefs) {
  auto rows = CefListValue::Create(); const auto effective = Effective(prefs);
  size_t index = 0;
  for (int i = 0; i < static_cast<int>(std::size(kShortcutNames)); ++i) {
    const auto action = static_cast<Shortcut>(i); const auto label = Labels(effective,action); if (label.empty()) continue;
    auto row = CefDictionaryValue::Create(); const auto name = std::string(kShortcutNames[i]);
    row->SetString("action",name); row->SetString("title",Title(action)); row->SetString("label",label);
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
