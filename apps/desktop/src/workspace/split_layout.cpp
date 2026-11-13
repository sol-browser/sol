#include "workspace/split_layout.h"
#include <algorithm>
#include <climits>
#include <cstdlib>

namespace red {
namespace {
bool Container(const SplitNode& node) { return node.axis == 'h' || node.axis == 'v'; }

bool ValidNode(const SplitNode& node, char parent_axis, int panes, std::vector<int>& seen, int depth) {
  if (depth > kMaxSplitPanes || node.weight < kMinSplitWeight || node.weight > 10000) return false;
  if (!Container(node)) {
    if (node.axis != 0 || !node.children.empty() || node.slot < 0 || node.slot >= panes) return false;
    seen.push_back(node.slot); return true;
  }
  if (node.slot != -1 || node.axis == parent_axis || node.children.size() < 2 ||
      node.children.size() > static_cast<size_t>(kMaxSplitPanes)) return false;
  int sum = 0;
  for (const auto& child : node.children) {
    if (!ValidNode(child, node.axis, panes, seen, depth + 1)) return false;
    sum += child.weight;
  }
  return sum == 10000;
}

class Parser {
 public:
  explicit Parser(std::string_view text) : text_(text) {}
  bool Done() const { return at_ == text_.size(); }
  bool Node(SplitNode& node, int depth) {
    if (depth > kMaxSplitPanes || at_ >= text_.size()) return false;
    const char c = text_[at_];
    if (c >= '0' && c <= '9') { node.slot = c - '0'; ++at_; return true; }
    if (c != 'h' && c != 'v') return false;
    node.axis = c; ++at_;
    if (!Take('(')) return false;
    for (;;) {
      SplitNode child;
      if (!Number(child.weight) || !Take('*') || !Node(child, depth + 1)) return false;
      node.children.push_back(std::move(child));
      if (node.children.size() > static_cast<size_t>(kMaxSplitPanes)) return false;
      if (Take(',')) continue;
      return Take(')');
    }
  }

 private:
  bool Take(char c) { if (at_ < text_.size() && text_[at_] == c) { ++at_; return true; } return false; }
  bool Number(int& out) {
    const size_t start = at_; int value = 0;
    while (at_ < text_.size() && text_[at_] >= '0' && text_[at_] <= '9') {
      value = value * 10 + (text_[at_] - '0'); ++at_;
      if (at_ - start > 5) return false;
    }
    if (at_ == start || (text_[start] == '0' && at_ - start > 1)) return false;
    out = value; return true;
  }
  std::string_view text_;
  size_t at_ = 0;
};

void Append(const SplitNode& node, std::string& out) {
  if (!Container(node)) { out += static_cast<char>('0' + node.slot); return; }
  out += node.axis; out += '(';
  for (size_t i = 0; i < node.children.size(); ++i) {
    if (i) out += ',';
    out += std::to_string(node.children[i].weight); out += '*';
    Append(node.children[i], out);
  }
  out += ')';
}

void Rebalance(std::vector<SplitNode>& children) {
  if (children.empty()) return;  // Nothing to share out (and no division by zero).
  long long total = 0;
  for (const auto& child : children) total += std::max(1, child.weight);
  int sum = 0;
  for (auto& child : children) { child.weight = static_cast<int>(10000LL * std::max(1, child.weight) / total); sum += child.weight; }
  children.back().weight += 10000 - sum;
  for (auto& child : children) {
    while (child.weight < kMinSplitWeight) {
      auto largest = std::max_element(children.begin(), children.end(), [](const auto& a, const auto& b) { return a.weight < b.weight; });
      const int take = std::min(kMinSplitWeight - child.weight, largest->weight - kMinSplitWeight);
      if (take <= 0) break;
      largest->weight -= take; child.weight += take;
    }
  }
}

SplitNode NormalizeNode(SplitNode node) {
  if (!Container(node)) { node.children.clear(); return node; }
  std::vector<SplitNode> flat;
  for (auto& item : node.children) {
    auto child = NormalizeNode(std::move(item));
    if (Container(child) && child.axis == node.axis) {
      for (auto& grandchild : child.children) {
        grandchild.weight = static_cast<int>(1LL * grandchild.weight * child.weight / 10000);
        flat.push_back(std::move(grandchild));
      }
    } else flat.push_back(std::move(child));
  }
  node.children = std::move(flat);
  if (node.children.size() == 1) {
    const int weight = node.weight;
    SplitNode only = std::move(node.children.front());
    only.weight = weight; return only;
  }
  if (!node.children.empty()) Rebalance(node.children);
  return node;
}

std::optional<SplitNode> Finish(SplitNode root, int panes) {
  auto normalized = NormalizeSplitLayout(std::move(root));
  if (!ValidSplitLayout(normalized, panes)) return std::nullopt;
  return normalized;
}

SplitNode Leaf(int slot, int weight = 10000) { SplitNode leaf; leaf.slot = slot; leaf.weight = weight; return leaf; }

SplitNode Line(char axis, int first, int count) {
  SplitNode line; line.axis = axis;
  for (int i = 0; i < count; ++i) line.children.push_back(Leaf(first + i, 10000 / count));
  line.children.back().weight += 10000 - (10000 / count) * count;
  return line;
}

bool InsertAt(SplitNode& node, int target, char axis, bool before, SplitNode leaf) {
  for (size_t i = 0; i < node.children.size(); ++i) {
    auto& child = node.children[i];
    if (!Container(child) && child.slot == target) {
      if (node.axis == axis) {
        const int half = child.weight / 2;
        child.weight -= half; leaf.weight = half;
        node.children.insert(node.children.begin() + static_cast<long long>(i) + (before ? 0 : 1), std::move(leaf));
      } else {
        SplitNode wrap; wrap.axis = axis; wrap.weight = child.weight;
        SplitNode pane = child; pane.weight = 5000; leaf.weight = 5000;
        if (before) { wrap.children.push_back(std::move(leaf)); wrap.children.push_back(std::move(pane)); }
        else { wrap.children.push_back(std::move(pane)); wrap.children.push_back(std::move(leaf)); }
        child = std::move(wrap);
      }
      return true;
    }
    if (Container(child) && InsertAt(child, target, axis, before, leaf)) return true;
  }
  return false;
}

bool RemoveAt(SplitNode& node, int slot) {
  for (size_t i = 0; i < node.children.size(); ++i) {
    auto& child = node.children[i];
    if (!Container(child) && child.slot == slot) {
      const int weight = child.weight;
      node.children.erase(node.children.begin() + static_cast<long long>(i));
      if (!node.children.empty()) node.children[i > 0 ? i - 1 : 0].weight += weight;
      return true;
    }
    if (Container(child) && RemoveAt(child, slot)) return true;
  }
  return false;
}

void Renumber(SplitNode& node, int removed) {
  if (!Container(node)) { if (node.slot > removed) --node.slot; return; }
  for (auto& child : node.children) Renumber(child, removed);
}

void Swap(SplitNode& node, int a, int b) {
  if (!Container(node)) { if (node.slot == a) node.slot = b; else if (node.slot == b) node.slot = a; return; }
  for (auto& child : node.children) Swap(child, a, b);
}

void Equalize(SplitNode& container) {
  const int count = static_cast<int>(container.children.size());
  for (auto& child : container.children) child.weight = 10000 / count;
  container.children.back().weight += 10000 - (10000 / count) * count;
}

int MinExtent(const SplitNode& node, bool horizontal, const SplitMetrics& metrics) {
  if (!Container(node)) return horizontal ? metrics.min_width : metrics.min_height;
  const bool along = (node.axis == 'h') == horizontal;
  int total = 0;
  for (const auto& child : node.children) {
    const int extent = MinExtent(child, horizontal, metrics);
    total = along ? total + extent : std::max(total, extent);
  }
  if (along) total += metrics.gap * (static_cast<int>(node.children.size()) - 1);
  return total;
}

std::vector<int> Allocate(int total, const std::vector<int>& minimums, const std::vector<int>& weights) {
  const size_t count = minimums.size();
  std::vector<int> sizes(count, 0);
  total = std::max(0, total);
  long long minimum_sum = 0;
  for (const int value : minimums) minimum_sum += value;
  if (minimum_sum >= total) {
    int used = 0;
    for (size_t i = 0; i < count; ++i) {
      sizes[i] = minimum_sum ? static_cast<int>(1LL * total * minimums[i] / minimum_sum) : total / static_cast<int>(count);
      used += sizes[i];
    }
    sizes.back() += total - used;
    return sizes;
  }
  std::vector<bool> fixed(count, false);
  long long remaining = 0, weight_sum = 0;
  for (bool again = true; again;) {
    again = false; remaining = total; weight_sum = 0;
    for (size_t i = 0; i < count; ++i) { if (fixed[i]) remaining -= minimums[i]; else weight_sum += weights[i]; }
    for (size_t i = 0; i < count; ++i)
      if (!fixed[i] && (weight_sum ? remaining * weights[i] / weight_sum : 0) < minimums[i]) { fixed[i] = true; again = true; }
  }
  int used = 0; size_t last_free = count;
  for (size_t i = 0; i < count; ++i) {
    sizes[i] = fixed[i] ? minimums[i] : static_cast<int>(weight_sum ? remaining * weights[i] / weight_sum : 0);
    if (!fixed[i]) last_free = i;
    used += sizes[i];
  }
  sizes[last_free == count ? count - 1 : last_free] += total - used;
  return sizes;
}

std::vector<SplitRect> ChildRects(const SplitNode& node, SplitRect rect, const SplitMetrics& metrics) {
  const bool horizontal = node.axis == 'h';
  const int count = static_cast<int>(node.children.size());
  const int along = horizontal ? rect.width : rect.height;
  std::vector<int> minimums, weights;
  for (const auto& child : node.children) { minimums.push_back(MinExtent(child, horizontal, metrics)); weights.push_back(child.weight); }
  const auto sizes = Allocate(along - metrics.gap * (count - 1), minimums, weights);
  std::vector<SplitRect> rects;
  int position = horizontal ? rect.x : rect.y;
  for (int i = 0; i < count; ++i) {
    rects.push_back(horizontal ? SplitRect{position, rect.y, sizes[i], rect.height} : SplitRect{rect.x, position, rect.width, sizes[i]});
    position += sizes[i] + metrics.gap;
  }
  return rects;
}

void Place(const SplitNode& node, SplitRect rect, const SplitMetrics& metrics, std::vector<int>& path, SplitGeometry& geometry) {
  if (!Container(node)) {
    if (node.slot >= 0 && node.slot < static_cast<int>(geometry.panes.size())) geometry.panes[node.slot] = rect;
    return;
  }
  const auto rects = ChildRects(node, rect, metrics);
  for (size_t i = 0; i < rects.size(); ++i) {
    path.push_back(static_cast<int>(i));
    Place(node.children[i], rects[i], metrics, path, geometry);
    path.pop_back();
    if (i + 1 < rects.size()) {
      const auto& a = rects[i];
      const SplitRect gap = node.axis == 'h' ? SplitRect{a.x + a.width, rect.y, metrics.gap, rect.height}
                                             : SplitRect{rect.x, a.y + a.height, rect.width, metrics.gap};
      geometry.dividers.push_back({path, static_cast<int>(i), node.axis, false, gap});
    }
  }
}

// The rect of the container at |path|, or nullopt.
std::optional<SplitRect> ContainerRect(const SplitNode& root, SplitRect area, const SplitMetrics& metrics, const std::vector<int>& path) {
  const SplitNode* node = &root; SplitRect rect = area;
  for (const int index : path) {
    if (!Container(*node) || index < 0 || index >= static_cast<int>(node->children.size())) return std::nullopt;
    rect = ChildRects(*node, rect, metrics)[index]; node = &node->children[index];
  }
  if (!Container(*node)) return std::nullopt;
  return rect;
}

bool DragPair(SplitNode& container, SplitRect rect, const SplitMetrics& metrics, int index, int position) {
  if (!Container(container) || index < 0 || index + 1 >= static_cast<int>(container.children.size())) return false;
  const bool horizontal = container.axis == 'h';
  const auto rects = ChildRects(container, rect, metrics);
  const auto& a = rects[index]; const auto& b = rects[index + 1];
  const int start = horizontal ? a.x : a.y;
  const int end = horizontal ? b.x + b.width : b.y + b.height;
  const int pair = end - start - metrics.gap;
  if (pair <= 0) return false;
  const int minimum_a = MinExtent(container.children[index], horizontal, metrics);
  const int minimum_b = MinExtent(container.children[index + 1], horizontal, metrics);
  int size = position - metrics.gap / 2 - start;
  if (minimum_a + minimum_b <= pair) size = std::clamp(size, minimum_a, pair - minimum_b);
  else size = horizontal ? a.width : a.height;  // Too cramped to move.
  auto& first = container.children[index]; auto& second = container.children[index + 1];
  const int sum = first.weight + second.weight;
  const int weight = std::clamp(static_cast<int>((2LL * sum * size + pair) / (2LL * pair)), kMinSplitWeight, sum - kMinSplitWeight);
  first.weight = weight; second.weight = sum - weight;
  return true;
}
}  // namespace

bool ValidSplitLayout(const SplitNode& root, int panes) {
  if (panes < 2 || panes > kMaxSplitPanes || !Container(root) || root.weight != 10000) return false;
  std::vector<int> seen;
  if (!ValidNode(root, 0, panes, seen, 0) || static_cast<int>(seen.size()) != panes) return false;
  std::sort(seen.begin(), seen.end());
  for (int i = 0; i < panes; ++i) if (seen[i] != i) return false;
  return true;
}

std::optional<SplitNode> ParseSplitLayout(std::string_view text, int panes) {
  if (text.empty() || text.size() > 128) return std::nullopt;
  Parser parser(text); SplitNode root;
  if (!parser.Node(root, 0) || !parser.Done() || !ValidSplitLayout(root, panes) || FormatSplitLayout(root) != text) return std::nullopt;
  return root;
}

std::string FormatSplitLayout(const SplitNode& root) { std::string out; Append(root, out); return out; }

int SplitPaneCount(const SplitNode& node) {
  if (!Container(node)) return node.slot >= 0 ? 1 : 0;
  int count = 0;
  for (const auto& child : node.children) count += SplitPaneCount(child);
  return count;
}

std::vector<int> SplitReadingOrder(const SplitNode& root) {
  std::vector<int> order;
  auto walk = [&order](const auto& self, const SplitNode& node) -> void {
    if (!Container(node)) { if (node.slot >= 0) order.push_back(node.slot); return; }
    for (const auto& child : node.children) self(self, child);
  };
  walk(walk, root);
  return order;
}

SplitNode NormalizeSplitLayout(SplitNode root) {
  auto normalized = NormalizeNode(std::move(root));
  normalized.weight = 10000;
  return normalized;
}

SplitNode TwoPaneSplitLayout(bool vertical, int ratio, bool reversed) {
  SplitNode root; root.axis = vertical ? 'v' : 'h';
  ratio = std::clamp(ratio, kMinSplitWeight, 10000 - kMinSplitWeight);
  root.children = {Leaf(reversed ? 1 : 0, ratio), Leaf(reversed ? 0 : 1, 10000 - ratio)};
  return root;
}

bool LinkableSplitGrid(const SplitNode& root) {
  if (!Container(root) || root.children.size() != 2) return false;
  for (const auto& row : root.children)
    if (!Container(row) || row.children.size() != 2 || Container(row.children[0]) || Container(row.children[1])) return false;
  return true;
}

bool ParseSplitEdge(std::string_view text, SplitEdge& edge) {
  if (text == "left") edge = SplitEdge::Left;
  else if (text == "right") edge = SplitEdge::Right;
  else if (text == "top") edge = SplitEdge::Top;
  else if (text == "bottom") edge = SplitEdge::Bottom;
  else return false;
  return true;
}

std::optional<SplitNode> InsertSplitPane(const SplitNode& root, int target, SplitEdge edge, int new_slot) {
  const int panes = SplitPaneCount(root);
  if (!ValidSplitLayout(root, panes) || panes >= kMaxSplitPanes || new_slot != panes) return std::nullopt;
  const char axis = edge == SplitEdge::Left || edge == SplitEdge::Right ? 'h' : 'v';
  const bool before = edge == SplitEdge::Left || edge == SplitEdge::Top;
  SplitNode result = root;
  if (target == -1) {
    if (result.axis == axis) {
      const int count = static_cast<int>(result.children.size());
      for (auto& child : result.children) child.weight = child.weight * count / (count + 1);
      auto leaf = Leaf(new_slot, 10000 / (count + 1));
      result.children.insert(before ? result.children.begin() : result.children.end(), std::move(leaf));
    } else {
      SplitNode old = std::move(result); old.weight = 5000;
      result = SplitNode{}; result.axis = axis;
      if (before) { result.children.push_back(Leaf(new_slot, 5000)); result.children.push_back(std::move(old)); }
      else { result.children.push_back(std::move(old)); result.children.push_back(Leaf(new_slot, 5000)); }
    }
    return Finish(std::move(result), panes + 1);
  }
  if (target < 0 || target >= panes || !InsertAt(result, target, axis, before, Leaf(new_slot))) return std::nullopt;
  return Finish(std::move(result), panes + 1);
}

std::optional<SplitNode> RemoveSplitPane(const SplitNode& root, int slot) {
  const int panes = SplitPaneCount(root);
  if (!ValidSplitLayout(root, panes) || panes <= 2 || slot < 0 || slot >= panes) return std::nullopt;
  SplitNode result = root;
  if (!RemoveAt(result, slot)) return std::nullopt;
  Renumber(result, slot);
  return Finish(std::move(result), panes - 1);
}

SplitNode SwapSplitSlots(SplitNode root, int a, int b) { Swap(root, a, b); return root; }

std::optional<SplitNode> MoveSplitPane(const SplitNode& root, int slot, int target, SplitEdge edge) {
  const int panes = SplitPaneCount(root);
  if (!ValidSplitLayout(root, panes) || slot < 0 || slot >= panes || target < -1 || target >= panes || target == slot) return std::nullopt;
  const char axis = edge == SplitEdge::Left || edge == SplitEdge::Right ? 'h' : 'v';
  const bool before = edge == SplitEdge::Left || edge == SplitEdge::Top;
  std::optional<SplitNode> result;
  if (panes == 2) {
    // Two panes: only the direction and the order can change.
    SplitNode pair; pair.axis = axis;
    pair.children = before ? std::vector<SplitNode>{Leaf(slot, 5000), Leaf(1 - slot, 5000)}
                           : std::vector<SplitNode>{Leaf(1 - slot, 5000), Leaf(slot, 5000)};
    result = pair;
  } else {
    // Take the pane out, put it back at its new place (it takes the last slot),
    // then give every pane its original slot again.
    const auto removed = RemoveSplitPane(root, slot);
    if (!removed) return std::nullopt;
    const auto inserted = InsertSplitPane(*removed, target == -1 ? -1 : target > slot ? target - 1 : target, edge, panes - 1);
    if (!inserted) return std::nullopt;
    std::vector<int> slots(panes);
    for (int i = 0; i < panes; ++i) slots[i] = i == panes - 1 ? slot : i < slot ? i : i + 1;
    result = RelabelSplitSlots(*inserted, slots);
  }
  if (!ValidSplitLayout(*result, panes) || *result == root) return std::nullopt;
  return result;
}

SplitNode RelabelSplitSlots(SplitNode root, const std::vector<int>& slots) {
  auto walk = [&slots](const auto& self, SplitNode& node) -> void {
    if (!Container(node)) { if (node.slot >= 0 && node.slot < static_cast<int>(slots.size())) node.slot = slots[node.slot]; return; }
    for (auto& child : node.children) self(self, child);
  };
  walk(walk, root);
  return root;
}

std::optional<SplitNode> NudgeSplitDivider(const SplitNode& root, const std::vector<int>& path, int index, int delta, bool linked) {
  SplitNode result = root;
  SplitNode* node = &result;
  for (const int i : path) {
    if (i < 0 || i >= static_cast<int>(node->children.size())) return std::nullopt;
    node = &node->children[i];
  }
  if (!Container(*node) || index < 0 || index + 1 >= static_cast<int>(node->children.size())) return std::nullopt;
  auto& first = node->children[index]; auto& second = node->children[index + 1];
  const int sum = first.weight + second.weight;
  first.weight = std::clamp(first.weight + delta, kMinSplitWeight, sum - kMinSplitWeight); second.weight = sum - first.weight;
  if (linked && LinkableSplitGrid(result) && path.size() == 1) {
    const auto& source = result.children[path[0]]; auto& target = result.children[1 - path[0]];
    target.children[0].weight = source.children[0].weight; target.children[1].weight = source.children[1].weight;
  }
  return result;
}

std::optional<SplitNode> SplitPreset(std::string_view preset, int panes) {
  if (panes < 2 || panes > kMaxSplitPanes) return std::nullopt;
  if (preset == "columns") return Line('h', 0, panes);
  if (preset == "rows") return Line('v', 0, panes);
  if (preset == "main-left" || preset == "main-top") {
    const bool left = preset == "main-left";
    if (panes == 2) return Line(left ? 'h' : 'v', 0, 2);
    SplitNode root; root.axis = left ? 'h' : 'v';
    auto rest = Line(left ? 'v' : 'h', 1, panes - 1); rest.weight = 5000;
    root.children.push_back(Leaf(0, 5000)); root.children.push_back(std::move(rest));
    return root;
  }
  if (preset == "grid" && panes == 4) {
    SplitNode root; root.axis = 'v';
    auto top = Line('h', 0, 2), bottom = Line('h', 2, 2); top.weight = bottom.weight = 5000;
    root.children.push_back(std::move(top)); root.children.push_back(std::move(bottom));
    return root;
  }
  return std::nullopt;
}

std::vector<std::string> SplitMenuPresets(int panes) {
  if (panes == 2) return {"columns", "rows"};
  if (panes == 3) return {"columns", "main-left", "main-top"};
  if (panes == 4) return {"grid", "columns", "main-left"};
  return {};
}

const SplitNode* SplitNodeAt(const SplitNode& root, const std::vector<int>& path) {
  const SplitNode* node = &root;
  for (const int index : path) {
    if (index < 0 || index >= static_cast<int>(node->children.size())) return nullptr;
    node = &node->children[index];
  }
  return node;
}

SplitNode EqualizeSplitLayout(SplitNode root) {
  auto walk = [](const auto& self, SplitNode& node) -> void {
    if (!Container(node)) return;
    Equalize(node);
    for (auto& child : node.children) self(self, child);
  };
  walk(walk, root);
  return root;
}

std::optional<SplitNode> EqualizeSplitContainer(const SplitNode& root, const std::vector<int>& path, bool linked) {
  SplitNode result = root;
  SplitNode* node = &result;
  for (const int index : path) {
    if (index < 0 || index >= static_cast<int>(node->children.size())) return std::nullopt;
    node = &node->children[index];
  }
  if (!Container(*node)) return std::nullopt;
  Equalize(*node);
  if (linked && LinkableSplitGrid(result) && path.size() == 1) Equalize(result.children[1 - path[0]]);
  return result;
}

SplitGeometry LayoutSplitPanes(const SplitNode& root, int panes, SplitRect area, const SplitMetrics& metrics, bool linked) {
  SplitGeometry geometry;
  if (!ValidSplitLayout(root, panes)) return geometry;
  geometry.panes.resize(panes);
  std::vector<int> path;
  Place(root, area, metrics, path, geometry);
  if (linked && LinkableSplitGrid(root)) {
    const auto outer = std::find_if(geometry.dividers.begin(), geometry.dividers.end(), [](const auto& d) { return d.path.empty(); });
    const auto inner = std::find_if(geometry.dividers.begin(), geometry.dividers.end(), [](const auto& d) { return d.path.size() == 1; });
    if (outer != geometry.dividers.end() && inner != geometry.dividers.end()) {
      const int side = metrics.gap * 3;
      const int cx = root.axis == 'v' ? inner->rect.x + inner->rect.width / 2 : outer->rect.x + outer->rect.width / 2;
      const int cy = root.axis == 'v' ? outer->rect.y + outer->rect.height / 2 : inner->rect.y + inner->rect.height / 2;
      geometry.dividers.insert(geometry.dividers.begin(), SplitDivider{{}, 0, root.axis, true, {cx - side / 2, cy - side / 2, side, side}});
    }
  }
  return geometry;
}

std::optional<SplitNode> DragSplitDivider(const SplitNode& root, int panes, SplitRect area, const SplitMetrics& metrics,
                                          const SplitDivider& divider, int x, int y, bool linked) {
  if (!ValidSplitLayout(root, panes)) return std::nullopt;
  SplitNode result = root;
  const bool grid = linked && LinkableSplitGrid(result);
  auto drag = [&](const std::vector<int>& path, int index, int px, int py) {
    const auto rect = ContainerRect(result, area, metrics, path);
    SplitNode* node = &result;
    for (const int i : path) node = &node->children[i];
    if (!rect) return false;
    return DragPair(*node, *rect, metrics, index, node->axis == 'h' ? px : py);
  };
  if (divider.cross) {
    if (!grid || !drag({}, 0, x, y) || !drag({0}, 0, x, y)) return std::nullopt;
    result.children[1].children[0].weight = result.children[0].children[0].weight;
    result.children[1].children[1].weight = result.children[0].children[1].weight;
  } else {
    if (!drag(divider.path, divider.index, x, y)) return std::nullopt;
    if (grid && divider.path.size() == 1) {
      const auto& source = result.children[divider.path[0]];
      auto& target = result.children[1 - divider.path[0]];
      target.children[0].weight = source.children[0].weight; target.children[1].weight = source.children[1].weight;
    }
  }
  if (!ValidSplitLayout(result, panes)) return std::nullopt;
  return result;
}

int NeighbourSplitPane(const SplitGeometry& geometry, int slot, int dx, int dy) {
  if (slot < 0 || slot >= static_cast<int>(geometry.panes.size()) || (dx == 0) == (dy == 0)) return -1;
  const auto& from = geometry.panes[slot];
  const int fx = from.x + from.width / 2, fy = from.y + from.height / 2;
  int best = -1; long long best_score = LLONG_MAX;
  for (int i = 0; i < static_cast<int>(geometry.panes.size()); ++i) {
    if (i == slot) continue;
    const auto& to = geometry.panes[i];
    const int primary = dx > 0 ? to.x - (from.x + from.width) : dx < 0 ? from.x - (to.x + to.width)
                       : dy > 0 ? to.y - (from.y + from.height) : from.y - (to.y + to.height);
    if (primary < 0) continue;
    const bool overlaps = dx != 0 ? to.y < from.y + from.height && from.y < to.y + to.height
                                  : to.x < from.x + from.width && from.x < to.x + to.width;
    const int perpendicular = dx != 0 ? std::abs(to.y + to.height / 2 - fy) : std::abs(to.x + to.width / 2 - fx);
    const long long score = (overlaps ? 0LL : 1LL << 40) + 100000LL * primary + perpendicular;
    if (score < best_score) { best_score = score; best = i; }
  }
  return best;
}
}  // namespace red
