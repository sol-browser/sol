#pragma once
#include "integrations/providers.h"
#include "interface_strings.h"
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
  bool move_group = false;
};
inline std::vector<SidebarMenuItem> SpaceMenuItems(const std::vector<MenuSpace>& spaces, const std::vector<MenuProfile>& profiles,
                                                  const std::string& active, const std::string& profile,
                                                  std::string_view locale = "en") {
  const auto label = [locale](InterfaceMessage entry) { return InterfaceText(locale, entry); };
  const auto space=std::find_if(spaces.begin(),spaces.end(),[&](const auto& s){return s.id==active;});
  if (space==spaces.end()) return {};
  SidebarMenuItem selection{label(InterfaceMessage::SidebarSetProfile),"","",true,{}};
  for (const auto& item : profiles) selection.children.push_back({item.name,"setProfile",item.id,true,{},item.id==profile});
  selection.children.push_back({"","","",false,{}});
  selection.children.push_back({label(InterfaceMessage::SidebarNewProfile),"newProfile","",profiles.size()<24,{}});
  SidebarMenuItem transfer{InterfaceTextWithName(locale, InterfaceMessage::SidebarExportSpace, space->name),"","",spaces.size()>1,{}};
  for (const auto& item : spaces) if (item.id!=active) transfer.children.push_back({item.name,"exportSpace",item.id,true,{}});
  return {{label(InterfaceMessage::SidebarEditThemeMore),"editTheme","",true,{}},selection,{"","","",false,{}},
    {label(InterfaceMessage::SidebarNewFolderTitle),"newFolder","",true,{}},transfer,{"","","",false,{}},{label(InterfaceMessage::SidebarDeleteSpace),"deleteSpace","",spaces.size()>1,{}}};
}
// |spaces|: every space in switcher order; |active|: the one on screen.
inline std::vector<SidebarMenuItem> SidebarMenuItems(const std::vector<MenuNode>& nodes, const std::string& id,
                                                     const std::vector<MenuSpace>& spaces = {}, const std::string& active = "", bool standalone = false,
                                                     std::string_view locale = "en") {
  const auto label = [locale](InterfaceMessage entry) { return InterfaceText(locale, entry); };
  if (standalone) {
    if (id.empty()) return {{label(InterfaceMessage::SidebarNewTab), "newTab", "", true, {}}};
    const auto found = std::find_if(nodes.begin(), nodes.end(), [&](const auto& n) { return n.id == id && !n.folder && !n.archived && !n.closing; });
    if (found == nodes.end()) return {};
    std::vector<SidebarMenuItem> items = {{label(InterfaceMessage::SidebarOpenTab), "activate", "", true, {}}, {label(InterfaceMessage::SidebarRename), "rename", "", true, {}},
      {label(InterfaceMessage::SidebarDuplicateTab), "duplicate", "", true, {}}, {label(InterfaceMessage::SidebarDetach), "detach", "", true, {}}};
    if (found->live) items.push_back({found->muted ? label(InterfaceMessage::SidebarUnmuteTab) : label(InterfaceMessage::SidebarMuteTab), found->muted ? "unmute" : "mute", "", true, {}});
    items.push_back({label(InterfaceMessage::SidebarCloseTab), "close", "", true, {}}); return items;
  }
  if (id.empty()) return {{label(InterfaceMessage::SidebarNewTab), "newTab", "", true, {}}, {label(InterfaceMessage::SidebarNewFolder), "newFolder", "", true, {}},
    {"", "", "", false, {}}, {label(InterfaceMessage::SidebarNewSpace), "newSpace", "", true, {}}, {label(InterfaceMessage::SidebarArchive), "showArchive", "", true, {}},
    {"", "", "", false, {}}, {label(InterfaceMessage::SidebarEditTheme), "editTheme", "", true, {}}};
  const auto found = std::find_if(nodes.begin(), nodes.end(), [&](const auto& n) { return n.id == id; });
  if (found == nodes.end() || found->closing) return {};
  if (found->archived) return found->folder ? std::vector<SidebarMenuItem>{} : std::vector<SidebarMenuItem>{{label(InterfaceMessage::SidebarRestoreTab), "restore", "", true, {}}};
  const auto& n = *found;
  std::vector<SidebarMenuItem> items;
  auto add = [&](std::string label, std::string action, std::string target = "") { items.push_back({std::move(label), std::move(action), std::move(target), true, {}}); };
  auto separator = [&]() { items.push_back({"", "", "", false, {}}); };
  if (!n.folder) {
    // Tab menus (Arc layout, from the user's reference): Pinned, Favorite and
    // Today differ only in their editing and closing groups. Moving between
    // sections and folders is by dragging; a pinned tab resets from its icon.
    const bool saved = n.pinned || n.favorite;
    add(label(InterfaceMessage::SidebarCopyLink), "copyLink");
    separator();
    add(label(InterfaceMessage::SidebarRenameMore), "rename");
    if (n.custom_title) add(label(InterfaceMessage::SidebarWebsiteTitle), "automaticTitle");
    items.push_back({n.muted ? label(InterfaceMessage::SidebarUnmute) : label(InterfaceMessage::SidebarMute), n.muted ? "unmute" : "mute", "", n.live, {}});
    if (saved && XNotificationSite(n.url))
      add(n.x_badge_hidden ? label(InterfaceMessage::SidebarShowBadge) : label(InterfaceMessage::SidebarHideBadge), n.x_badge_hidden ? "showXBadge" : "hideXBadge");
    if (saved) items.push_back({label(InterfaceMessage::SidebarEditPinned), "", "", true, {
      {label(InterfaceMessage::SidebarReplacePinned), "setHome", "", n.live && !n.current_url.empty() && n.current_url != n.url, {}},
      {label(InterfaceMessage::SidebarEditMore), "editPinned", "", true, {}}}});
    separator();
    add(saved ? label(InterfaceMessage::SidebarOpenSplit) : label(InterfaceMessage::SidebarAddSplit), "split");
    add(label(InterfaceMessage::SidebarDuplicate), "duplicate");
    std::vector<SidebarMenuItem> moves;
    if (!n.favorite && n.parent.empty())
      for (const auto& space : spaces) moves.push_back({space.name, space.id == n.space ? "" : "space", space.id, true, {}, space.id == n.space});
    if (!moves.empty()) moves.push_back({"", "", "", false, {}});
    if (!n.favorite && n.parent.empty()) moves.push_back({label(InterfaceMessage::SidebarNewSpaceMore), "newSpaceWith", "", spaces.size() < 24, {}});
    moves.push_back({label(InterfaceMessage::SidebarNewWindow), "detach", "", true, {}});
    items.push_back({label(InterfaceMessage::SidebarMoveTo), "", "", true, std::move(moves), false, true});
    separator();
    // Saved tabs keep their row: "close" frees the page's memory (it loads again on click).
    if (saved) items.push_back({label(InterfaceMessage::SidebarUnload), "close", "", n.live, {}});
    if (n.pinned) add(label(InterfaceMessage::SidebarUnpin), "today");
    else if (n.favorite) add(label(InterfaceMessage::SidebarRemoveFavorite), "today");
    else {
      std::vector<const MenuNode*> today;
      for (const auto& other : nodes)
        if (!other.folder && !other.pinned && !other.favorite && !other.archived && !other.closing && other.parent.empty() && other.space == n.space) today.push_back(&other);
      std::sort(today.begin(), today.end(), [](const auto* a, const auto* b) { return a->position < b->position; });
      const auto at = std::find(today.begin(), today.end(), &n);
      items.push_back({label(InterfaceMessage::SidebarArchiveOthers), "archiveOthers", "", today.size() > 1, {}});
      add(label(InterfaceMessage::SidebarArchiveTabTitle), "close");
      items.push_back({label(InterfaceMessage::SidebarArchiveBelow), "archiveBelow", "", at != today.end() && at + 1 != today.end(), {}});
    }
    std::vector<SidebarMenuItem> clean;
    for (auto& item : items) if (!item.label.empty() || (!clean.empty() && !clean.back().label.empty())) clean.push_back(std::move(item));
    if (!clean.empty() && clean.back().label.empty()) clean.pop_back();
    return clean;
  }
  // Folders: no expand/collapse (a click does it) and no move up/down (dragging does it), per the user, 2026-11-24.
  if (!n.folder) add(n.live ? label(InterfaceMessage::SidebarSwitchTab) : label(InterfaceMessage::SidebarOpenTab), "activate");
  if (!n.folder) add(n.pinned ? label(InterfaceMessage::SidebarUnpinTab) : label(InterfaceMessage::SidebarPinTab), n.pinned ? "today" : "pinned");
  add(label(InterfaceMessage::SidebarRename), "rename");
  if (n.folder) add(label(InterfaceMessage::SidebarFolderIcon), "folderIcon");
  if (!n.folder && n.custom_title) add(label(InterfaceMessage::SidebarWebsiteTitleLower), "automaticTitle");
  if (n.folder) add(label(InterfaceMessage::SidebarNestedFolder), "newFolder");
  if (n.pinned && !n.folder) add(label(InterfaceMessage::SidebarResetAddress), "reset");
  if (!n.folder) add(label(InterfaceMessage::SidebarDuplicateTab), "duplicate");
  if (!n.folder) add(label(InterfaceMessage::SidebarDetach), "detach");
  if (!n.folder && n.live) add(n.muted ? label(InterfaceMessage::SidebarUnmuteTab) : label(InterfaceMessage::SidebarMuteTab), n.muted ? "unmute" : "mute");
  separator();
  std::vector<SidebarMenuItem> destinations;
  if (n.folder && !n.parent.empty()) destinations.push_back({label(InterfaceMessage::SidebarPinned), "pinned", "", true, {}});
  if (!n.folder) {
    if (!n.favorite) destinations.push_back({label(InterfaceMessage::SidebarFavorites), "favorites", "", true, {}});
    if (n.favorite || !n.pinned || !n.parent.empty()) destinations.push_back({label(InterfaceMessage::SidebarPinned), "pinned", "", true, {}});
    if (n.pinned) destinations.push_back({label(InterfaceMessage::SidebarToday), "today", "", true, {}});
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
  if (!folders.empty()) destinations.push_back({label(InterfaceMessage::SidebarFolder), "", "", true, std::move(folders)});
  // Top-level Pinned/Today entries can move to another space.
  if (n.parent.empty() && !n.favorite && spaces.size() > 1) {
    std::vector<SidebarMenuItem> others;
    for (const auto& space : spaces) if (space.id != n.space) others.push_back({space.name, "space", space.id, true, {}});
    if (!others.empty()) destinations.push_back({label(InterfaceMessage::SidebarSpace), "", "", true, std::move(others)});
  }
  if (!destinations.empty()) items.push_back({label(InterfaceMessage::SidebarMoveTo), "", "", true, std::move(destinations), false, true});
  if (n.folder) {
    const bool empty = std::none_of(nodes.begin(), nodes.end(), [&](const auto& child) { return child.parent == id; });
    separator(); items.push_back({empty ? label(InterfaceMessage::SidebarDeleteFolder) : label(InterfaceMessage::SidebarDeleteFolderMore), "removeFolder", "", true, {}});
  } else {
    separator();
    if (n.live || !n.pinned) add(n.pinned ? label(InterfaceMessage::SidebarKeepPin) : label(InterfaceMessage::SidebarArchiveTab), "close");
    if (n.pinned) add(label(InterfaceMessage::SidebarRemoveSidebar), "archive");
  }
  // Empty groups do not leave consecutive separators.
  std::vector<SidebarMenuItem> clean;
  for (auto& item : items) if (!item.label.empty() || (!clean.empty() && !clean.back().label.empty())) clean.push_back(std::move(item));
  if (!clean.empty() && clean.back().label.empty()) clean.pop_back();
  return clean;
}
}  // namespace red
