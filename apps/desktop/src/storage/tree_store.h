#pragma once
#include <cstdint>
#include <optional>
#include <filesystem>
#include <string>
#include <vector>
#include <map>
#include "app/theme_palette.h"

struct sqlite3;
namespace red {
struct TreeNode {
  std::string id, parent, title, url;
  bool folder = false, pinned = false, expanded = true, favorite = false, archived = false;
  int position = 0;
  std::string page_title;
  bool custom_title = false;
  // Milliseconds since the Unix epoch when archived; 0 when not archived or
  // archived before schema v4 (unknown).
  int64_t archived_at = 0;
  int64_t last_active_at = 0;
  // Space of a top-level Pinned/Today/archived entry (schema v6). Empty for
  // Favorites (shared by every space) and nested entries (their root's space).
  std::string space;
  bool operator==(const TreeNode&) const = default;
};
// An Arc-style Space: its own Pinned, Today and theme; Favorites are shared.
struct Space {
  std::string id, name, icon;  // icon: one emoji/letter, or empty (first letter of the name)
  int position = 0;
  std::string active_id;  // Last page shown in this space, when it still exists.
  int theme_hue = 307, theme_intensity = 100, theme_mode = 0, theme_noise = 0;
  std::optional<ThemePalette> theme_palette;
  bool operator==(const Space&) const = default;
};
constexpr int kMaxSpaces = 24;
struct SplitGroup {
  std::string anchor, other, title;
  int ratio = 5000;  // Basis points; relative pane allocation.
  bool vertical = false, reversed = false;
  bool operator==(const SplitGroup&) const = default;
};
struct TreeSnapshot {
  std::int64_t revision = 0;
  std::string active_id;
  std::vector<TreeNode> nodes;
  int sidebar_width = 256;
  std::vector<Space> spaces;  // At least one, ordered by position.
  std::string active_space;
  std::vector<SplitGroup> splits;
  std::vector<SplitGroup> archived_splits;
  const Space* ActiveSpace() const;
};
// Sorted root + descendant IDs; empty for an unknown/non-folder root.
std::vector<std::string> FolderNodeIds(const TreeSnapshot& tree, const std::string& id);

enum class TreeAction { CreateFolder, CreateTab, Rename, AutomaticTitle, Move, Place, Pin, Expand, Activate, RemoveLeaf, RemoveFolder, SetUrl, Unload, Archive, Restore,
  CreateSpace, EditSpace, DeleteSpace, SwitchSpace, MoveSpace, MoveToSpace,
  CreateSplit, RemoveSplit, RenameSplit, LayoutSplit, SwapSplit, AutoArchive };
struct TreeCommand {
  TreeAction action;
  std::int64_t expected_revision = 0;
  std::string id, parent, title, url, section, next_active;
  std::string space, icon;  // Space commands; MoveToSpace target.
  std::string after;  // CreateTab: a Today tab to open directly below (a link's opener); else first.
  std::string split_with;  // Native Peek adoption: atomically create Today + pair with this existing page.
  std::string split_source_url,split_source_title;  // Pinned/Favorite source is duplicated into Today.
  std::vector<std::string> confirmed_nodes;  // Native captured folder scope, checked again at commit.
  // SwitchSpace/DeleteSpace: when set, the space opens only on one of these (loaded pages), else empty.
  std::optional<std::vector<std::string>> live_nodes;
  int position = -1;  // Append; otherwise index after removing the moved node.
  bool value = false;
  bool reversed = false;  // New split placement, independent of orientation.
  int64_t not_after = 0;  // Native auto-archive eligibility cutoff.
};
struct TreeResult {
  int code = 0;
  std::string error, created_id;
  TreeSnapshot snapshot;
  explicit operator bool() const { return code == 0; }
};
// Single owning thread. No CEF, renderer, JSON or live-browser dependency.
class TreeStore {
 public:
  TreeStore() = default;
  ~TreeStore();
  TreeStore(const TreeStore&) = delete;
  TreeStore& operator=(const TreeStore&) = delete;
  TreeResult Open(const std::filesystem::path& path);
  TreeResult Read();
  TreeResult Apply(const TreeCommand& command, const TreeSnapshot* window = nullptr);
  TreeResult Import(const TreeSnapshot& incoming, const std::filesystem::path& backup);
  static bool ValidateSnapshot(const TreeSnapshot& snapshot);
  // Metadata/preferences do not invalidate unrelated optimistic tree mutations.
  TreeResult SetPageTitle(const std::string& id, const std::string& title);
  TreeResult SetSidebarWidth(int width);
  TreeResult TouchTabs(const std::map<std::string,int64_t>& activity);
  // The active space's theme.
  TreeResult SetTheme(int hue, int intensity, int mode, int noise, const std::string& space = "",
                      const std::optional<ThemePalette>& palette = std::nullopt);
 private:
  TreeResult Load();
  TreeResult Save(TreeResult result, const TreeSnapshot& before);
  bool Exec(const char* sql);
  sqlite3* db_ = nullptr;
};
}  // namespace red
