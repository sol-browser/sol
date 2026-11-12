#include "import/formats/arc_sidebar.h"
#include "import/formats/json_lite.h"
#include <cmath>
#include <set>
#include <windows.h>

namespace red::importer {
namespace {
const Json& Field(const Json& object, std::string_view key) {
  static const Json empty;
  const auto* value = object.Get(key);
  return value ? *value : empty;
}

std::string String(const Json& value) {
  return value.type == Json::Type::String ? value.text : std::string{};
}

bool UsableId(const std::string& id) { return !id.empty() && id.size() <= 256; }

const Json* MainSidebar(const Json& file) {
  const auto& containers = Field(Field(file, "sidebar"), "containers").items;
  for (size_t i = 0; i + 1 < containers.size(); i += 2) {
    if (containers[i].Get("global")) return &containers[i + 1];
  }
  return nullptr;
}

struct ArcProfile {
  std::string directory = "Default";
  std::string machine;
  std::string Key() const { return directory + '\n' + machine; }
};

ArcProfile ReadProfile(const Json& profile) {
  const auto& custom = Field(Field(profile, "custom"), "_0");
  auto directory = custom.Str("directoryBasename");
  if (!UsableId(directory) || directory == "Default") return {};
  auto machine = custom.Str("machineID");
  return {std::move(directory), UsableId(machine) ? std::move(machine) : std::string{}};
}

std::vector<std::string> SpaceRoots(const Json& space, std::string_view section) {
  std::vector<std::string> roots;
  for (auto field : {"newContainerIDs", "containerIDs"}) {
    const auto& pairs = Field(space, field).items;
    for (size_t i = 0; i + 1 < pairs.size(); i += 2) {
      if (pairs[i].Get(section) || String(pairs[i]) == section) {
        auto id = String(pairs[i + 1]);
        if (UsableId(id)) roots.push_back(std::move(id));
      }
    }
    if (!roots.empty()) break;
  }
  return roots;
}

std::string Icon(const Json& icon) {
  auto emoji = CleanLabel(icon.Str("emoji_v2"), 48, "");
  if (!emoji.empty() && emoji.find(' ') == std::string::npos &&
      std::count_if(emoji.begin(), emoji.end(), [](unsigned char c) { return (c & 0xc0) != 0x80; }) <= 16 &&
      MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, emoji.data(), static_cast<int>(emoji.size()), nullptr, 0)) return emoji;
  auto codepoint = icon.Int("emoji");
  if (codepoint >= 0xa0 && codepoint <= 0x10ffff && (codepoint < 0xd800 || codepoint > 0xdfff)) {
    wchar_t chars[2]{};
    int length = 1;
    if (codepoint > 0xffff) {
      codepoint -= 0x10000;
      chars[0] = static_cast<wchar_t>(0xd800 + (codepoint >> 10));
      chars[1] = static_cast<wchar_t>(0xdc00 + (codepoint & 0x3ff));
      length = 2;
    } else chars[0] = static_cast<wchar_t>(codepoint);
    char utf8[4]{};
    const int bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, chars, length, utf8, sizeof(utf8), nullptr, nullptr);
    if (bytes) return std::string(utf8, bytes);
  }
  static const std::map<std::string, std::string> icons = {
      {"book", "📖"}, {"briefcase", "💼"}, {"code", "💻"}, {"flash", "⚡"},
      {"heart", "♥️"}, {"home", "🏠"}, {"musicalNote", "🎵"}, {"planet", "🪐"},
      {"restaurant", "🍴"}, {"star", "⭐"}, {"triangle", "🔺"}};
  const auto found = icons.find(icon.Str("icon"));
  return found == icons.end() ? std::string{} : found->second;
}

std::optional<double> Number(const Json& value) {
  if (value.type != Json::Type::Number || !std::isfinite(value.number)) return std::nullopt;
  return value.number;
}

void ApplyTheme(const Json& theme, Space& space) {
  const auto& background = Field(Field(Field(theme, "background"), "single"), "_0");
  const auto& style = Field(Field(Field(background, "style"), "color"), "_0");
  const auto& gradient = Field(Field(style, "blendedGradient"), "_0");
  const auto& single = Field(Field(style, "blendedSingleColor"), "_0");
  const auto& stops = Field(gradient, "baseColors").items;
  const auto& color = !stops.empty() ? stops.front() : single.IsObject() ? Field(single, "color") :
      Field(Field(theme, "primaryColorPalette"), "midTone");
  const auto red = Number(Field(color, "red"));
  const auto green = Number(Field(color, "green"));
  const auto blue = Number(Field(color, "blue"));
  if (!red || !green || !blue) return;
  const double r = std::clamp(*red, 0.0, 1.0), g = std::clamp(*green, 0.0, 1.0), b = std::clamp(*blue, 0.0, 1.0);
  const double hi = std::max({r, g, b}), delta = hi - std::min({r, g, b});
  const double hue = delta == 0 ? 0 : hi == r ? (g - b) / delta : hi == g ? 2 + (b - r) / delta : 4 + (r - g) / delta;
  space.theme_hue = (static_cast<int>(std::lround(60 * hue)) + 360) % 360;
  const auto& modifiers = Field(single.IsObject() ? single : gradient, "modifiers");
  const double intensity = Number(Field(modifiers, "intensityFactor")).value_or(1);
  const double noise = Number(Field(modifiers, "noiseFactor")).value_or(0);
  space.theme_intensity = delta == 0 ? 0 : static_cast<int>(std::lround(100 * std::clamp(intensity, 0.0, 1.0)));
  space.theme_noise = static_cast<int>(std::lround(100 * std::clamp(noise, 0.0, 1.0)));
  // Arc's value describes the foreground. Light text requires a dark background.
  space.theme_mode = background.Str("contentOverBackgroundAppearance") == "light" ? 1 : 0;
}

std::set<std::string> ExpandedFolders(std::string_view windows) {
  std::set<std::string> expanded;
  const auto file = ParseJson(windows);
  if (!file) return expanded;
  for (const auto& window : Field(*file, "windows").items) {
    const auto& pairs = Field(window, "expandedItems").items;
    for (size_t i = 0; i + 1 < pairs.size(); i += 2)
      if (pairs[i + 1].Get("expanded")) expanded.insert(String(pairs[i]));
  }
  return expanded;
}

bool ReadTab(const Json& item, TreeNode& node) {
  const auto& tab = Field(Field(item, "data"), "tab");
  node.url = tab.Str("savedURL");
  if (!IsWebUrl(node.url)) return false;
  node.page_title = CleanLabel(tab.Str("savedTitle"), 240, "");
  node.custom_title = !node.title.empty();
  if (!node.custom_title) node.title = CleanLabel(node.page_title, 240, "Imported tab");
  return true;
}

class SidebarReader {
 public:
  SidebarReader(const std::string& source, std::set<std::string> expanded)
      : source_(source), expanded_(std::move(expanded)) {}

  ArcSidebar Read(const Json& sidebar, std::string_view archive) {
    const auto& raw_items = Field(sidebar, "items").items;
    for (const auto& value : raw_items) {
      auto id = value.Str("id");
      if (!UsableId(id) || !items_.emplace(id, &value).second) continue;
      auto parent = value.Str("parentID");
      if (UsableId(parent)) by_parent_[std::move(parent)].push_back(std::move(id));
    }

    auto& tree = result_.workspace.tree;
    const auto& raw_spaces = Field(sidebar, "spaces").items;
    std::map<std::string, std::string> space_ids;
    std::map<std::string, std::string> favorite_space;
    for (const auto& value : raw_spaces) {
      const auto id = value.Str("id");
      if (!UsableId(id) || space_ids.contains(id)) continue;
      if (tree.spaces.size() == kMaxSpaces) return Fail("Arc has more than 24 Spaces. Nothing imported.");
      const auto profile = ReadProfile(Field(value, "profile"));
      Space space;
      space.id = StableId(source_, "space:" + id);
      space_ids.emplace(id, space.id);
      space.name = CleanLabel(value.Str("title"), 40, "Arc Space");
      space.position = static_cast<int>(tree.spaces.size());
      space.profile = StableId(source_, "profile:" + profile.Key());
      if (favorite_space.try_emplace(space.profile, space.id).second) {
        tree.profiles.push_back({space.profile, CleanLabel("Arc · " + profile.directory, 40, "Arc")});
        result_.profile_directories.emplace(space.profile, profile.directory);
      }
      const auto& custom = Field(value, "customInfo");
      space.icon = Icon(Field(custom, "iconType"));
      ApplyTheme(Field(custom, "windowTheme"), space);
      tree.spaces.push_back(std::move(space));
      const auto& added = tree.spaces.back();
      for (const auto& root : SpaceRoots(value, "pinned")) ReadRoot(root, added.id, added.profile, Section::Pinned);
      for (const auto& root : SpaceRoots(value, "unpinned")) ReadRoot(root, added.id, added.profile, Section::Today);
    }
    if (tree.spaces.empty()) return Fail("No Spaces were found in this Arc sidebar file.");
    tree.active_space = tree.spaces.front().id;

    const auto& favorites = Field(sidebar, "topAppsContainerIDs").items;
    for (size_t i = 0; i + 1 < favorites.size(); i += 2)
      ReadFavorites(String(favorites[i + 1]), ReadProfile(favorites[i]), favorite_space);
    // Some snapshots have the profile on the container but omit the index.
    for (const auto& [id, value] : items_) {
      const auto& container = Field(Field(Field(*value, "data"), "itemContainer"), "containerType");
      if (const auto* apps = container.Get("topApps"))
        ReadFavorites(id, ReadProfile(Field(*apps, "_0")), favorite_space);
    }
    if (too_many_nodes_) return Fail("Arc's sidebar exceeds Sol's limit of 10,000 entries. Nothing imported.");
    if (!archive.empty()) ReadArchive(archive, space_ids);
    if (!TreeStore::ValidateSnapshot(tree)) return Fail("The Arc sidebar could not be arranged within Sol's workspace limits.");
    return std::move(result_);
  }

 private:
  enum class Section { Pinned, Today, Favorites };

  ArcSidebar Fail(const char* message) {
    ArcSidebar failure;
    failure.workspace.error = message;
    return failure;
  }

  std::vector<std::string> Children(const std::string& id) const {
    const auto item = items_.find(id);
    const auto* children = item == items_.end() ? nullptr : item->second->Get("childrenIds");
    if (children && children->IsArray()) {
      std::vector<std::string> ids;
      for (const auto& child : children->items) if (auto key = String(child); UsableId(key)) ids.push_back(std::move(key));
      return ids;
    }
    const auto found = by_parent_.find(id);
    return found == by_parent_.end() ? std::vector<std::string>{} : found->second;
  }

  void ReadFavorites(const std::string& root, const ArcProfile& arc, const std::map<std::string, std::string>& spaces) {
    const auto profile = StableId(source_, "profile:" + arc.Key());
    const auto space = spaces.find(profile);
    if (space != spaces.end()) ReadRoot(root, space->second, profile, Section::Favorites);
    else if (UsableId(root) && visited_.insert(root).second) result_.skipped += static_cast<int>(Children(root).size());
  }

  void ReadRoot(const std::string& root, const std::string& space, const std::string& profile, Section section) {
    if (root.empty() || !visited_.insert(root).second) return;
    for (const auto& id : Children(root)) Visit(id, "", space, profile, section, 1);
  }

  void Add(TreeNode node) {
    node.position = positions_[node.parent]++;
    result_.workspace.tree.nodes.push_back(std::move(node));
  }

  void ReadArchive(std::string_view json, const std::map<std::string, std::string>& spaces) {
    const auto archive = ParseJson(json);
    if (!archive || !Field(*archive, "items").IsArray()) { result_.archive_incomplete = true; return; }
    const auto& entries = Field(*archive, "items").items;
    std::set<std::string> seen;
    auto& tree = result_.workspace.tree;
    for (size_t i = 0; i + 1 < entries.size(); i += 2) {
      if (tree.nodes.size() == 10000) { result_.archive_incomplete = true; return; }
      const auto id = String(entries[i]);
      const auto& record = entries[i + 1];
      const auto& item = Field(record, "sidebarItem");
      TreeNode node;
      node.title = CleanLabel(item.Str("title"), 240, "");
      if (!UsableId(id) || !seen.insert(id).second || !ReadTab(item, node)) { result_.archive_incomplete = true; continue; }
      node.id = StableId(source_, "archived:" + id);
      result_.favicon_item_ids.emplace(node.id, id);
      node.archived = true;
      const auto source_space = Field(Field(record, "source"), "space").Str("_0");
      const auto space = spaces.find(source_space);
      node.space = space == spaces.end() ? tree.active_space : space->second;
      // Arc stores seconds since 2001-01-01 UTC, Sol uses Unix milliseconds.
      const auto time = Number(Field(record, "archivedAt"));
      if (time && *time >= 0 && *time < 1e10) node.archived_at = static_cast<int64_t>((*time + 978307200.0) * 1000.0);
      Add(std::move(node));
      ++result_.archived;
    }
  }

  void Visit(const std::string& id, const std::string& parent, const std::string& space,
             const std::string& profile, Section section, int depth) {
    if (result_.workspace.tree.nodes.size() == 10000) { too_many_nodes_ = true; return; }
    const auto found = items_.find(id);
    if (found == items_.end() || depth > 64 || !visited_.insert(id).second) { ++result_.skipped; return; }
    const auto& item = *found->second;
    const auto& data = Field(item, "data");
    TreeNode node;
    node.id = StableId(source_, "item:" + id);
    node.parent = parent;
    node.space = parent.empty() ? space : "";
    node.pinned = section != Section::Today;
    node.title = CleanLabel(item.Str("title"), 240, "");
    if (data.Get("tab")) {
      if (!ReadTab(item, node)) { ++result_.skipped; return; }
      result_.favicon_item_ids.emplace(node.id, id);
      if (section == Section::Favorites && favorites_[profile] < 12) {
        node.favorite = true;
        node.space.clear();
        node.profile = profile;
        ++favorites_[profile];
        ++result_.favorites;
      }
      Add(std::move(node));
      ++result_.workspace.tabs;
    } else if (data.Get("list")) {
      auto child_parent = parent;
      if (section == Section::Pinned) {
        node.title = CleanLabel(std::move(node.title), 240, "Folder");
        node.folder = true;
        node.expanded = expanded_.contains(id);
        child_parent = node.id;
        Add(std::move(node));
        ++result_.workspace.folders;
      }
      for (const auto& child : Children(id)) Visit(child, child_parent, space, profile, section, depth + 1);
    } else if (const auto* split = data.Get("splitView")) {
      ReadSplit(id, parent, space, profile, section, depth, *split, std::move(node.title));
    } else ++result_.skipped;
  }

  void ReadSplit(const std::string& id, const std::string& parent, const std::string& space,
                 const std::string& profile, Section section, int depth, const Json& split, std::string title) {
    auto& tree = result_.workspace.tree;
    const auto start = tree.nodes.size();
    for (const auto& child : Children(id)) {
      const auto item = items_.find(child);
      if (item == items_.end() || !Field(*item->second, "data").Get("tab")) { ++result_.skipped; continue; }
      Visit(child, parent, space, profile, section, depth);
    }
    const auto count = tree.nodes.size() - start;
    if (count < 2 || count > kMaxSplitPanes || section == Section::Favorites) return;
    const bool vertical = split.Str("layoutOrientation") == "vertical";
    SplitGroup group;
    group.anchor = tree.nodes[start].id;
    group.title = std::move(title);
    for (size_t i = start; i < tree.nodes.size(); ++i) group.pages.push_back(tree.nodes[i].id);
    group.layout = *SplitPreset(vertical ? "rows" : "columns", static_cast<int>(count));
    const auto& factors = Field(split, "itemWidthFactors").items;
    if (count == 2 && factors.size() == 2) {
      const auto first = Number(factors[0]), second = Number(factors[1]);
      if (first && second && *first > 0 && *second > 0) {
        const auto scale = std::max(*first, *second);
        const auto ratio = (*first / scale) / (*first / scale + *second / scale);
        group.layout = TwoPaneSplitLayout(vertical, std::clamp(static_cast<int>(std::lround(ratio * 10000)), 1000, 9000), false);
      }
    }
    tree.splits.push_back(std::move(group));
    ++result_.splits;
  }

  const std::string& source_;
  std::set<std::string> expanded_, visited_;
  std::map<std::string, const Json*> items_;
  std::map<std::string, std::vector<std::string>> by_parent_;
  std::map<std::string, int> positions_, favorites_;
  bool too_many_nodes_ = false;
  ArcSidebar result_;
};
}  // namespace

ArcSidebar ParseArcSidebar(std::string_view json, const std::string& source, std::string_view windows, std::string_view archive) {
  const auto file = ParseJson(json);
  if (file) if (const auto* sidebar = MainSidebar(*file); sidebar && Field(*sidebar, "spaces").IsArray())
    return SidebarReader(source, ExpandedFolders(windows)).Read(*sidebar, archive);
  ArcSidebar result;
  result.workspace.error = "This file does not contain a supported Arc sidebar.";
  return result;
}
}  // namespace red::importer
