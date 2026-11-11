#pragma once
#include <algorithm>
#include <string_view>
#include "storage/tree_store.h"
namespace red {
// The outermost collapsed folder is the one that shows this page as a peek.
inline std::string_view CollapsedFolderContaining(const std::vector<TreeNode>& nodes, std::string_view id) {
  std::string_view folder;
  for (int depth = 0; depth <= 64 && !id.empty(); ++depth) {
    const auto node = std::find_if(nodes.begin(), nodes.end(), [&](const auto& n) { return n.id == id; });
    if (node == nodes.end()) break;
    if (node->folder && !node->expanded) folder = node->id;
    id = node->parent;
  }
  return folder;
}

// Pages in sidebar order: Favorites, then |space|'s Pinned and Today. With
// |skip_collapsed|, pages inside collapsed folders (hidden in the sidebar) are
// left out.
inline std::vector<std::string> OrderedTabs(const std::vector<TreeNode>& nodes, const std::string& space,
                                            std::string_view excluded_folder = {}, std::string_view profile = "default",
                                            bool skip_collapsed = false) {
  std::vector<std::string> result;
  auto section = [](const TreeNode& n) { return n.favorite ? 0 : n.pinned ? 1 : 2; };
  auto visit = [&](auto&& self, const std::string& parent, int depth) -> void {
    if (depth > 64) return;
    std::vector<const TreeNode*> children;
    for (const auto& node : nodes)
      if (!node.archived && node.parent == parent && (!parent.empty() || (node.favorite ? node.profile == profile : node.space == space))) children.push_back(&node);
    std::stable_sort(children.begin(), children.end(), [&](auto a, auto b) {
      return section(*a) != section(*b) ? section(*a) < section(*b) : a->position < b->position;
    });
    for (const auto* node : children) {
      if (!node->folder) result.push_back(node->id);
      else if (node->id != excluded_folder && (!skip_collapsed || node->expanded)) self(self, node->id, depth + 1);
    }
  };
  visit(visit, "", 0); return result;
}
// Today tabs that "Close Other Tabs" (|below| false), "Close Tabs Below"
// (|below| true) or Clear (|all|) archive, in sidebar order, for the Today tab
// |id|. Clear keeps the page on screen: |active| and, in Split View, the other
// pane of its split. Empty when |id| is not a Today tab.
inline std::vector<std::string> TodayArchiveTargets(const TreeSnapshot& tree, const std::string& id, bool below, bool all,
                                                    const std::string& active) {
  const auto node = std::find_if(tree.nodes.begin(), tree.nodes.end(), [&](const auto& n) { return n.id == id; });
  if (node == tree.nodes.end() || node->folder || node->archived || node->pinned || node->favorite || !node->parent.empty()) return {};
  std::vector<const TreeNode*> today;
  for (const auto& other : tree.nodes)
    if (!other.folder && !other.archived && !other.pinned && !other.favorite && other.parent.empty() && other.space == node->space) today.push_back(&other);
  std::sort(today.begin(), today.end(), [](const auto* a, const auto* b) { return a->position < b->position; });
  std::vector<std::string> keep;
  if (all && !active.empty()) {
    keep.push_back(active);
    for (const auto& split : tree.splits)
      if (split.anchor == active || split.other == active) { keep.push_back(split.anchor); keep.push_back(split.other); }
  }
  std::vector<std::string> ids;
  for (const auto* other : today)
    if (all ? std::find(keep.begin(), keep.end(), other->id) == keep.end()
            : (other->id != id && (!below || other->position > node->position))) ids.push_back(other->id);
  return ids;
}
inline std::vector<std::string> OrderedTabsForSpace(const TreeSnapshot& tree, const std::string& space, std::string_view excluded_folder = {},
                                                    bool skip_collapsed = false) {
  const auto entry = std::find_if(tree.spaces.begin(), tree.spaces.end(), [&](const auto& item) { return item.id == space; });
  return OrderedTabs(tree.nodes, space, excluded_folder, entry == tree.spaces.end() ? "default" : entry->profile, skip_collapsed);
}
}  // namespace red
