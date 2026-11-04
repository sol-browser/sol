#pragma once
#include <algorithm>
#include "storage/tree_store.h"
namespace red {
// Pages in sidebar order: Favorites, then |space|'s Pinned and Today.
inline std::vector<std::string> OrderedTabs(const std::vector<TreeNode>& nodes, const std::string& space) {
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
      if (node->folder) self(self, node->id, depth + 1); else result.push_back(node->id);
    }
  };
  visit(visit, "", 0); return result;
}
}  // namespace red
