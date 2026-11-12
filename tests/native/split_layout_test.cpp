#include "workspace/split_layout.h"
#include <algorithm>
#include <cstdio>
#include <functional>
#include <set>
#include <string>

// Split View layout trees: parsing, normalization, edits, presets and geometry.
namespace {
int failures = 0;
void Check(bool ok, const char* what) { if (!ok) { std::printf("FAIL %s\n", what); ++failures; } }
void Same(const std::string& got, const std::string& want, const char* what) {
  if (got != want) { std::printf("FAIL %s: got '%s', want '%s'\n", what, got.c_str(), want.c_str()); ++failures; }
}
std::string Text(const std::optional<red::SplitNode>& node) { return node ? red::FormatSplitLayout(*node) : "<none>"; }
// The arrangement alone: weights dropped, panes numbered in reading order.
void Shape(const red::SplitNode& node, std::string& out, int& next) {
  if (!node.axis) { out += static_cast<char>('a' + next++); return; }
  out += node.axis; out += '(';
  for (size_t i = 0; i < node.children.size(); ++i) { if (i) out += ','; Shape(node.children[i], out, next); }
  out += ')';
}
std::string Shape(const red::SplitNode& node) { std::string out; int next = 0; Shape(node, out, next); return out; }
bool Overlap(const red::SplitRect& a, const red::SplitRect& b) {
  return a.x < b.x + b.width && b.x < a.x + a.width && a.y < b.y + b.height && b.y < a.y + a.height;
}
}  // namespace

int main() {
  using red::SplitEdge;
  const red::SplitMetrics metrics{8, 220, 160};
  // Canonical text round-trips; anything else is rejected.
  Same(Text(red::ParseSplitLayout("h(6000*0,4000*v(5000*1,5000*2))", 3)), "h(6000*0,4000*v(5000*1,5000*2))", "canonical layout round-trips");
  for (const char* bad : {"h(5000*0,5000*h(5000*1,5000*2))", "h(9500*0,500*1)", "h(5000*0,5000*0)", "h(5000*0,5000*2)", "h(05000*0,5000*1)",
                          "h(5000*0,5000*1))", "h(10000*0)", "0", "x(5000*0,5000*1)", "h(5000*0, 5000*1)", "", "h(5000*0,5000*1"})
    Check(!red::ParseSplitLayout(bad, 2) && !red::ParseSplitLayout(bad, 3), bad);
  Check(!red::ParseSplitLayout("h(2000*0,2000*1,2000*2,2000*3,2000*4)", 5), "five panes are rejected");
  Check(!red::ParseSplitLayout("h(5000*0,5000*1)", 3), "every slot must appear");
  // Schema 15 -> 16: ratio is the leading pane; reversed puts the second page first.
  Same(red::FormatSplitLayout(red::TwoPaneSplitLayout(false, 6200, false)), "h(6200*0,3800*1)", "two-pane migration keeps ratio");
  Same(red::FormatSplitLayout(red::TwoPaneSplitLayout(true, 3000, true)), "v(3000*1,7000*0)", "reversed vertical migration keeps order");
  // Every arrangement of 2-4 panes is reachable by inserting at pane or window edges: 2, 6 and 22 (Schroeder numbers).
  {
    std::set<std::string> seen[5];
    std::function<void(const red::SplitNode&)> grow = [&](const red::SplitNode& root) {
      const int panes = red::SplitPaneCount(root);
      Check(red::ValidSplitLayout(root, panes), "grown layout is valid");
      if (!seen[panes].insert(Shape(root)).second) return;
      Same(Text(red::ParseSplitLayout(red::FormatSplitLayout(root), panes)), red::FormatSplitLayout(root), "grown layout round-trips");
      if (panes == red::kMaxSplitPanes) {
        Check(!red::InsertSplitPane(root, 0, SplitEdge::Left, panes), "a fifth pane is refused");
        return;
      }
      for (int target = -1; target < panes; ++target)
        for (const auto edge : {SplitEdge::Left, SplitEdge::Right, SplitEdge::Top, SplitEdge::Bottom})
          if (const auto next = red::InsertSplitPane(root, target, edge, panes)) grow(*next);
    };
    grow(red::TwoPaneSplitLayout(false, 5000, false));
    grow(red::TwoPaneSplitLayout(true, 5000, false));
    Check(seen[2].size() == 2 && seen[3].size() == 6 && seen[4].size() == 22, "2, 6 and 22 distinct arrangements");
  }
  // Insertion details.
  const auto main_left = red::ParseSplitLayout("h(6000*0,4000*v(5000*1,5000*2))", 3);
  Same(Text(red::InsertSplitPane(*main_left, 2, SplitEdge::Bottom, 3)), "h(6000*0,4000*v(5000*1,2500*2,2500*3))", "same-axis insert halves the target");
  Same(Text(red::InsertSplitPane(*main_left, 2, SplitEdge::Left, 3)), "h(6000*0,4000*v(5000*1,5000*h(5000*3,5000*2)))", "cross-axis insert wraps the target");
  Same(Text(red::InsertSplitPane(*main_left, -1, SplitEdge::Right, 3)), "h(4000*0,2666*v(5000*1,5000*2),3334*3)", "window edge adds a full-height pane");
  Same(Text(red::InsertSplitPane(*main_left, -1, SplitEdge::Top, 3)), "v(5000*3,5000*h(6000*0,4000*v(5000*1,5000*2)))", "window edge across wraps the root");
  Check(!red::InsertSplitPane(*main_left, 2, SplitEdge::Left, 4), "the new pane takes the next slot");
  Check(!red::InsertSplitPane(*main_left, 7, SplitEdge::Left, 3), "unknown target is refused");
  // Removal: share goes to the neighbour, containers collapse, slots renumber.
  const auto grid = red::SplitPreset("grid", 4);
  Same(Text(grid), "v(5000*h(5000*0,5000*1),5000*h(5000*2,5000*3))", "grid preset");
  Same(Text(red::RemoveSplitPane(*grid, 3)), "v(5000*h(5000*0,5000*1),5000*2)", "removing from a grid row collapses it");
  Same(Text(red::RemoveSplitPane(*grid, 0)), "v(5000*0,5000*h(5000*1,5000*2))", "removing slot 0 renumbers the rest");
  Same(Text(red::RemoveSplitPane(*main_left, 0)), "v(5000*0,5000*1)", "removing the main pane flattens to the remaining column");
  Check(!red::RemoveSplitPane(red::TwoPaneSplitLayout(false, 5000, false), 0), "two panes dissolve instead of removing");
  Same(red::FormatSplitLayout(red::SwapSplitSlots(*main_left, 0, 2)), "h(6000*2,4000*v(5000*1,5000*0))", "swap exchanges two panes");
  // Moving a pane by its header keeps every page's slot.
  Same(Text(red::MoveSplitPane(*grid, 0, 3, SplitEdge::Right)), "v(5000*1,5000*h(5000*2,2500*3,2500*0))", "move a grid pane beside another");
  Same(Text(red::MoveSplitPane(*grid, 3, -1, SplitEdge::Left)), "h(5000*3,5000*v(5000*h(5000*0,5000*1),5000*2))", "move a pane along the window edge");
  Same(Text(red::MoveSplitPane(red::TwoPaneSplitLayout(false, 5000, false), 0, 1, SplitEdge::Right)), "h(5000*1,5000*0)", "two panes swap order");
  Same(Text(red::MoveSplitPane(red::TwoPaneSplitLayout(false, 5000, false), 1, 0, SplitEdge::Bottom)), "v(5000*0,5000*1)", "two panes turn vertical");
  Check(!red::MoveSplitPane(*grid, 2, 2, SplitEdge::Left) && !red::MoveSplitPane(*grid, 0, 4, SplitEdge::Left) &&
        !red::MoveSplitPane(red::TwoPaneSplitLayout(false, 5000, false), 1, 0, SplitEdge::Right), "invalid or unchanged moves are refused");
  // Every move between every pair of panes at every edge stays valid with all slots.
  {
    std::function<void(const red::SplitNode&)> each = [&](const red::SplitNode& root) {
      const int panes = red::SplitPaneCount(root);
      for (int slot = 0; slot < panes; ++slot) for (int target = -1; target < panes; ++target)
        for (const auto edge : {SplitEdge::Left, SplitEdge::Right, SplitEdge::Top, SplitEdge::Bottom})
          if (const auto moved = red::MoveSplitPane(root, slot, target, edge)) Check(red::ValidSplitLayout(*moved, panes), "every move is valid");
      if (panes < red::kMaxSplitPanes) for (const auto edge : {SplitEdge::Right, SplitEdge::Bottom})
        if (const auto next = red::InsertSplitPane(root, panes - 1, edge, panes)) each(*next);
    };
    each(red::TwoPaneSplitLayout(false, 5000, false));
  }
  // Presets and menu.
  for (int panes = 2; panes <= 4; ++panes) for (const auto& preset : red::SplitMenuPresets(panes)) {
    const auto layout = red::SplitPreset(preset, panes);
    Check(layout && red::ValidSplitLayout(*layout, panes), "menu preset is valid");
  }
  Check(!red::SplitPreset("grid", 3) && !red::SplitPreset("diagonal", 2), "unknown or impossible preset");
  Same(Text(red::SplitPreset("columns", 3)), "h(3333*0,3333*1,3334*2)", "equal columns");
  Same(Text(red::SplitPreset("main-top", 4)), "v(5000*0,5000*h(3333*1,3333*2,3334*3))", "main on top");
  Check(red::LinkableSplitGrid(*grid) && !red::LinkableSplitGrid(*main_left), "linkable 2x2 only");
  // Geometry: exact cells, min sizes and cramped windows.
  {
    const auto two = red::LayoutSplitPanes(red::TwoPaneSplitLayout(false, 5000, false), 2, {0, 0, 1000, 600}, metrics);
    Check(two.panes.size() == 2 && two.panes[0] == red::SplitRect{0, 0, 496, 600} && two.panes[1] == red::SplitRect{504, 0, 496, 600}, "equal columns geometry");
    Check(two.dividers.size() == 1 && two.dividers[0].rect == red::SplitRect{496, 0, 8, 600} && two.dividers[0].axis == 'h', "one divider in the gap");
    const auto narrow = red::LayoutSplitPanes(*red::ParseSplitLayout("h(1000*0,9000*1)", 2), 2, {0, 0, 1000, 600}, metrics);
    Check(narrow.panes[0].width == 220 && narrow.panes[1].width == 772, "minimum width overrides a small share");
    const auto cramped = red::LayoutSplitPanes(red::TwoPaneSplitLayout(false, 8000, false), 2, {10, 20, 300, 200}, metrics);
    Check(cramped.panes[0].width == 146 && cramped.panes[1].x == 164 && cramped.panes[1].width == 146, "cramped panes shrink in proportion without overlap");
    const auto offset = red::LayoutSplitPanes(*main_left, 3, {100, 50, 1208, 808}, metrics);
    Check(offset.panes[0] == red::SplitRect{100, 50, 720, 808} && offset.panes[1] == red::SplitRect{828, 50, 480, 400} &&
          offset.panes[2] == red::SplitRect{828, 458, 480, 400}, "nested geometry");
    Check(offset.dividers.size() == 2 && offset.dividers[1].path == std::vector<int>{1} && offset.dividers[1].axis == 'v', "nested divider path");
  }
  // Every arrangement at several sizes: inside the area, disjoint, panes - 1 dividers.
  {
    std::set<std::string> done;
    std::function<void(const red::SplitNode&)> each = [&](const red::SplitNode& root) {
      const int panes = red::SplitPaneCount(root);
      if (!done.insert(red::FormatSplitLayout(root)).second) return;
      for (const auto area : {red::SplitRect{0, 0, 1900, 1000}, red::SplitRect{30, 40, 900, 500}, red::SplitRect{0, 0, 300, 200}}) {
        const auto geometry = red::LayoutSplitPanes(root, panes, area, metrics);
        bool ok = static_cast<int>(geometry.panes.size()) == panes && static_cast<int>(geometry.dividers.size()) == panes - 1;
        for (int i = 0; ok && i < panes; ++i) {
          const auto& p = geometry.panes[i];
          ok = p.width >= 0 && p.height >= 0 && p.x >= area.x && p.y >= area.y && p.x + p.width <= area.x + area.width && p.y + p.height <= area.y + area.height;
          for (int j = i + 1; ok && j < panes; ++j) ok = !Overlap(p, geometry.panes[j]);
          if (ok && area.width >= 1900) ok = p.width >= 220 && p.height >= 160;
        }
        Check(ok, "every arrangement lays out inside the area without overlap");
      }
      if (panes < red::kMaxSplitPanes)
        for (int target = -1; target < panes; ++target)
          for (const auto edge : {SplitEdge::Left, SplitEdge::Right, SplitEdge::Top, SplitEdge::Bottom})
            if (const auto next = red::InsertSplitPane(root, target, edge, panes)) each(*next);
    };
    each(red::TwoPaneSplitLayout(false, 5000, false)); each(red::TwoPaneSplitLayout(true, 5000, false));
  }
  // Dragging: only the two neighbours change; minimums clamp; a linked grid moves together.
  {
    const red::SplitRect area{0, 0, 1208, 808};
    const auto columns = red::SplitPreset("columns", 3);
    const auto geometry = red::LayoutSplitPanes(*columns, 3, area, metrics);
    const auto moved = red::DragSplitDivider(*columns, 3, area, metrics, geometry.dividers[0], 500, 0, false);
    Check(moved && moved->children[2].weight == 3334 && moved->children[0].weight > 4100 && moved->children[0].weight < 4250 && moved->children[0].weight + moved->children[1].weight == 6666,
          "dragging changes only the neighbouring panes");
    const auto clamped = red::DragSplitDivider(*columns, 3, area, metrics, geometry.dividers[0], 5, 0, false);
    Check(clamped && red::LayoutSplitPanes(*clamped, 3, area, metrics).panes[0].width == 220, "minimum width clamps the drag");
    const auto linked = red::LayoutSplitPanes(*grid, 4, area, metrics, true);
    Check(linked.dividers.size() == 4 && linked.dividers[0].cross, "a linked grid adds its intersection handle first");
    const auto inner = std::find_if(linked.dividers.begin(), linked.dividers.end(), [](const auto& d) { return d.path == std::vector<int>{1}; });
    const auto rows = red::DragSplitDivider(*grid, 4, area, metrics, *inner, 300, 600, true);
    Check(rows && rows->children[0].children[0].weight == rows->children[1].children[0].weight && rows->children[0].children[0].weight < 5000,
          "linked inner dividers move together");
    const auto independent = red::DragSplitDivider(*grid, 4, area, metrics, *inner, 300, 600, false);
    Check(independent && independent->children[0].children[0].weight == 5000, "unlinked rows move alone");
    const auto cross = red::DragSplitDivider(*grid, 4, area, metrics, linked.dividers[0], 300, 300, true);
    Check(cross && cross->children[0].weight < 5000 && cross->children[1].children[0].weight < 5000, "the intersection moves both axes");
    Same(Text(red::EqualizeSplitContainer(*rows, {0}, true)), "v(5000*h(5000*0,5000*1),5000*h(5000*2,5000*3))", "equalize a linked row");
  }
  // Spatial focus.
  {
    const auto geometry = red::LayoutSplitPanes(*main_left, 3, {0, 0, 1208, 808}, metrics);
    Check(red::NeighbourSplitPane(geometry, 0, 1, 0) == 1 && red::NeighbourSplitPane(geometry, 1, 0, 1) == 2 &&
          red::NeighbourSplitPane(geometry, 2, -1, 0) == 0 && red::NeighbourSplitPane(geometry, 0, -1, 0) == -1, "arrow focus follows geometry");
  }
  if (failures) { std::printf("%d failure(s)\n", failures); return 1; }
  std::printf("split layout: all checks passed\n");
  return 0;
}
