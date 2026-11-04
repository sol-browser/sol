#include "navigation/shortcuts.h"
#include "workspace/tab_order.h"
#include "navigation/file_url.h"
#include <cstdio>
int main() {
  int failures = 0;
  auto check = [&](bool value, const char* name) { if (!value) { std::fprintf(stderr, "%s\n", name); ++failures; } };
  check(red::FileUrlFromGenericPath(u8"C:/Users/me/a b#1.pdf") == "file:///C:/Users/me/a%20b%231.pdf", "dropped file path to URL");
  check(red::FileUrlFromGenericPath(u8"D:/книга.pdf") == "file:///D:/%D0%BA%D0%BD%D0%B8%D0%B3%D0%B0.pdf", "UTF-8 file names");
  check(red::FileUrlFromGenericPath(u8"//server/share/a.pdf").empty() && red::FileUrlFromGenericPath(u8"a.pdf").empty(),
        "UNC and relative paths have no local file URL");
  using red::Shortcut;
  check(red::ShortcutKey('L', true, false, false) == Shortcut::Address, "Ctrl+L");
  check(red::ShortcutKey(9, true, true, false) == Shortcut::PreviousTab, "reverse tab cycle");
  check(red::ShortcutKey(37, false, false, true) == Shortcut::Back, "history navigation");
  check(red::ShortcutKey(115, false, false, true) == Shortcut::CloseWindow, "Alt+F4 routes to root coordinator instead of closing one Chrome child");
  check(red::ShortcutKey('R', true, true, false) == Shortcut::ReloadBypass, "cache-bypassing reload");
  check(red::ShortcutKey(123, false, false, false) == Shortcut::DevTools, "F12 opens page tools");
  check(red::ShortcutKey('I', true, true, false) == Shortcut::DevTools, "Ctrl+Shift+I opens page tools");
  check(!red::ShortcutKey(123, false, true, false) && !red::ShortcutKey('I', true, true, true), "DevTools modifiers stay exact");
  check(red::ShortcutNamed("devTools") == Shortcut::DevTools, "typed DevTools command");
  check(!red::ShortcutKey('T', true, false, true), "AltGr is not a browser shortcut");
  check(!red::ShortcutKey('W', true, true, false), "Ctrl+Shift+W is not accidentally close-tab");
  check(!red::ShortcutNamed("eval"), "no arbitrary native command capability");
  std::vector<red::TreeNode> nodes;
  auto node = [&](const char* id, const char* parent, int position, bool folder, bool pinned, bool favorite, bool archived) {
    red::TreeNode n; n.id = id; n.parent = parent; n.position = position; n.folder = folder;
    n.pinned = pinned; n.favorite = favorite; n.archived = archived; n.expanded = false; nodes.push_back(n);
  };
  node("today", "", 0, false, false, false, false);
  node("folder", "", 3, true, true, false, false);
  node("nested", "folder", 0, true, true, false, false);
  node("nested-tab", "nested", 0, false, true, false, false);
  node("favorite", "", 6, false, true, true, false);
  node("archived", "", 1, false, false, false, true);
  for (auto& n : nodes) if (n.parent.empty() && !n.favorite) n.space = "work";
  node("other-space", "", 2, false, false, false, false); nodes.back().space = "home";
  check(red::OrderedTabs(nodes, "work") == std::vector<std::string>{"favorite", "nested-tab", "today"},
        "tab cycle follows sidebar sections and collapsed descendants, excludes archive and other spaces");
  check(red::OrderedTabs(nodes, "home") == std::vector<std::string>{"favorite", "other-space"}, "favorites are shared by every space");
  check(red::OrderedTabs({}, "work").empty(), "empty workspace");
  return failures ? 1 : 0;
}
