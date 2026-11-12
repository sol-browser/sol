#include "windows/sidebar_menu.h"
#include "windows/sidebar_peek.h"
#include "workspace/controller.h"
#include "windows/workspace_layout.h"
#include "windows/focus.h"
#include "app/session.h"
#include "include/base/cef_callback.h"
#include "include/wrapper/cef_closure_task.h"
#include "include/cef_parser.h"
#include <optional>
namespace red {
namespace {
std::string Label(const std::string& raw) {
  std::string result;
  for (const unsigned char c : raw) { if (c < 32 || c == 127) continue; result += static_cast<char>(c); }
  auto wide = CefString(result).ToWString();
  if (wide.size() > 100) {
    wide.resize(99);
    if (!wide.empty() && wide.back() >= 0xd800 && wide.back() <= 0xdbff) wide.pop_back();
    wide += L"\u2026";
  }
  return CefString(wide).ToString();
}
// Row icons by action (Menu.tsx draws them).
std::string IconFor(const std::string& action) {
  static const std::map<std::string, std::string> icons = {
    {"activate", "arrowRight"}, {"rename", "pencil"}, {"renamePane", "pencil"}, {"duplicate", "copy"}, {"detach", "window"}, {"mute", "volumeX"},
    {"unmute", "volume"}, {"close", "close"}, {"newTab", "plus"}, {"newFolder", "folder"}, {"newSpace", "spaces"}, {"showArchive", "archive"},
    {"restore", "undo"}, {"pinned", "pin"}, {"today", "pinOff"}, {"favorites", "star"}, {"split", "columns"}, {"swapSplit", "swap"},
    {"unsplit", "columns"}, {"archive", "archive"}, {"archiveOthers", "archive"}, {"archiveBelow", "archive"}, {"copyLink", "link"},
    {"up", "arrowUp"}, {"down", "arrowDown"}, {"expand", "folder"}, {"removeFolder", "trash"}, {"setHome", "home"}, {"reset", "undo"},
    {"automaticTitle", "text"}, {"editPinned", "pencil"}, {"newSpaceWith", "spaces"}, {"editTheme", "palette"},
    {"showXBadge", "bell"}, {"hideXBadge", "bell"}, {"enableIntegration", "settings"}, {"disableIntegration", "settings"}};
  const auto found = icons.find(action);
  return found == icons.end() ? "" : found->second;
}
}
void SidebarMenu::Fill(MenuPopup::Builder& menu, const std::vector<SidebarMenuItem>& items) {
  for (const auto& item : items) {
    if (item.label.empty()) { menu.Separator(); continue; }
    const int command = 27000 + static_cast<int>(actions_.size());
    actions_[command] = {item.action, item.target};
    if (!item.children.empty()) { Fill(menu.Submenu(Label(item.label), IconFor(item.action), item.enabled), item.children); continue; }
    menu.Add(Label(item.label), item.checked ? "" : IconFor(item.action), [weak = weak_from_this(), generation = generation_ + 1, command] {
      if (auto self = weak.lock()) self->Choose(generation, command);
    }, "", item.enabled, item.checked);
  }
}
void SidebarMenu::Open(const std::string& id, int x, int y, int64_t query, CefRefPtr<CefMessageRouterBrowserSide::Callback> callback,
                       CefRefPtr<CefBrowser> source, bool space_menu) {
  auto state = state_.lock();
  // The docked sidebar, or the floating one while the docked one is hidden.
  const bool shown = state && (state->sidebar_visible || (state->sidebar_peek && state->sidebar_peek->is_open()));
  if (!state || !state->window || !state->workspace || !shown || state->workspace->WindowClosing()) { callback->Failure(409, "Sidebar is unavailable"); return; }
  const auto bounds = state->window->GetClientAreaBoundsInScreen();
  if (x < 0 || x >= SidebarWidth(state->sidebar_width, bounds.width) || y < kToolbarHeight || y >= bounds.height) { callback->Failure(400, "Invalid menu position"); return; }
  const auto snapshot = state->workspace->Snapshot();
  if (!snapshot->GetBool("ready")) { callback->Failure(409, "Workspace is loading"); return; }
  std::vector<MenuNode> nodes; const auto rows = snapshot->GetList("nodes");
  for (size_t i = 0; i < rows->GetSize(); ++i) {
    const auto row = rows->GetDictionary(i);
    nodes.push_back({row->GetString("id"), row->GetString("parentId"), row->GetString("title"), row->GetString("url"), row->GetString("currentUrl"), row->GetString("kind") == "folder",
      row->GetBool("pinned"), row->GetBool("favorite"), row->GetBool("archived"), row->GetBool("live"), row->GetBool("closing"), row->GetBool("expanded"), row->GetInt("position"), row->GetBool("customTitle"), row->GetBool("muted"),
      row->GetString("space"), row->GetBool("xBadgeHidden")});
  }
  std::vector<MenuSpace> spaces; const auto space_rows = snapshot->GetList("spaces");
  for (size_t i = 0; space_rows && i < space_rows->GetSize(); ++i)
    spaces.push_back({space_rows->GetDictionary(i)->GetString("id"), space_rows->GetDictionary(i)->GetString("name")});
  auto items = SidebarMenuItems(nodes, id, spaces, snapshot->GetString("activeSpace"), state->standalone_window);
  if (space_menu) {
    if (state->standalone_window || !id.empty()) { callback->Failure(403,"Space actions are unavailable"); return; }
    std::vector<MenuProfile> profiles; const auto list=snapshot->GetList("profiles");
    for (size_t i=0;list && i<list->GetSize();++i) profiles.push_back({list->GetDictionary(i)->GetString("id"),list->GetDictionary(i)->GetString("name")});
    items=SpaceMenuItems(spaces,profiles,snapshot->GetString("activeSpace"),snapshot->GetString("activeProfile"));
  }
  CefRefPtr<CefDictionaryValue> entry, active;
  for (size_t i = 0; i < rows->GetSize(); ++i) {
    auto row = rows->GetDictionary(i); if (row->GetString("id") == id) entry = row;
    if (row->GetString("id") == snapshot->GetString("activeId")) active = row;
  }
  if (entry && entry->GetString("splitAnchor") == id) {
    std::optional<SidebarMenuItem> movement;
    std::optional<SidebarMenuItem> badge;
    for (const auto& item : items) if (item.action == "showXBadge" || item.action == "hideXBadge") badge = item;
    for (auto item : items) if (item.label == "Move to") {
      std::erase_if(item.children,[](const auto& child) { return child.action == "favorites"; });
      if (!item.children.empty()) movement = std::move(item);
    }
    // One rename per pane, in the order they appear on screen.
    const auto panes = entry->GetList("splitPanes");
    const char* ordinals[] = {"first","second","third","fourth"};
    items = {{"Open split view","activate","",true,{}}};
    for (size_t pane = 0; panes && pane < panes->GetSize() && pane < 4; ++pane)
      items.push_back({std::string("Rename ") + ordinals[pane] + " tab","renamePane",panes->GetString(pane).ToString(),true,{}});
    items.push_back({entry->GetBool("pinned") ? "Unpin split view" : "Pin split view",entry->GetBool("pinned") ? "today" : "pinned","",true,{}});
    if (panes && panes->GetSize() == 2) items.push_back({"Swap panes","swapSplit","",true,{}});
    items.push_back({"Separate tabs","unsplit","",true,{}});
    if (movement) items.push_back(std::move(*movement));
    if (badge) items.push_back(std::move(*badge));
  } else if (entry && active && entry->GetString("kind") == "tab" && !entry->GetBool("archived") && !entry->GetBool("closing") &&
      entry->GetString("id") != active->GetString("id") && !entry->HasKey("splitAnchor") &&
      (!active->HasKey("splitAnchor") || (active->GetList("splitPanes") && active->GetList("splitPanes")->GetSize() < static_cast<size_t>(kMaxSplitPanes))) &&
      !(entry->GetBool("favorite") && active->GetBool("favorite") && !active->HasKey("splitAnchor"))) {
    // With the page on screen in a split of fewer than four, the tab joins it.
    for (auto& item : items) if (item.action == "split") item.target = active->GetString("id");
  }
  // Without another tab to pair with, the split item is unavailable.
  for (auto& item : items) if (item.action == "split" && item.target.empty()) item.enabled = false;
  if (!space_menu && entry && !state->private_window && !state->standalone_window && !entry->GetBool("closing") &&
      !entry->GetBool("archived") && entry->GetBool("pinned") && entry->GetString("kind") == "tab") {
    if (const auto* provider = FindIntegrationSetting(IntegrationForUrl(entry->GetString("url").ToString())); provider && provider->listed) {
      const bool enabled = BrowserIntegrationEnabled(state, provider->id);
      items.push_back({"", "", "", false, {}});
      items.push_back({std::string(enabled ? "Disable " : "Enable ") + std::string(provider->name) + " Integration (All Tabs)",
                       enabled ? "disableIntegration" : "enableIntegration", std::string(provider->id), true, {}});
    }
  }
  if (items.empty()) { callback->Failure(404, "Entry is unavailable"); return; }
  Cancel(); actions_.clear();
  MenuPopup::Builder menu; Fill(menu, items);  // Rows carry generation_ + 1, the opening below.
  ++generation_; query_ = query; callback_ = callback;
  selected_action_.clear(); selected_target_.clear(); source_ = source;
  auto details = CefDictionaryValue::Create(); details->SetString("id", id); details->SetInt("items", static_cast<int>(actions_.size()));
  details->SetBool("from_peek", source && source->GetIdentifier() == state->peek_id);
  state->Record("sidebar_menu_open", details);
  if (!state->menu_popup) state->menu_popup = std::make_shared<MenuPopup>(state);
  menu_open_ = true; Shown(generation_);
  state->menu_popup->Open(menu, CefPoint(bounds.x + x, bounds.y + y), "sidebar:" + std::to_string(generation_),
      [weak = weak_from_this(), generation = generation_] { if (auto self = weak.lock()) self->Closed(generation); });
}
void SidebarMenu::Complete(const std::string& action, const std::string& target) {
  if (!callback_) return;
  auto callback = callback_; callback_ = nullptr; query_ = -1;
  auto d = CefDictionaryValue::Create(); d->SetString("action", action); d->SetString("targetId", target);
  auto v = CefValue::Create(); v->SetDictionary(d); callback->Success(CefWriteJSON(v, JSON_WRITER_DEFAULT));
}
void SidebarMenu::Cancel(int64_t query, bool reply) {
  if (query >= 0 && query != query_) return;
  if (!menu_open_ && !callback_) return;
  if (reply) Complete("cancel"); else { callback_ = nullptr; query_ = -1; }
  ++generation_; shown_ = false;
  selected_action_.clear(); selected_target_.clear(); source_ = nullptr;
  if (auto state = state_.lock(); state && state->menu_popup && menu_open_) { menu_open_ = false; state->menu_popup->Close(false); }
  menu_open_ = false; actions_.clear();
}
void SidebarMenu::Execute(unsigned generation, int command) {
  if (generation != generation_) return;
  const auto item = actions_.find(command); if (item == actions_.end() || item->second.first.empty()) return;
  if (auto state = state_.lock()) { auto d = CefDictionaryValue::Create(); d->SetString("action", item->second.first); d->SetBool("rename", item->second.first == "rename"); state->Record("sidebar_menu_action", d); }
  selected_action_ = item->second.first; selected_target_ = item->second.second;
}
void SidebarMenu::Choose(unsigned generation, int command) {
  Execute(generation, command);
  Closed(generation);
}
void SidebarMenu::Shown(unsigned generation) { if (generation == generation_) { shown_ = true; if (auto state = state_.lock()) state->Record("sidebar_menu_shown"); } }
void SidebarMenu::Closed(unsigned generation) {
  // Once per opening: a chosen row finishes it before the menu reports going away.
  if (generation != generation_ || (!shown_ && !menu_open_)) return;
  const auto action = selected_action_.empty() ? "cancel" : selected_action_;
  const auto target = selected_target_;
  const auto source = source_;
  selected_action_.clear(); selected_target_.clear(); source_ = nullptr;
  shown_ = false; menu_open_ = false; actions_.clear();
  if (auto state = state_.lock()) state->Record("sidebar_menu_closed");
  // The Views menu restores native focus as it closes. Start inline editing
  // after that restoration, in the same trusted document which opened it.
  CefPostTask(TID_UI, base::BindOnce([](std::weak_ptr<SidebarMenu> weak, unsigned generation,
      std::string action, std::string target, CefRefPtr<CefBrowser> source) {
    if (auto self = weak.lock(); self && generation == self->generation_) {
      if ((action == "rename" || action == "renamePane") && source) FocusBrowser(source);
      self->Complete(action, target);
    }
  }, weak_from_this(), generation, std::string(action), target, source));
}
bool SidebarMenu::ProbeSelect(const std::string& wanted) {
  auto state = state_.lock();
  if (!state || !state->probe || !shown_ ||
      (!state->organization_probe && !(state->sidebar_peek_probe && wanted == "editPinned"))) return false;
  for (const auto& [command, action] : actions_) if (action.first == wanted) {
    Choose(generation_, command); if (state->menu_popup) state->menu_popup->Close(false); return true;
  }
  return false;
}
}  // namespace red
