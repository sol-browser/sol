#include "workspace/context_menu_model.h"
#include <cstdio>
using red::MenuNode;
using red::SidebarMenuItem;
MenuNode Node(const char* id, const char* parent = "", bool folder = false) {
  MenuNode n; n.id = id; n.parent = parent; n.title = id; n.folder = folder; n.pinned = folder || *parent; return n;
}
const SidebarMenuItem* Find(const std::vector<SidebarMenuItem>& items, const std::string& action, const std::string& target = "") {
  for (const auto& item : items) {
    if (item.action == action && item.target == target) return &item;
    if (auto found = Find(item.children, action, target)) return found;
  }
  return nullptr;
}
int main() {
  int failures = 0;
  auto check = [&](bool pass, const char* label) { if (!pass) { ++failures; std::fprintf(stderr, "%s\n", label); } };
  auto today = Node("today"); today.live = true;
  auto folder = Node("folder", "", true); auto nested = Node("nested", "folder", true);
  auto saved = Node("saved", "nested"); saved.live = true;
  auto empty = Node("empty", "", true);
  std::vector<MenuNode> nodes{today, folder, nested, saved, empty};
  auto menu = red::SidebarMenuItems(nodes, "today");
  // Today (Arc layout): copy, rename, mute, split, duplicate, move, archive group.
  check(Find(menu, "copyLink") && Find(menu, "rename") && Find(menu, "split") && Find(menu, "duplicate") && Find(menu, "detach"), "Today tab actions");
  check(Find(menu, "close") && Find(menu, "archiveOthers") && Find(menu, "archiveBelow"), "Today archive group");
  check(!Find(menu, "archiveOthers")->enabled && !Find(menu, "archiveBelow")->enabled, "single Today tab: no others, none below");
  check(!Find(menu, "setHome") && !Find(menu, "editPinned") && !Find(menu, "today"), "Today has no pinned-page actions");
  check(!Find(menu, "automaticTitle"), "automatic tab needs no reset-name action");
  check(Find(menu, "mute") && Find(menu, "mute")->enabled && !Find(menu, "unmute"), "live tab: mute");
  nodes[0].muted = true;
  check(Find(red::SidebarMenuItems(nodes, "today"), "unmute") && !Find(red::SidebarMenuItems(nodes, "today"), "mute"), "muted tab offers unmute");
  nodes[0].muted = false;
  check(!Find(red::SidebarMenuItems(nodes, "folder"), "duplicate") && !Find(red::SidebarMenuItems(nodes, "folder"), "mute"), "folders have no tab actions");
  nodes[0].custom_title = true;
  check(Find(red::SidebarMenuItems(nodes, "today"), "automaticTitle") != nullptr, "manual name can return to the website title");
  // Pinned: edit pinned page (replace with current only when it differs), remove pin.
  nodes[3].url = "https://a.test/"; nodes[3].current_url = "https://a.test/";
  menu = red::SidebarMenuItems(nodes, "saved");
  check(Find(menu, "setHome") && !Find(menu, "setHome")->enabled && Find(menu, "editPinned"), "pinned page at its saved address");
  check(Find(menu, "today") && !Find(menu, "archiveOthers"), "pinned: remove pin, no archive group");
  check(Find(menu, "close") && Find(menu, "close")->label == "Unload Tab" && Find(menu, "close")->enabled == nodes[3].live, "pinned: unload only while loaded");
  nodes[3].current_url = "https://a.test/inbox";
  check(Find(red::SidebarMenuItems(nodes, "saved"), "setHome")->enabled, "pinned page away from its saved address can replace it");
  auto favorite = Node("fav"); favorite.favorite = true; favorite.live = true;
  auto with_favorite = nodes; with_favorite.push_back(favorite);
  menu = red::SidebarMenuItems(with_favorite, "fav");
  check(Find(menu, "today") && Find(menu, "editPinned") && !Find(menu, "space", "home"), "favorite: remove favorite, edit, no space moves");
  check(Find(menu, "close") && Find(menu, "close")->label == "Unload Tab" && Find(menu, "close")->enabled, "loaded favorite can unload");
  with_favorite.back().live = false;
  check(!Find(red::SidebarMenuItems(with_favorite, "fav"), "close")->enabled, "unloaded favorite: unload is disabled");
  check(!Find(red::SidebarMenuItems(with_favorite, "fav"), "hideXBadge"), "Only X pins have a notification toggle");
  with_favorite.back().url = "https://x.com/home";
  check(Find(red::SidebarMenuItems(with_favorite, "fav"), "hideXBadge"), "X Favorite can hide its badge while unloaded");
  with_favorite.back().x_badge_hidden = true;
  check(Find(red::SidebarMenuItems(with_favorite, "fav"), "showXBadge"), "Muted X badge can be shown again");
  menu = red::SidebarMenuItems(nodes, "folder");
  check(Find(menu, "newFolder") && Find(menu, "expand") && !Find(menu, "close"), "folder actions");
  check(!Find(menu, "folder", "folder") && !Find(menu, "folder", "nested"), "cannot nest folder inside self or descendant");
  check(Find(menu, "removeFolder") && Find(menu, "removeFolder")->enabled && Find(menu,"removeFolder")->label=="Delete folder…", "nonempty folder offers confirmed deletion");
  check(Find(red::SidebarMenuItems(nodes, "empty"), "removeFolder")->enabled, "empty folder removal enabled");
  nodes[3].live = false;
  check(Find(red::SidebarMenuItems(nodes, "saved"), "duplicate") && !Find(red::SidebarMenuItems(nodes, "saved"), "mute")->enabled, "unloaded tab: duplicate, mute unavailable");
  check(!Find(red::SidebarMenuItems(nodes, "saved"), "setHome")->enabled, "unloaded pinned page cannot replace its address");
  nodes[0].live = false;
  check(Find(red::SidebarMenuItems(nodes, "today"), "close") != nullptr, "unloaded Today can still be archived");
  nodes[0].archived = true;
  check(Find(red::SidebarMenuItems(nodes, "today"), "restore") && !Find(red::SidebarMenuItems(nodes, "today"), "close"), "archived entry offers restoration only");
  nodes[3].closing = true;
  check(red::SidebarMenuItems(nodes, "saved").empty(), "closing entry rejected");
  check(red::SidebarMenuItems(nodes, "missing").empty(), "unknown entry rejected");
  check(Find(red::SidebarMenuItems(nodes, ""), "newTab") && Find(red::SidebarMenuItems(nodes, ""), "newFolder"), "blank sidebar creation menu");
  const auto background=red::SidebarMenuItems(nodes, "");const auto edit_theme=Find(background,"editTheme");
  check(edit_theme && edit_theme->label=="Edit theme" && edit_theme->enabled,"blank sidebar offers theme editor");
  check(!Find(red::SidebarMenuItems(nodes,"folder"),"editTheme") && !Find(red::SidebarMenuItems({Node("tab")},"tab"),"editTheme"),"theme editor belongs only to the sidebar background");
  check(!Find(red::SidebarMenuItems(nodes,"",{},"",true),"editTheme"),"standalone windows have no space theme menu");
  {
    // Spaces: move a top-level entry to another space; folders and order stay per space.
    const std::vector<red::MenuSpace> spaces{{"home", "Home"}, {"work", "Work"}};
    auto a = Node("a"); a.space = "home"; auto b = Node("b"); b.space = "work";
    auto home_folder = Node("home-folder", "", true); home_folder.space = "home";
    auto work_folder = Node("work-folder", "", true); work_folder.space = "work"; work_folder.position = 1;
    auto inner = Node("inner", "home-folder");
    std::vector<MenuNode> spaced{a, b, home_folder, work_folder, inner};
    auto items = red::SidebarMenuItems(spaced, "a", spaces, "home");
    check(Find(items, "space", "work") && Find(items, "", "home") && Find(items, "", "home")->checked, "move to: other space, current space checked");
    check(Find(items, "newSpaceWith") && Find(items, "detach"), "move to: new space and new window");
    check(!Find(red::SidebarMenuItems(spaced, "inner", spaces, "home"), "space", "work"), "nested entries move with their folder");
    check(Find(red::SidebarMenuItems(spaced, "home-folder", spaces, "home"), "space", "work") != nullptr, "top-level folder moves to another space");
    auto c = Node("c"); c.space = "home"; c.position = 2; auto d = Node("d"); d.space = "home"; d.position = 3;
    std::vector<MenuNode> three{a, c, d, b};
    auto below = red::SidebarMenuItems(three, "c", spaces, "home");
    check(Find(below, "archiveOthers")->enabled && Find(below, "archiveBelow")->enabled, "Today: others and below in this space");
    check(!Find(red::SidebarMenuItems(three, "d", spaces, "home"), "archiveBelow")->enabled, "last Today tab: nothing below");
    check(Find(red::SidebarMenuItems(spaced, "", spaces, "home"), "newSpace") != nullptr, "sidebar background offers a new space");
  }
  {
    auto items=red::SpaceMenuItems({{"home","Home"},{"work","Work"}},{{"default","Default"},{"office","Office"}},"home","office");
    check(Find(items,"setProfile","office") && Find(items,"setProfile","office")->checked && !Find(items,"setProfile","default")->checked,
      "Space menu checks its assigned profile");
    check(Find(items,"editTheme") && Find(items,"newProfile") && Find(items,"newFolder") && Find(items,"exportSpace","work") && Find(items,"deleteSpace"),
      "Space Edit offers theme, profiles, folder, export and deletion");
    check(!Find(items,"rename") && !Find(items,"editIcon") && !Find(items,"exportSpace","home"),"Space menu excludes duplicated editor actions and self-export");
    items=red::SpaceMenuItems({{"home","Home"}},{{"default","Default"}},"home","default");
    check(!Find(items,"deleteSpace")->enabled,"last Space cannot be deleted from Edit");
  }
  return failures ? 1 : 0;
}
