#include "sync/sync_tree.h"
#include "import/formats/json_lite.h"
#include "sync/sync_crypto.h"
#include "web_url.h"
#include "workspace/split_layout.h"
#include <algorithm>
#include <climits>
#include <iterator>
#include <map>
#include <set>

namespace red::sync {
namespace {
using importer::Json;
constexpr std::string_view kProfile = "profile/", kSpace = "space/", kNode = "node/", kSplit = "split/";
constexpr int kMaxFavorites = 12, kMaxProfiles = 24, kMaxNodes = 10000;

struct NodeRecord {
  TreeNode node;     // id, parent, title, url, folder, custom_title; favorite/profile for a Favorite.
  std::string space;  // Space of the entry's top-level Pinned ancestor; empty for Favorites.
  bool favorite = false;
  int64_t order = 0;
};

const TreeNode* FindNode(const TreeSnapshot& tree, const std::string& id) {
  const auto it = std::find_if(tree.nodes.begin(), tree.nodes.end(), [&](const auto& n) { return n.id == id; });
  return it == tree.nodes.end() ? nullptr : &*it;
}
const TreeNode* RootOf(const TreeSnapshot& tree, const TreeNode& node) {
  const TreeNode* current = &node;
  for (int depth = 0; current && !current->parent.empty() && depth < 64; ++depth) current = FindNode(tree, current->parent);
  return current;
}
bool Synced(const TreeNode& n) { return !n.archived && (n.pinned || n.favorite); }

void Field(std::string& out, const char* name, std::string_view value) {
  out += out.size() > 1 ? ",\"" : "\""; out += name; out += "\":"; AppendJsonString(out, value);
}
void NumberField(std::string& out, const char* name, int64_t value) {
  out += out.size() > 1 ? ",\"" : "\""; out += name; out += "\":"; out += std::to_string(value);
}
void FlagField(std::string& out, const char* name, bool value) {
  out += out.size() > 1 ? ",\"" : "\""; out += name; out += "\":"; out += value ? "true" : "false";
}
bool Flag(const Json& json, std::string_view key, bool& out) {
  const auto* value = json.Get(key);
  if (!value || value->type != Json::Type::Bool) return false;
  out = value->boolean; return true;
}
bool Text(const Json& json, std::string_view key, std::string& out) {
  const auto* value = json.Get(key);
  if (!value || value->type != Json::Type::String) return false;
  out = value->text; return true;
}
bool Number(const Json& json, std::string_view key, int64_t low, int64_t high, int64_t& out) {
  const auto* value = json.Get(key);
  if (!value || value->type != Json::Type::Number) return false;
  out = json.Int(key, low - 1);
  return out >= low && out <= high;
}
bool Number(const Json& json, std::string_view key, int64_t low, int64_t high, int& out) {
  int64_t wide = 0;
  if (!Number(json, key, low, high, wide)) return false;
  out = static_cast<int>(wide); return true;
}

bool ParseSpace(const std::string& id, std::string_view value, Space& out) {
  const auto json = importer::ParseJson(value);
  if (!json || !json->IsObject()) return false;
  Space s; s.id = id;
  std::string palette;
  if (!Text(*json, "name", s.name) || !Text(*json, "icon", s.icon) || !Number(*json, "position", 0, 1000000, s.position) ||
      !Number(*json, "hue", 0, 359, s.theme_hue) || !Number(*json, "intensity", 0, 160, s.theme_intensity) ||
      !Number(*json, "mode", 0, 2, s.theme_mode) || !Number(*json, "noise", 0, 100, s.theme_noise) ||
      !Text(*json, "palette", palette) || !DecodePalette(palette, s.theme_palette) || !Text(*json, "profile", s.profile) ||
      !ValidSpaceFields(s)) return false;
  out = std::move(s); return true;
}

bool ParseNode(const std::string& id, std::string_view value, NodeRecord& out) {
  const auto json = importer::ParseJson(value);
  if (!json || !json->IsObject()) return false;
  NodeRecord r; r.node.id = id; r.node.pinned = true;
  std::string section;
  if (!Text(*json, "parent", r.node.parent) || !Text(*json, "section", section) || !Text(*json, "space", r.space) ||
      !Text(*json, "profile", r.node.profile) || !Flag(*json, "folder", r.node.folder) || !Text(*json, "title", r.node.title) ||
      !Flag(*json, "custom_title", r.node.custom_title) || !Text(*json, "url", r.node.url) || !Number(*json, "order", 0, kMaxNodes, r.order))
    return false;
  if (section != "pinned" && section != "favorite") return false;
  r.favorite = section == "favorite";
  // A Favorite is a top-level page owned by a profile; a Pinned entry names its Space.
  if (r.favorite ? (!r.node.parent.empty() || r.node.folder || !r.space.empty()) : (r.space.size() != 36 || !r.node.profile.empty())) return false;
  if (r.node.parent == id || (!r.node.parent.empty() && r.node.parent.size() != 36)) return false;
  if (!r.favorite) r.node.profile = "default";
  if (!ValidNodeFields(r.node)) return false;
  out = std::move(r); return true;
}

bool SameTree(const TreeSnapshot& a, const TreeSnapshot& b) {
  auto sorted = [](std::vector<TreeNode> nodes) {
    std::sort(nodes.begin(), nodes.end(), [](const auto& x, const auto& y) { return x.id < y.id; });
    return nodes;
  };
  auto groups = [](std::vector<SplitGroup> list) {
    std::sort(list.begin(), list.end(), [](const auto& x, const auto& y) { return x.anchor < y.anchor; });
    return list;
  };
  return sorted(a.nodes) == sorted(b.nodes) && a.spaces == b.spaces && a.profiles == b.profiles && groups(a.splits) == groups(b.splits) &&
         a.archived_splits == b.archived_splits && a.active_id == b.active_id && a.active_space == b.active_space;
}
// A split whose pages are all Pinned or Favorites syncs as one record; a
// split with a Today page stays on this PC.
bool SyncedGroup(const TreeSnapshot& tree, const SplitGroup& group) {
  return std::all_of(group.pages.begin(), group.pages.end(), [&](const auto& id) { const auto* n = FindNode(tree, id); return n && Synced(*n); });
}
bool ParseSplit(const std::string& anchor, std::string_view value, SplitGroup& out) {
  const auto json = importer::ParseJson(value);
  const auto* pages = json && json->IsObject() ? json->Get("pages") : nullptr;
  if (!pages || !pages->IsArray() || pages->items.size() < 2 || pages->items.size() > static_cast<size_t>(kMaxSplitPanes)) return false;
  SplitGroup group; group.anchor = anchor;
  std::string layout;
  for (const auto& page : pages->items) {
    if (page.type != Json::Type::String || page.text.size() != 36 ||
        std::find(group.pages.begin(), group.pages.end(), page.text) != group.pages.end()) return false;
    group.pages.push_back(page.text);
  }
  if (anchor.size() != 36 || !group.Contains(anchor) || !Text(*json, "title", group.title) || group.title.size() > 240 ||
      !Text(*json, "layout", layout) || !Flag(*json, "linked", group.linked)) return false;
  auto parsed = ParseSplitLayout(layout, static_cast<int>(group.pages.size()));
  if (!parsed) return false;
  group.layout = std::move(*parsed);
  out = std::move(group); return true;
}
bool GroupMembersExist(const SplitGroup& group, const std::set<std::string>& ids) {
  return std::all_of(group.pages.begin(), group.pages.end(), [&](const auto& id) { return ids.contains(id); });
}
}  // namespace

bool IsTreeKey(std::string_view key) {
  return key.starts_with(kProfile) || key.starts_with(kSpace) || key.starts_with(kNode) || key.starts_with(kSplit);
}

namespace {
constexpr std::string_view kIcon = "icon/";
constexpr size_t kMaxIconBytes = 16 * 1024, kMaxIcons = 500;
bool IsPng(std::string_view png) {
  return png.size() > 8 && png.size() <= kMaxIconBytes && png.starts_with(std::string_view("\x89PNG\r\n\x1a\n", 8));
}
}  // namespace

bool IsIconKey(std::string_view key) { return key.starts_with(kIcon); }
std::string IconKeyHost(std::string_view key) {
  if (!IsIconKey(key)) return "";
  const auto host = key.substr(kIcon.size());
  return SyncedIconHost("https://" + std::string(host) + "/") == host ? std::string(host) : "";
}
std::string SyncedIconHost(std::string_view url) {
  const auto scheme = WebSchemeLength(url);
  if (!scheme) return "";
  auto host = url.substr(scheme);
  host = host.substr(0, host.find_first_of("/?#"));
  if (host.find('@') != std::string_view::npos) return "";
  if (const auto port = host.rfind(':'); port != std::string_view::npos) host = host.substr(0, port);
  if (host.empty() || host.size() > 253) return "";
  std::string out;
  for (const char c : host) {
    const char lower = c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
    if (!((lower >= 'a' && lower <= 'z') || (lower >= '0' && lower <= '9') || lower == '.' || lower == '-')) return "";
    out.push_back(lower);
  }
  return out;
}
std::map<std::string, std::set<std::string>> SyncedIconHosts(const TreeSnapshot& tree) {
  std::map<std::string, std::set<std::string>> hosts;
  for (const auto& node : tree.nodes) {
    if (node.folder || node.archived || !(node.pinned || node.favorite)) continue;
    if (auto host = SyncedIconHost(node.url); !host.empty() && (hosts.size() < kMaxIcons || hosts.contains(host)))
      hosts[host].insert(BrowsingProfileId(tree, node.id));
  }
  return hosts;
}
void AddIconValues(const std::map<std::string, std::set<std::string>>& hosts, const std::map<std::string, std::string>& pngs,
                   const Values& base, Values& out) {
  for (const auto& [host, profiles] : hosts) {
    const auto key = std::string(kIcon) + host;
    if (const auto known = base.find(key); known != base.end()) { out.emplace(key, known->second); continue; }
    if (const auto png = pngs.find(host); png != pngs.end() && IsPng(png->second)) out.emplace(key, Base64Encode(png->second));
  }
}
bool ParseIconValue(std::string_view value, std::string& png) {
  return value.size() <= kMaxIconBytes * 2 && Base64Decode(value, png) && IsPng(png);
}

Values TreeValues(const TreeSnapshot& tree) {
  Values values;
  for (const auto& p : tree.profiles) {
    std::string v = "{"; Field(v, "name", p.name); v += "}";
    values.emplace(std::string(kProfile) + p.id, std::move(v));
  }
  for (const auto& s : tree.spaces) {
    std::string v = "{";
    Field(v, "name", s.name); Field(v, "icon", s.icon); NumberField(v, "position", static_cast<int64_t>(s.position));
    NumberField(v, "hue", static_cast<int64_t>(s.theme_hue)); NumberField(v, "intensity", static_cast<int64_t>(s.theme_intensity));
    NumberField(v, "mode", static_cast<int64_t>(s.theme_mode)); NumberField(v, "noise", static_cast<int64_t>(s.theme_noise));
    Field(v, "palette", EncodePalette(s.theme_palette)); Field(v, "profile", s.profile);
    v += "}";
    values.emplace(std::string(kSpace) + s.id, std::move(v));
  }
  // Order is the index among the entries shown together: one folder's
  // children, one Space's Pinned roots, one profile's Favorites.
  std::map<std::string, std::vector<const TreeNode*>> groups;
  for (const auto& n : tree.nodes) {
    if (!Synced(n)) continue;
    groups[!n.parent.empty() ? "c|" + n.parent : n.favorite ? "f|" + n.profile : "p|" + n.space].push_back(&n);
  }
  for (auto& [group, members] : groups) {
    std::sort(members.begin(), members.end(), [](auto a, auto b) { return a->position != b->position ? a->position < b->position : a->id < b->id; });
    for (size_t i = 0; i < members.size(); ++i) {
      const auto& n = *members[i];
      const auto* root = RootOf(tree, n);
      const bool favorite = n.parent.empty() && n.favorite;
      std::string v = "{";
      Field(v, "parent", n.parent); Field(v, "section", favorite ? "favorite" : "pinned");
      Field(v, "space", favorite || !root ? "" : root->space); Field(v, "profile", favorite ? n.profile : "");
      FlagField(v, "folder", n.folder); Field(v, "title", n.title); FlagField(v, "custom_title", n.custom_title);
      Field(v, "url", n.url); NumberField(v, "order", static_cast<int64_t>(i));
      v += "}";
      values.emplace(std::string(kNode) + n.id, std::move(v));
    }
  }
  for (const auto& group : tree.splits) {
    if (!SyncedGroup(tree, group)) continue;
    std::string v = "{\"pages\":[";
    for (size_t i = 0; i < group.pages.size(); ++i) { if (i) v += ","; AppendJsonString(v, group.pages[i]); }
    v += "]";
    Field(v, "title", group.title); Field(v, "layout", FormatSplitLayout(group.layout)); FlagField(v, "linked", group.linked);
    v += "}";
    values.emplace(std::string(kSplit) + group.anchor, std::move(v));
  }
  return values;
}

TreeApply ApplyTreeValues(const TreeSnapshot& local, const Values& values) {
  TreeApply out;
  std::map<std::string, std::string> profile_names;
  std::map<std::string, Space> spaces;
  std::map<std::string, NodeRecord> records;
  std::vector<SplitGroup> synced_splits;  // In anchor order (|values| is sorted).
  for (const auto& [key, value] : values) {
    if (key.starts_with(kProfile)) {
      const auto id = key.substr(kProfile.size());
      const auto json = importer::ParseJson(value);
      std::string name;
      if (ValidBrowsingProfileId(id) && json && json->IsObject() && Text(*json, "name", name) && ValidProfileName(name)) profile_names[id] = name;
    } else if (key.starts_with(kSpace)) {
      Space space;
      if (ParseSpace(key.substr(kSpace.size()), value, space)) spaces.emplace(space.id, std::move(space));
    } else if (key.starts_with(kNode)) {
      NodeRecord record;
      if (ParseNode(key.substr(kNode.size()), value, record)) records.emplace(record.node.id, std::move(record));
    } else if (key.starts_with(kSplit)) {
      SplitGroup group;
      if (ParseSplit(key.substr(kSplit.size()), value, group)) synced_splits.push_back(std::move(group));
    }
  }
  if (spaces.empty()) return out;  // A synced state always has a Space; refuse to empty the sidebar.

  TreeSnapshot tree = local;
  // Profiles: renamed or added, never removed (Sol has no profile deletion).
  for (const auto& [id, name] : profile_names) {
    const auto found = std::find_if(tree.profiles.begin(), tree.profiles.end(), [&](const auto& p) { return p.id == id; });
    if (found != tree.profiles.end()) found->name = name;
    else if (static_cast<int>(tree.profiles.size()) < kMaxProfiles) tree.profiles.push_back({id, name});
  }
  std::set<std::string> profile_ids;
  for (const auto& p : tree.profiles) profile_ids.insert(p.id);

  // Spaces: exactly the synced ones, in synced order; this PC keeps its last page per Space.
  std::vector<Space> ordered;
  for (auto& [id, space] : spaces) {
    if (!profile_ids.contains(space.profile)) continue;
    if (const auto it = std::find_if(local.spaces.begin(), local.spaces.end(), [&](const auto& s) { return s.id == id; }); it != local.spaces.end())
      space.active_id = it->active_id;
    ordered.push_back(space);
  }
  if (ordered.empty()) return out;
  std::sort(ordered.begin(), ordered.end(), [](const auto& a, const auto& b) { return a.position != b.position ? a.position < b.position : a.id < b.id; });
  if (ordered.size() > static_cast<size_t>(kMaxSpaces)) ordered.resize(kMaxSpaces);
  for (size_t i = 0; i < ordered.size(); ++i) ordered[i].position = static_cast<int>(i);
  tree.spaces = ordered;
  std::set<std::string> space_ids;
  for (const auto& s : tree.spaces) space_ids.insert(s.id);
  if (!space_ids.contains(tree.active_space)) tree.active_space = tree.spaces.front().id;

  // Synced entries: repair until stable, in id order so every PC repairs alike.
  bool changed = true;
  for (int pass = 0; changed && pass < 128; ++pass) {
    changed = false;
    for (auto it = records.begin(); it != records.end();) {
      auto& r = it->second;
      bool drop = false;
      if (r.favorite) drop = !profile_ids.contains(r.node.profile);
      else if (r.node.parent.empty()) drop = !space_ids.contains(r.space);
      else if (const auto parent = records.find(r.node.parent); parent == records.end() || !parent->second.node.folder || parent->second.favorite) {
        if (space_ids.contains(r.space)) { r.node.parent.clear(); r.order = kMaxNodes; }  // Its folder is gone: end of the Space's Pinned.
        else drop = true;
        changed = true;
      }
      if (drop) { it = records.erase(it); changed = true; }
      else ++it;
    }
  }
  // Cycles (two PCs moved folders into each other): detach the entry that closes the loop.
  for (auto& [id, r] : records) {
    std::set<std::string> seen{id};
    for (auto parent = r.node.parent; !parent.empty();) {
      if (!seen.insert(parent).second || seen.size() > 64) {
        r.node.parent.clear(); r.order = kMaxNodes;
        if (!space_ids.contains(r.space)) r.space = tree.spaces.front().id;
        break;
      }
      const auto found = records.find(parent);
      parent = found == records.end() ? "" : found->second.node.parent;
    }
  }
  // Root of a nested entry decides its Space (Space/section of a nested record is a hint only).
  for (auto& [id, r] : records) {
    std::string root = id;
    for (int depth = 0; depth < 64 && !records.at(root).node.parent.empty(); ++depth) root = records.at(root).node.parent;
    if (root != id) { r.space = records.at(root).space; r.favorite = false; }
  }
  // Favorites beyond the limit become Pinned in the first Space of their profile.
  std::map<std::string, std::vector<NodeRecord*>> favorites;
  for (auto& [id, r] : records) if (r.favorite) favorites[r.node.profile].push_back(&r);
  for (auto& [profile, list] : favorites) {
    std::sort(list.begin(), list.end(), [](auto a, auto b) { return a->order != b->order ? a->order < b->order : a->node.id < b->node.id; });
    for (size_t i = kMaxFavorites; i < list.size(); ++i) {
      const auto space = std::find_if(tree.spaces.begin(), tree.spaces.end(), [&](const auto& s) { return s.profile == profile; });
      if (space == tree.spaces.end()) { list[i]->node.id.clear(); continue; }
      list[i]->favorite = false; list[i]->space = space->id; list[i]->order = kMaxNodes + static_cast<int64_t>(i);
    }
  }
  std::erase_if(records, [](const auto& item) { return item.second.node.id.empty(); });

  // Rebuild nodes: synced entries from records (keeping this PC's activity
  // and expansion), then local Today and archived tabs whose Space remains.
  std::vector<TreeNode> nodes;
  for (auto& [id, r] : records) {
    TreeNode n = r.node;
    n.pinned = true; n.favorite = r.favorite && n.parent.empty(); n.archived = false;
    n.space = n.parent.empty() && !n.favorite ? r.space : "";
    if (!n.favorite) n.profile = "default";
    if (const auto* old = FindNode(local, id)) {
      n.expanded = old->expanded; n.last_active_at = old->last_active_at; n.closed_at = old->closed_at;
      n.x_badge_hidden = old->x_badge_hidden;
      if (old->url == n.url) n.page_title = old->page_title;
      if (!n.favorite && old->profile != n.profile && profile_ids.contains(old->profile)) n.profile = old->profile;
    }
    nodes.push_back(std::move(n));
  }
  std::vector<TreeNode> locals;
  for (const auto& n : local.nodes) if (!Synced(n) && n.parent.empty() && space_ids.contains(n.space) && !records.contains(n.id)) locals.push_back(n);
  std::sort(locals.begin(), locals.end(), [](const auto& a, const auto& b) { return a.position < b.position; });

  // Positions. Top-level positions are one namespace in the store; sections are
  // read in position order, so only the order inside each section matters.
  auto order_of = [&](const TreeNode& n) { return records.at(n.id).order; };
  auto by_order = [&](const TreeNode* a, const TreeNode* b) { return order_of(*a) != order_of(*b) ? order_of(*a) < order_of(*b) : a->id < b->id; };
  // Keep this PC's interleaving of sections: every root keeps its old slot
  // (new ones go last), then each synced section's slots are refilled in synced order.
  std::vector<TreeNode*> roots;
  for (auto& n : nodes) if (n.parent.empty()) roots.push_back(&n);
  for (auto& n : locals) roots.push_back(&n);
  auto section = [&](const TreeNode& n) { return records.contains(n.id) ? (n.favorite ? "f|" + n.profile : "p|" + n.space) : "l|" + n.id; };
  auto old_slot = [&](const TreeNode& n) {
    const auto* old = FindNode(local, n.id);
    return old && old->parent.empty() ? static_cast<int64_t>(old->position) : INT64_MAX;
  };
  std::sort(roots.begin(), roots.end(), [&](const TreeNode* a, const TreeNode* b) {
    const auto sa = old_slot(*a), sb = old_slot(*b);
    if (sa != sb) return sa < sb;
    const auto ka = section(*a), kb = section(*b);
    if (ka != kb) return ka < kb;
    return records.contains(a->id) && records.contains(b->id) ? by_order(a, b) : a->id < b->id;
  });
  std::map<std::string, std::vector<size_t>> slots;
  std::map<std::string, std::vector<TreeNode*>> members;
  for (size_t i = 0; i < roots.size(); ++i) { const auto key = section(*roots[i]); slots[key].push_back(i); members[key].push_back(roots[i]); }
  std::vector<TreeNode*> placed(roots.size());
  for (auto& [key, list] : members) {
    if (key.starts_with("f|") || key.starts_with("p|")) std::sort(list.begin(), list.end(), by_order);
    for (size_t k = 0; k < list.size(); ++k) placed[slots[key][k]] = list[k];
  }
  for (size_t i = 0; i < placed.size(); ++i) placed[i]->position = static_cast<int>(i);
  std::map<std::string, std::vector<TreeNode*>> children;
  for (auto& n : nodes) if (!n.parent.empty()) children[n.parent].push_back(&n);
  for (auto& [parent, list] : children) {
    std::sort(list.begin(), list.end(), by_order);
    for (size_t i = 0; i < list.size(); ++i) list[i]->position = static_cast<int>(i);
  }
  nodes.insert(nodes.end(), locals.begin(), locals.end());
  if (nodes.size() > static_cast<size_t>(kMaxNodes)) return out;
  tree.nodes = std::move(nodes);

  std::set<std::string> ids;
  for (const auto& n : tree.nodes) ids.insert(n.id);
  // Splits of synced pages come from the records; this PC keeps its splits
  // that include a Today page. Synced splits must consist of synced pages.
  std::erase_if(tree.splits, [&](const auto& g) {
    return !GroupMembersExist(g, ids) || std::all_of(g.pages.begin(), g.pages.end(), [&](const auto& id) { return records.contains(id); });
  });
  // Synced splits go first, so when a page is in both, every PC keeps the synced one.
  std::erase_if(synced_splits, [&](const auto& g) { return !std::all_of(g.pages.begin(), g.pages.end(), [&](const auto& id) { return records.contains(id); }); });
  tree.splits.insert(tree.splits.begin(), std::make_move_iterator(synced_splits.begin()), std::make_move_iterator(synced_splits.end()));
  std::erase_if(tree.archived_splits, [&](const auto& g) { return !GroupMembersExist(g, ids); });
  if (!ids.contains(tree.active_id)) tree.active_id.clear();
  for (auto& s : tree.spaces) if (!ids.contains(s.active_id)) s.active_id.clear();
  if (!TreeStore::ValidateSnapshot(tree)) {
    // The page on screen moved to another Space, or a split's pages now
    // differ in Space or profile: drop exactly those, keep the other splits.
    tree.active_id.clear();
    if (!TreeStore::ValidateSnapshot(tree)) {
      const auto live = std::move(tree.splits), archived = std::move(tree.archived_splits);
      tree.splits.clear(); tree.archived_splits.clear();
      for (const auto& group : live) {
        tree.splits.push_back(group);
        if (!TreeStore::ValidateSnapshot(tree)) tree.splits.pop_back();
      }
      for (const auto& group : archived) {
        tree.archived_splits.push_back(group);
        if (!TreeStore::ValidateSnapshot(tree)) tree.archived_splits.pop_back();
      }
      if (!TreeStore::ValidateSnapshot(tree)) return out;
    }
  }
  out.ok = true;
  out.changed = !SameTree(tree, local);
  out.tree = std::move(tree);
  return out;
}
}  // namespace red::sync
