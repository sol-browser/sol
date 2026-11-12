#pragma once
// Split View layout: a slicing tree of up to four panes. Pure data and integer
// geometry; no CEF, Win32 or JSON. Every guillotine arrangement of 2-4 panes
// has exactly one normalized tree (2, 6 and 22 of them), see docs/split-view.md.
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace red {
constexpr int kMaxSplitPanes = 4;
constexpr int kMinSplitWeight = 1000;  // Basis points of the parent.

struct SplitNode {
  char axis = 0;        // 'h': children side by side; 'v': top to bottom; 0: a pane.
  int slot = -1;        // A pane: index into SplitGroup::pages.
  int weight = 10000;   // Share of the parent along its axis, basis points.
  std::vector<SplitNode> children;
  bool operator==(const SplitNode&) const = default;
};

// Canonical text: pane = slot digit; container = axis '(' weight '*' node {',' ...} ')'.
// Example: "h(6000*0,4000*v(5000*1,5000*2))". Rejects anything not canonical/valid.
std::optional<SplitNode> ParseSplitLayout(std::string_view text, int panes);
std::string FormatSplitLayout(const SplitNode& root);
bool ValidSplitLayout(const SplitNode& root, int panes);
int SplitPaneCount(const SplitNode& node);
// Slots in reading order (left to right, top to bottom through the tree).
std::vector<int> SplitReadingOrder(const SplitNode& root);
// Flattens a container inside one of its own axis, collapses one-child
// containers and rebalances weights (sum 10000, each >= kMinSplitWeight).
SplitNode NormalizeSplitLayout(SplitNode root);
// The schema-15 two-pane form: |ratio| is the leading (left/top) pane's share;
// |reversed| puts slot 1 (the second page) first.
SplitNode TwoPaneSplitLayout(bool vertical, int ratio, bool reversed);
// A 2x2 tree whose inner dividers may move together (V[H2,H2] or H[V2,V2]).
bool LinkableSplitGrid(const SplitNode& root);

enum class SplitEdge { Left, Right, Top, Bottom };
bool ParseSplitEdge(std::string_view text, SplitEdge& edge);
// Splits pane |target| (or the whole area for -1) at |edge|; the new pane gets |new_slot|.
std::optional<SplitNode> InsertSplitPane(const SplitNode& root, int target, SplitEdge edge, int new_slot);
// Removes pane |slot|; its share goes to the neighbouring sibling; slots after
// it are renumbered down by one (SplitGroup::pages erases that entry).
std::optional<SplitNode> RemoveSplitPane(const SplitNode& root, int slot);
SplitNode SwapSplitSlots(SplitNode root, int a, int b);
// Moves pane |slot| beside pane |target| at |edge| (-1: along the whole area's
// edge), as when its header is dragged there. Slots (the pages' order) keep
// their meaning; nullopt when invalid or when nothing would change.
std::optional<SplitNode> MoveSplitPane(const SplitNode& root, int slot, int target, SplitEdge edge);
// Renames each pane |slot| to |slots[slot]| (a preset keeps the pages' reading order).
SplitNode RelabelSplitSlots(SplitNode root, const std::vector<int>& slots);
// Moves divider (path, index) by |delta| basis points of its pair, within limits.
std::optional<SplitNode> NudgeSplitDivider(const SplitNode& root, const std::vector<int>& path, int index, int delta, bool linked);
// Presets: "columns", "rows", "main-left", "main-top", "grid" (four panes).
std::optional<SplitNode> SplitPreset(std::string_view preset, int panes);
// The presets the layout menu offers for |panes|.
std::vector<std::string> SplitMenuPresets(int panes);
// Equal shares in every container ("Equal sizes").
SplitNode EqualizeSplitLayout(SplitNode root);
// Equal shares in the container at |path| (double-click/Home on its divider).
std::optional<SplitNode> EqualizeSplitContainer(const SplitNode& root, const std::vector<int>& path, bool linked);
const SplitNode* SplitNodeAt(const SplitNode& root, const std::vector<int>& path);

struct SplitRect {
  int x = 0, y = 0, width = 0, height = 0;
  bool Contains(int px, int py) const { return px >= x && py >= y && px < x + width && py < y + height; }
  bool operator==(const SplitRect&) const = default;
};
struct SplitDivider {
  std::vector<int> path;  // Container: child indices from the root.
  int index = 0;          // Between children index and index + 1.
  char axis = 0;          // Container axis: 'h' moves along x, 'v' along y.
  bool cross = false;     // Linked 2x2 intersection: moves both axes.
  SplitRect rect;
  bool operator==(const SplitDivider&) const = default;
};
struct SplitGeometry {
  std::vector<SplitRect> panes;  // By slot.
  std::vector<SplitDivider> dividers;
};
struct SplitMetrics { int gap = 8, min_width = 220, min_height = 160; };
// When the area is smaller than the panes' minimums, panes shrink in
// proportion to their minimum; they never overlap or leave the area.
SplitGeometry LayoutSplitPanes(const SplitNode& root, int panes, SplitRect area, const SplitMetrics& metrics, bool linked = false);
// Moves divider (path, index) toward point (x, y), clamped by minimum sizes and
// weights; a linked grid moves both inner dividers; a cross moves both axes.
std::optional<SplitNode> DragSplitDivider(const SplitNode& root, int panes, SplitRect area, const SplitMetrics& metrics,
                                          const SplitDivider& divider, int x, int y, bool linked);
// The pane nearest (dx, dy) from |slot| (spatial focus movement); -1 if none.
int NeighbourSplitPane(const SplitGeometry& geometry, int slot, int dx, int dy);
}  // namespace red
