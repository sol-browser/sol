#pragma once
#include "integrations/providers.h"
#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <vector>
namespace red {
struct MenuNode {
  std::string id, parent, title;
  std::string url, current_url;  // Saved address and the live page's address.
  bool folder = false, pinned = false, favorite = false, archived = false, live = false, closing = false, expanded = false;
  int position = 0;
  bool custom_title = false;
  bool muted = false;
  std::string space;  // Top-level Pinned/Today entries only.
  bool x_badge_hidden = false;
};
struct MenuSpace { std::string id, name; };
struct MenuProfile { std::string id, name; };
struct SidebarMenuItem {
  std::string label, action, target;
  bool enabled = true;
  std::vector<SidebarMenuItem> children;
  bool checked = false;  // Shown with a check mark (the current space).
};
inline std::vector<SidebarMenuItem> SpaceMenuItems(const std::vector<MenuSpace>& spaces, const std::vector<MenuProfile>& profiles,
                                                  const std::string& active, const std::string& profile) {
  const auto space=std::find_if(spaces.begin(),spaces.end(),[&](const auto& s){return s.id==active;});
  if (space==spaces.end()) return {};
  SidebarMenuItem selection{"Set Profile","","",true,{}};
  for (const auto& item : profiles) selection.children.push_back({item.name,"setProfile",item.id,true,{},item.id==profile});
  selection.children.push_back({"","","",false,{}});
  selection.children.push_back({"New Profile...","newProfile","",profiles.size()<24,{}});
  SidebarMenuItem transfer{"Export \""+space->name+"\" into Folder","","",spaces.size()>1,{}};
  for (const auto& item : spaces) if (item.id!=active) transfer.children.push_back({item.name,"exportSpace",item.id,true,{}});
  return {{"Edit Theme...","editTheme","",true,{}},selection,{"","","",false,{}},
    {"New Folder","newFolder","",true,{}},transfer,{"","","",false,{}},{"Delete Space","deleteSpace","",spaces.size()>1,{}}};
}
// |spaces|: every space in switcher order; |active|: the one on screen.
inline std::vector<SidebarMenuItem> SidebarMenuItems(const std::vector<MenuNode>& nodes, const std::string& id,
                                                     const std::vector<MenuSpace>& spaces = {}, const std::string& active = "", bool standalone = false) {
  if (standalone) {
    if (id.empty()) return {{"New tab", "newTab", "", true, {}}};
    const auto found = std::find_if(nodes.begin(), nodes.end(), [&](const auto& n) { return n.id == id && !n.folder && !n.archived && !n.closing; });
    if (found == nodes.end()) return {};
    std::vector<SidebarMenuItem> items = {{"Open tab", "activate", "", true, {}}, {"Rename", "rename", "", true, {}},
      {"Duplicate tab", "duplicate", "", true, {}}, {"Move to new window", "detach", "", true, {}}};
    if (found->live) items.push_back({found->muted ? "Unmute tab" : "Mute tab", found->muted ? "unmute" : "mute", "", true, {}});
    items.push_back({"Close tab", "close", "", true, {}}); return items;
  }
  if (id.empty()) return {{"New tab", "newTab", "", true, {}}, {"New folder", "newFolder", "", true, {}},
    {"", "", "", false, {}}, {"New space", "newSpace", "", true, {}}, {"Archive", "showArchive", "", true, {}},
    {"", "", "", false, {}}, {"Edit theme", "editTheme", "", true, {}}};
  const auto found = std::find_if(nodes.begin(), nodes.end(), [&](const auto& n) { return n.id == id; });
  if (found == nodes.end() || found->closing) return {};
  if (found->archived) return found->folder ? std::vector<SidebarMenuItem>{} : std::vector<SidebarMenuItem>{{"Restore tab", "restore", "", true, {}}};
  const auto& n = *found;
  std::vector<SidebarMenuItem> items;
  auto add = [&](std::string label, std::string action, std::string target = "") { items.push_back({std::move(label), std::move(action), std::move(target), true, {}}); };
  auto separator = [&]() { items.push_back({"", "", "", false, {}}); };
  if (!n.folder) {
    // Tab menus (Arc layout, from the user's reference): Pinned, Favorite and
    // Today differ only in their editing and closing groups. Moving between
    // sections and folders is by dragging; a pinned tab resets from its icon.
    const bool saved = n.pinned || n.favorite;
    add("Copy Link", "copyLink");
    separator();
    add("Rename\u2026", "rename");
    if (n.custom_title) add("Use Website Title", "automaticTitle");
    items.push_back({n.muted ? "Unmute" : "Mute", n.muted ? "unmute" : "mute", "", n.live, {}});
    if (saved && XNotificationSite(n.url))
      add(n.x_badge_hidden ? "Show Notification Badge" : "Hide Notification Badge", n.x_badge_hidden ? "showXBadge" : "hideXBadge");
    if (saved) items.push_back({"Edit Pinned Page", "", "", true, {
      {"Replace Pinned URL with Current", "setHome", "", n.live && !n.current_url.empty() && n.current_url != n.url, {}},
      {"Edit\u2026", "editPinned", "", true, {}}}});
    separator();
    add(saved ? "Open in Split View" : "Add Split View", "split");
    add("Duplicate", "duplicate");
    std::vector<SidebarMenuItem> moves;
    if (!n.favorite && n.parent.empty())
      for (const auto& space : spaces) moves.push_back({space.name, space.id == n.space ? "" : "space", space.id, true, {}, space.id == n.space});
    if (!moves.empty()) moves.push_back({"", "", "", false, {}});
    if (!n.favorite && n.parent.empty()) moves.push_back({"New Space\u2026", "newSpaceWith", "", spaces.size() < 24, {}});
    moves.push_back({"New Window", "detach", "", true, {}});
    items.push_back({"Move to", "", "", true, std::move(moves)});
    separator();
    // Saved tabs keep their row: "close" frees the page's memory (it loads again on click).
    if (saved) items.push_back({"Unload Tab", "close", "", n.live, {}});
    if (n.pinned) add("Remove Pin", "today");
    else if (n.favorite) add("Remove Favorite", "today");
    else {
      std::vector<const MenuNode*> today;
      for (const auto& other : nodes)
        if (!other.folder && !other.pinned && !other.favorite && !other.archived && !other.closing && other.parent.empty() && other.space == n.space) today.push_back(&other);
      std::sort(today.begin(), today.end(), [](const auto* a, const auto* b) { return a->position < b->position; });
      const auto at = std::find(today.begin(), today.end(), &n);
      items.push_back({"Archive Other Tabs", "archiveOthers", "", today.size() > 1, {}});
      add("Archive Tab", "close");
      items.push_back({"Archive Tabs Below", "archiveBelow", "", at != today.end() && at + 1 != today.end(), {}});
    }
    std::vector<SidebarMenuItem> clean;
    for (auto& item : items) if (!item.label.empty() || (!clean.empty() && !clean.back().label.empty())) clean.push_back(std::move(item));
    if (!clean.empty() && clean.back().label.empty()) clean.pop_back();
    return clean;
  }
  if (!n.folder) add(n.live ? "Switch to tab" : "Open tab", "activate");
  else add(n.expanded ? "Collapse folder" : "Expand folder", "expand");
  if (!n.folder) add(n.pinned ? "Unpin tab" : "Pin tab", n.pinned ? "today" : "pinned");
  add("Rename", "rename");
  if (!n.folder && n.custom_title) add("Use website title", "automaticTitle");
  if (n.folder) add("New folder inside", "newFolder");
  if (n.pinned && !n.folder) add("Reset to saved address", "reset");
  if (!n.folder) add("Duplicate tab", "duplicate");
  if (!n.folder) add("Move to new window", "detach");
  if (!n.folder && n.live) add(n.muted ? "Unmute tab" : "Mute tab", n.muted ? "unmute" : "mute");
  separator();
  std::vector<SidebarMenuItem> destinations;
  if (n.folder && !n.parent.empty()) destinations.push_back({"Pinned", "pinned", "", true, {}});
  if (!n.folder) {
    if (!n.favorite) destinations.push_back({"Favorites", "favorites", "", true, {}});
    if (n.favorite || !n.pinned || !n.parent.empty()) destinations.push_back({"Pinned", "pinned", "", true, {}});
    if (n.pinned) destinations.push_back({"Today", "today", "", true, {}});
  }
  std::vector<SidebarMenuItem> folders;
  std::map<std::string, const MenuNode*> by_id;
  for (const auto& node : nodes) by_id[node.id] = &node;
  // The space of an entry: its top-level ancestor's ("" for Favorites).
  auto space_of = [&](const MenuNode& node) {
    const MenuNode* current = &node;
    for (int depth = 0; current && !current->parent.empty() && depth < 64; ++depth) {
      const auto parent = by_id.find(current->parent);
      current = parent == by_id.end() ? nullptr : parent->second;
    }
    return current && !current->favorite ? current->space : std::string();
  };
  const auto here = n.favorite ? active : space_of(n);
  for (const auto& folder : nodes) {
    if (!folder.folder || folder.archived || folder.closing || folder.id == n.parent) continue;
    if (!spaces.empty() && space_of(folder) != here) continue;  // Folders of this space only.
    std::set<std::string> ancestors;
    auto current = &folder; bool invalid = false;
    std::string path = folder.title;
    while (current) {
      if (current->id == id || !ancestors.insert(current->id).second || ancestors.size() > 64) { invalid = true; break; }
      const auto parent = by_id.find(current->parent);
      current = parent == by_id.end() ? nullptr : parent->second;
      if (current) path = current->title + " / " + path;
    }
    if (!invalid) folders.push_back({path, "folder", folder.id, true, {}});
    // Native menus remain bounded; drag/drop still reaches every folder.
    if (folders.size() >= 200) break;
  }
  if (!folders.empty()) destinations.push_back({"Folder", "", "", true, std::move(folders)});
  // Top-level Pinned/Today entries can move to another space.
  if (n.parent.empty() && !n.favorite && spaces.size() > 1) {
    std::vector<SidebarMenuItem> others;
    for (const auto& space : spaces) if (space.id != n.space) others.push_back({space.name, "space", space.id, true, {}});
    if (!others.empty()) destinations.push_back({"Space", "", "", true, std::move(others)});
  }
  if (!destinations.empty()) items.push_back({"Move to", "", "", true, std::move(destinations)});
  std::vector<const MenuNode*> siblings;
  for (const auto& other : nodes)
    if (other.parent == n.parent && other.pinned == n.pinned && other.favorite == n.favorite && !other.archived &&
        (!n.parent.empty() || n.favorite || other.space == n.space)) siblings.push_back(&other);
  std::sort(siblings.begin(), siblings.end(), [](const auto* a, const auto* b) { return a->position < b->position; });
  const auto at = std::find(siblings.begin(), siblings.end(), &n);
  if (at != siblings.begin()) add("Move up", "up");
  if (at != siblings.end() && at + 1 != siblings.end()) add("Move down", "down");
  if (n.folder) {
    const bool empty = std::none_of(nodes.begin(), nodes.end(), [&](const auto& child) { return child.parent == id; });
    separator(); items.push_back({empty ? "Delete folder" : "Delete folder…", "removeFolder", "", true, {}});
  } else {
    separator();
    if (n.live || !n.pinned) add(n.pinned ? "Close page, keep pin" : "Archive tab", "close");
    if (n.pinned) add("Remove from sidebar", "archive");
  }
  // Empty groups do not leave consecutive separators.
  std::vector<SidebarMenuItem> clean;
  for (auto& item : items) if (!item.label.empty() || (!clean.empty() && !clean.back().label.empty())) clean.push_back(std::move(item));
  if (!clean.empty() && clean.back().label.empty()) clean.pop_back();
  return clean;
}
}  // namespace red
