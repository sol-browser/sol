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

// Pages in sidebar order: Favorites, then |space|'s Pinned and Today.
inline std::vector<std::string> OrderedTabs(const std::vector<TreeNode>& nodes, const std::string& space,
                                            std::string_view excluded_folder = {}) {
  std::vector<std::string> result;
  auto section = [](const TreeNode& n) { return n.favorite ? 0 : n.pinned ? 1 : 2; };
  auto visit = [&](auto&& self, const std::string& parent, int depth) -> void {
    if (depth > 64) return;
    std::vector<const TreeNode*> children;
    for (const auto& node : nodes)
      if (!node.archived && node.parent == parent && (!parent.empty() || node.favorite || node.space == space)) children.push_back(&node);
    std::stable_sort(children.begin(), children.end(), [&](auto a, auto b) {
      return section(*a) != section(*b) ? section(*a) < section(*b) : a->position < b->position;
    });
    for (const auto* node : children) {
      if (!node->folder) result.push_back(node->id);
      else if (node->id != excluded_folder) self(self, node->id, depth + 1);
    }
  };
  visit(visit, "", 0); return result;
}
}  // namespace red
