#include "storage/tree_store.h"
#include <windows.h>
#include <bcrypt.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <map>
#include <set>
#include <sqlite3.h>
#include "trust_policy.h"
#include "workspace/tab_order.h"

namespace red {
namespace {
constexpr int kApplicationId = 0x52454442;
TreeResult Error(int code, const char* message) { TreeResult r; r.code = code; r.error = message; return r; }
class Statement {
 public:
  Statement(sqlite3* db, const char* sql) { sqlite3_prepare_v2(db, sql, -1, &value, nullptr); }
  ~Statement() { sqlite3_finalize(value); }
  void Finish() { sqlite3_finalize(value); value = nullptr; }
  sqlite3_stmt* value = nullptr;
  void Text(int i, const std::string& text) { sqlite3_bind_text(value, i, text.data(), static_cast<int>(text.size()), SQLITE_TRANSIENT); }
};
std::string Text(sqlite3_stmt* statement, int column) {
  const auto* value = sqlite3_column_text(statement, column);
  return value ? std::string(reinterpret_cast<const char*>(value), sqlite3_column_bytes(statement, column)) : "";
}
bool SafeText(const std::string& value, size_t maximum, bool allow_empty = false) {
  return (allow_empty || value.find_first_not_of(" \t\r\n") != std::string::npos) && value.size() <= maximum &&
      std::none_of(value.begin(), value.end(), [](unsigned char c) { return c < 32 || c == 127; });
}
bool SafeUrl(const std::string& url) {
  // Websites, extension pages, local files and exactly our own internal pages.
  return SafeText(url, 4096) && (url.starts_with("https://") || url.starts_with("http://") || IsExtensionPageUrl(url) ||
                                 IsInternalPageUrl(url) || IsLocalFileUrl(url));
}
std::string NewId() {
  std::array<unsigned char, 16> bytes{};
  if (BCryptGenRandom(nullptr, bytes.data(), static_cast<ULONG>(bytes.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) return "";
  bytes[6] = (bytes[6] & 15) | 64; bytes[8] = (bytes[8] & 63) | 128;
  constexpr char hex[] = "0123456789abcdef";
  std::string result;
  for (size_t i = 0; i < bytes.size(); ++i) {
    if (i == 4 || i == 6 || i == 8 || i == 10) result += '-';
    result += hex[bytes[i] >> 4]; result += hex[bytes[i] & 15];
  }
  return result;
}
TreeNode* Find(TreeSnapshot& tree, const std::string& id) {
  auto it = std::find_if(tree.nodes.begin(), tree.nodes.end(), [&](const auto& n) { return n.id == id; });
  return it == tree.nodes.end() ? nullptr : &*it;
}
const TreeNode* FindConst(const TreeSnapshot& tree, const std::string& id) {
  auto it = std::find_if(tree.nodes.begin(), tree.nodes.end(), [&](const auto& n) { return n.id == id; });
  return it == tree.nodes.end() ? nullptr : &*it;
}
Space* FindSpace(TreeSnapshot& tree, const std::string& id) {
  auto it = std::find_if(tree.spaces.begin(), tree.spaces.end(), [&](const auto& s) { return s.id == id; });
  return it == tree.spaces.end() ? nullptr : &*it;
}
// The space an entry belongs to: its top-level ancestor's; empty for Favorites.
std::string SpaceOf(const TreeSnapshot& tree, const TreeNode& node) {
  const TreeNode* current = &node;
  for (int depth = 0; current && !current->parent.empty() && depth < 64; ++depth) current = FindConst(tree, current->parent);
  return current && !current->favorite ? current->space : "";
}
// The page to show when entering |space|: the one last shown there, else its
// first Pinned/Today page, else none (an empty space). With |live|, only a page
// in it qualifies, so entering a space never loads a saved page by itself.
std::string SpaceLanding(const TreeSnapshot& tree, const Space& space, const std::optional<std::vector<std::string>>& live = std::nullopt) {
  const auto allowed = [&](const std::string& id) { return !live || std::find(live->begin(), live->end(), id) != live->end(); };
  if (const auto* last = FindConst(tree, space.active_id); last && !last->folder && !last->archived && allowed(last->id)) {
    const auto owner = SpaceOf(tree, *last);
    if (owner == space.id || owner.empty()) return last->id;
  }
  for (const auto& id : OrderedTabs(tree.nodes, space.id)) if (!FindConst(tree, id)->favorite && allowed(id)) return id;
  return "";
}
bool ValidSpaceTheme(const Space& s) {
  return s.theme_hue >= 0 && s.theme_hue <= 359 && s.theme_intensity >= 0 && s.theme_intensity <= 160 &&
         s.theme_mode >= 0 && s.theme_mode <= 2 && s.theme_noise >= 0 && s.theme_noise <= 100 &&
         (!s.theme_palette || ValidPalette(*s.theme_palette));
}
bool ValidIcon(const std::string& icon) {
  if(!SafeText(icon,128,true)||icon.find(' ')!=std::string::npos)return false;
  if(icon.empty())return true;
  if(!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,icon.data(),static_cast<int>(icon.size()),nullptr,0))return false;
  return std::count_if(icon.begin(),icon.end(),[](unsigned char c){return (c&0xc0)!=0x80;})<=16;
}
bool Valid(const TreeSnapshot& tree) {
  if (tree.nodes.size() > 10000 || tree.revision < 0 || tree.sidebar_width < 200 || tree.sidebar_width > 480) return false;
  if (tree.spaces.empty() || tree.spaces.size() > kMaxSpaces) return false;
  std::set<std::string> space_ids; std::set<int> space_positions;
  for (const auto& s : tree.spaces)
    if (s.id.size() != 36 || !SafeText(s.name, 40) || !ValidIcon(s.icon) || !ValidSpaceTheme(s) ||
        !space_ids.insert(s.id).second || !space_positions.insert(s.position).second) return false;
  if (*space_positions.rbegin() != static_cast<int>(tree.spaces.size()) - 1 || !space_ids.contains(tree.active_space)) return false;
  std::map<std::string, const TreeNode*> nodes;
  std::map<std::string, std::set<int>> positions;
  int favorites = 0;
  for (const auto& n : tree.nodes) {
    if (n.id.size() != 36 || !SafeText(n.title, 240) || !SafeText(n.page_title, 240, true) || n.position < 0 || n.last_active_at < 0 ||
        (!n.folder && !SafeUrl(n.url)) || (n.folder && !n.url.empty()) ||
        (n.folder && (!n.pinned || n.favorite || n.archived)) ||
        (!n.parent.empty() && (!n.pinned || n.favorite || n.archived)) ||
        (n.favorite && (!n.pinned || n.folder || n.archived)) ||
        (n.archived && (n.pinned || n.favorite || n.folder || !n.parent.empty())) ||
        !nodes.emplace(n.id, &n).second || !positions[n.parent].insert(n.position).second) return false;
    if (n.parent.empty() && !n.favorite ? !space_ids.contains(n.space) : !n.space.empty()) return false;
    if (n.favorite && ++favorites > 12) return false;
  }
  for (const auto& [parent, slots] : positions) {
    if (!slots.empty() && *slots.rbegin() != static_cast<int>(slots.size()) - 1) return false;
    if (!parent.empty() && (!nodes.contains(parent) || !nodes.at(parent)->folder)) return false;
  }
  for (const auto& n : tree.nodes) {
    std::set<std::string> ancestors;
    const TreeNode* current = &n;
    while (current) {
      if (!ancestors.insert(current->id).second || ancestors.size() > 64) return false;
      current = current->parent.empty() ? nullptr : nodes.at(current->parent);
    }
  }
  std::set<std::string> split_members;
  for (const auto& split : tree.splits) {
    const auto a = nodes.find(split.anchor), b = nodes.find(split.other);
    if (a == nodes.end() || b == nodes.end() || a->second->folder || b->second->folder ||
        a->second->archived || b->second->archived || a->second->favorite ||
        !SafeText(split.title,240,true) || split.ratio < 1000 || split.ratio > 9000 ||
        !split_members.insert(split.anchor).second || !split_members.insert(split.other).second ||
        (!b->second->favorite && SpaceOf(tree,*a->second) != SpaceOf(tree,*b->second))) return false;
  }
  for (const auto& split:tree.archived_splits) {
    const auto a=nodes.find(split.anchor),b=nodes.find(split.other);
    if (a==nodes.end() || b==nodes.end() || !a->second->archived || !b->second->archived ||
        !SafeText(split.title,240,true) || split.ratio<1000 || split.ratio>9000 ||
        !split_members.insert(split.anchor).second || !split_members.insert(split.other).second || a->second->space!=b->second->space) return false;
  }
  if (tree.active_id.empty()) return true;
  if (!nodes.contains(tree.active_id) || nodes.at(tree.active_id)->folder || nodes.at(tree.active_id)->archived) return false;
  // The page on screen belongs to the space on screen (or is a Favorite).
  const auto owner = SpaceOf(tree, *nodes.at(tree.active_id));
  return owner.empty() || owner == tree.active_space;
}
void Normalize(TreeSnapshot& tree, const std::string& parent) {
  std::vector<TreeNode*> siblings;
  for (auto& node : tree.nodes) if (node.parent == parent) siblings.push_back(&node);
  std::sort(siblings.begin(), siblings.end(), [](auto a, auto b) { return a->position < b->position; });
  for (size_t i = 0; i < siblings.size(); ++i) siblings[i]->position = static_cast<int>(i);
}
bool MoveNode(TreeSnapshot& tree, TreeNode& node, const std::string& parent_id, int position) {
  if (node.id == parent_id) return false;
  auto* parent = Find(tree, parent_id);
  if (!parent_id.empty() && (!parent || !parent->folder)) return false;
  for (auto* ancestor = parent; ancestor; ancestor = Find(tree, ancestor->parent))
    if (ancestor->id == node.id) return false;
  const auto old_parent = node.parent;
  node.parent = "__moving__"; Normalize(tree, old_parent);
  const auto count = static_cast<int>(std::count_if(tree.nodes.begin(), tree.nodes.end(), [&](const auto& n) { return n.parent == parent_id; }));
  const int target = position == -1 ? count : position;
  if (target < 0 || target > count) return false;
  for (auto& other : tree.nodes) if (other.parent == parent_id && other.position >= target) ++other.position;
  node.parent = parent_id; node.position = target;
  if (parent) { node.pinned = true; node.favorite = false; }
  return true;
}
}  // namespace

std::vector<std::string> FolderNodeIds(const TreeSnapshot& tree, const std::string& id) {
  const auto* root = FindConst(tree,id);
  if (!root || !root->folder || root->archived) return {};
  std::map<std::string,std::vector<std::string>> children;
  for (const auto& node : tree.nodes) children[node.parent].push_back(node.id);
  std::set<std::string> seen{id}; std::vector<std::string> pending{id};
  for (size_t i=0;i<pending.size();++i) for (const auto& child:children[pending[i]])
    if (seen.insert(child).second) pending.push_back(child);
  return {seen.begin(),seen.end()};
}

const Space* TreeSnapshot::ActiveSpace() const {
  auto it = std::find_if(spaces.begin(), spaces.end(), [&](const auto& s) { return s.id == active_space; });
  return it == spaces.end() ? nullptr : &*it;
}
TreeStore::~TreeStore() { if (db_) sqlite3_close(db_); }
bool TreeStore::ValidateSnapshot(const TreeSnapshot& snapshot) { return Valid(snapshot); }
bool TreeStore::Exec(const char* sql) { return sqlite3_exec(db_, sql, nullptr, nullptr, nullptr) == SQLITE_OK; }
TreeResult TreeStore::Open(const std::filesystem::path& path) {
  if (db_) return Error(500, "Store already open");
  const auto encoded = path.u8string();
  if (sqlite3_open_v2(reinterpret_cast<const char*>(encoded.c_str()), &db_,
      SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr) != SQLITE_OK)
    return Error(500, "Cannot open workspace database");
  sqlite3_busy_timeout(db_, 1500);
  if (!Exec("PRAGMA foreign_keys=ON; PRAGMA trusted_schema=OFF; PRAGMA synchronous=EXTRA;"))
    return Error(500, "Cannot configure workspace database");
  Statement version(db_, "PRAGMA user_version"), app(db_, "PRAGMA application_id");
  if (!version.value || !app.value || sqlite3_step(version.value) != SQLITE_ROW || sqlite3_step(app.value) != SQLITE_ROW)
    return Error(500, "Cannot inspect workspace schema");
  const int schema = sqlite3_column_int(version.value, 0), application = sqlite3_column_int(app.value, 0);
  version.Finish(); app.Finish();
  if (schema > 10 ||(application != 0 && application != kApplicationId)) return Error(500, "Unsupported workspace schema");
  if (schema == 0) {
    Statement tables(db_, "SELECT count(*) FROM sqlite_schema WHERE name NOT LIKE 'sqlite_%'");
    if (!tables.value || sqlite3_step(tables.value) != SQLITE_ROW || sqlite3_column_int(tables.value, 0) != 0)
      return Error(500, "Refusing to adopt an unrelated database");
    tables.Finish();
    if (!Exec("BEGIN IMMEDIATE; CREATE TABLE nodes(id TEXT PRIMARY KEY, parent_id TEXT REFERENCES nodes(id) DEFERRABLE INITIALLY DEFERRED,"
        " kind INTEGER NOT NULL CHECK(kind IN(0,1)),title TEXT NOT NULL,url TEXT NOT NULL,position INTEGER NOT NULL CHECK(position>=0),"
        " pinned INTEGER NOT NULL CHECK(pinned IN(0,1)),expanded INTEGER NOT NULL CHECK(expanded IN(0,1)));"
        "CREATE TABLE state(singleton INTEGER PRIMARY KEY CHECK(singleton=1),revision INTEGER NOT NULL,active_id TEXT REFERENCES nodes(id) DEFERRABLE INITIALLY DEFERRED);"
        "INSERT INTO state VALUES(1,0,NULL); PRAGMA application_id=1380271170; PRAGMA user_version=1; COMMIT;")) {
      Exec("ROLLBACK"); return Error(500, "Workspace migration failed");
    }
  } else if (application != kApplicationId) return Error(500, "Workspace identity mismatch");
  if (schema < 2 && !Exec("BEGIN IMMEDIATE; ALTER TABLE nodes ADD COLUMN favorite INTEGER NOT NULL DEFAULT 0 CHECK(favorite IN(0,1));"
      "ALTER TABLE nodes ADD COLUMN archived INTEGER NOT NULL DEFAULT 0 CHECK(archived IN(0,1));"
      "UPDATE nodes SET pinned=1 WHERE kind=1 OR parent_id IS NOT NULL; PRAGMA user_version=2; COMMIT;")) {
    Exec("ROLLBACK"); return Error(500, "Sidebar migration failed");
  }
  // v2 had no distinction between automatic and user names. Preserve existing
  // pinned names; new tabs (including future pins) follow their page by default.
  if (schema < 3 && !Exec("BEGIN IMMEDIATE; ALTER TABLE nodes ADD COLUMN page_title TEXT NOT NULL DEFAULT '';"
      "ALTER TABLE nodes ADD COLUMN custom_title INTEGER NOT NULL DEFAULT 0 CHECK(custom_title IN(0,1));"
      "UPDATE nodes SET custom_title=1 WHERE kind=0 AND pinned=1;"
      "ALTER TABLE state ADD COLUMN sidebar_width INTEGER NOT NULL DEFAULT 256 CHECK(sidebar_width BETWEEN 200 AND 480);"
      "PRAGMA user_version=3; COMMIT;")) {
    Exec("ROLLBACK"); return Error(500, "Page metadata migration failed");
  }
  // v4: when a tab was archived, so the Archive lists newest first. Entries
  // archived earlier keep 0 (unknown) and sort after dated ones.
  if (schema < 4 && !Exec("BEGIN IMMEDIATE; ALTER TABLE nodes ADD COLUMN archived_at INTEGER NOT NULL DEFAULT 0 CHECK(archived_at>=0);"
      "PRAGMA user_version=4; COMMIT;")) {
    Exec("ROLLBACK"); return Error(500, "Archive time migration failed");
  }
  // v5: the shell theme (hue, chroma percent, light/dark/system, grain).
  if (schema < 5 && !Exec("BEGIN IMMEDIATE;"
      "ALTER TABLE state ADD COLUMN theme_hue INTEGER NOT NULL DEFAULT 307 CHECK(theme_hue BETWEEN 0 AND 359);"
      "ALTER TABLE state ADD COLUMN theme_intensity INTEGER NOT NULL DEFAULT 100 CHECK(theme_intensity BETWEEN 0 AND 160);"
      "ALTER TABLE state ADD COLUMN theme_mode INTEGER NOT NULL DEFAULT 0 CHECK(theme_mode IN(0,1,2));"
      "ALTER TABLE state ADD COLUMN theme_noise INTEGER NOT NULL DEFAULT 0 CHECK(theme_noise BETWEEN 0 AND 100);"
      "PRAGMA user_version=5; COMMIT;")) {
    Exec("ROLLBACK"); return Error(500, "Theme migration failed");
  }
  // v6: Spaces. Everything so far becomes the first space ("Personal", with
  // the v5 theme); Favorites stay shared by every space.
  if (schema < 6) {
    const auto id = NewId();
    if (id.empty()) return Error(500, "Cannot generate space identity");
    bool ok = Exec("BEGIN IMMEDIATE;"
        "CREATE TABLE spaces(id TEXT PRIMARY KEY CHECK(length(id)=36),name TEXT NOT NULL,icon TEXT NOT NULL DEFAULT '',"
        " position INTEGER NOT NULL CHECK(position>=0),active_id TEXT,"
        " theme_hue INTEGER NOT NULL DEFAULT 307 CHECK(theme_hue BETWEEN 0 AND 359),"
        " theme_intensity INTEGER NOT NULL DEFAULT 100 CHECK(theme_intensity BETWEEN 0 AND 160),"
        " theme_mode INTEGER NOT NULL DEFAULT 0 CHECK(theme_mode IN(0,1,2)),"
        " theme_noise INTEGER NOT NULL DEFAULT 0 CHECK(theme_noise BETWEEN 0 AND 100));"
        "ALTER TABLE nodes ADD COLUMN space_id TEXT NOT NULL DEFAULT '';"
        "ALTER TABLE state ADD COLUMN active_space TEXT NOT NULL DEFAULT '';");
    auto run = [&](const char* sql) {
      Statement statement(db_, sql);
      if (!statement.value) return false;
      statement.Text(1, id);
      return sqlite3_step(statement.value) == SQLITE_DONE;
    };
    ok = ok && run("INSERT INTO spaces SELECT ?,'Personal','',0,active_id,theme_hue,theme_intensity,theme_mode,theme_noise FROM state WHERE singleton=1") &&
         run("UPDATE nodes SET space_id=? WHERE parent_id IS NULL AND favorite=0") &&
         run("UPDATE state SET active_space=? WHERE singleton=1") && Exec("PRAGMA user_version=6; COMMIT;");
    if (!ok) { Exec("ROLLBACK"); return Error(500, "Spaces migration failed"); }
  }
  if (schema < 7 && !Exec("BEGIN IMMEDIATE; CREATE TABLE splits("
      "anchor TEXT PRIMARY KEY REFERENCES nodes(id) DEFERRABLE INITIALLY DEFERRED,"
      "other TEXT NOT NULL UNIQUE REFERENCES nodes(id) DEFERRABLE INITIALLY DEFERRED,"
      "title TEXT NOT NULL DEFAULT '',ratio INTEGER NOT NULL CHECK(ratio BETWEEN 1000 AND 9000),"
      "vertical INTEGER NOT NULL CHECK(vertical IN(0,1)),reversed INTEGER NOT NULL CHECK(reversed IN(0,1)));"
      "PRAGMA user_version=7; COMMIT;")) {
    Exec("ROLLBACK"); return Error(500,"Split View migration failed");
  }
  if (schema < 8 && !Exec("BEGIN IMMEDIATE; ALTER TABLE nodes ADD COLUMN last_active_at INTEGER NOT NULL DEFAULT 0 CHECK(last_active_at>=0);"
      "UPDATE nodes SET last_active_at=CAST(strftime('%s','now') AS INTEGER)*1000;"
      "CREATE TABLE archived_splits(anchor TEXT PRIMARY KEY REFERENCES nodes(id) DEFERRABLE INITIALLY DEFERRED,"
      "other TEXT NOT NULL UNIQUE REFERENCES nodes(id) DEFERRABLE INITIALLY DEFERRED,title TEXT NOT NULL,"
      "ratio INTEGER NOT NULL CHECK(ratio BETWEEN 1000 AND 9000),vertical INTEGER NOT NULL CHECK(vertical IN(0,1)),reversed INTEGER NOT NULL CHECK(reversed IN(0,1)));"
      "PRAGMA user_version=8; COMMIT;")) { Exec("ROLLBACK"); return Error(500,"Activity migration failed"); }
  // Empty means legacy rendering. Only an explicit edit creates a v1 palette.
  if (schema < 9 && !Exec("BEGIN IMMEDIATE; ALTER TABLE spaces ADD COLUMN theme_palette TEXT NOT NULL DEFAULT '' CHECK(length(theme_palette)<=64);"
      "PRAGMA user_version=9; COMMIT;")) { Exec("ROLLBACK"); return Error(500,"Palette migration failed"); }
  // v10: Red became Sol; its own pages moved from red:// to sol://.
  if (schema < 10 && !Exec("BEGIN IMMEDIATE; UPDATE nodes SET url='sol://'||substr(url,7) WHERE url LIKE 'red://%';"
      "PRAGMA user_version=10; COMMIT;")) { Exec("ROLLBACK"); return Error(500,"Sol address migration failed"); }
  if (!Exec("PRAGMA journal_mode=WAL; PRAGMA synchronous=FULL;")) return Error(500, "Cannot configure workspace journal");
  Statement check(db_, "PRAGMA quick_check(1)");
  if (!check.value || sqlite3_step(check.value) != SQLITE_ROW || Text(check.value, 0) != "ok")
    return Error(500, "Workspace integrity check failed");
  return Read();
}
TreeResult TreeStore::Load() {
  TreeResult result;
  Statement state(db_, "SELECT revision,active_id,sidebar_width,active_space FROM state WHERE singleton=1");
  if (!state.value || sqlite3_step(state.value) != SQLITE_ROW) return Error(500, "Workspace state unavailable");
  result.snapshot.revision = sqlite3_column_int64(state.value, 0);
  result.snapshot.active_id = Text(state.value, 1);
  result.snapshot.sidebar_width = sqlite3_column_int(state.value, 2);
  result.snapshot.active_space = Text(state.value, 3);
  Statement spaces(db_, "SELECT id,name,icon,position,coalesce(active_id,''),theme_hue,theme_intensity,theme_mode,theme_noise,theme_palette FROM spaces ORDER BY position");
  if (!spaces.value) return Error(500, "Workspace spaces unavailable");
  while (sqlite3_step(spaces.value) == SQLITE_ROW) {
    Space space;
    space.id = Text(spaces.value, 0); space.name = Text(spaces.value, 1); space.icon = Text(spaces.value, 2);
    space.position = sqlite3_column_int(spaces.value, 3); space.active_id = Text(spaces.value, 4);
    space.theme_hue = sqlite3_column_int(spaces.value, 5); space.theme_intensity = sqlite3_column_int(spaces.value, 6);
    space.theme_mode = sqlite3_column_int(spaces.value, 7); space.theme_noise = sqlite3_column_int(spaces.value, 8);
    if (!DecodePalette(Text(spaces.value,9),space.theme_palette)) return Error(500,"Invalid saved palette");
    result.snapshot.spaces.push_back(std::move(space));
  }
  Statement nodes(db_, "SELECT id,coalesce(parent_id,''),kind,title,url,position,pinned,expanded,favorite,archived,page_title,custom_title,archived_at,space_id,last_active_at FROM nodes ORDER BY coalesce(parent_id,''),position");
  if (!nodes.value) return Error(500, "Workspace nodes unavailable");
  int code;
  while ((code = sqlite3_step(nodes.value)) == SQLITE_ROW) {
    TreeNode node;
    node.id = Text(nodes.value, 0); node.parent = Text(nodes.value, 1); node.folder = sqlite3_column_int(nodes.value, 2) == 1;
    node.title = Text(nodes.value, 3); node.url = Text(nodes.value, 4); node.position = sqlite3_column_int(nodes.value, 5);
    node.pinned = sqlite3_column_int(nodes.value, 6) != 0; node.expanded = sqlite3_column_int(nodes.value, 7) != 0;
    node.favorite = sqlite3_column_int(nodes.value, 8) != 0; node.archived = sqlite3_column_int(nodes.value, 9) != 0;
    node.page_title = Text(nodes.value, 10); node.custom_title = sqlite3_column_int(nodes.value, 11) != 0;
    node.archived_at = sqlite3_column_int64(nodes.value, 12);
    node.space = Text(nodes.value, 13);
    node.last_active_at = sqlite3_column_int64(nodes.value,14);
    result.snapshot.nodes.push_back(std::move(node));
  }
  if (code != SQLITE_DONE) return Error(500,"Invalid workspace tree");
  Statement splits(db_,"SELECT anchor,other,title,ratio,vertical,reversed FROM splits ORDER BY anchor");
  if (!splits.value) return Error(500,"Split views unavailable");
  while ((code = sqlite3_step(splits.value)) == SQLITE_ROW) {
    SplitGroup split; split.anchor = Text(splits.value,0); split.other = Text(splits.value,1); split.title = Text(splits.value,2);
    split.ratio = sqlite3_column_int(splits.value,3); split.vertical = sqlite3_column_int(splits.value,4) != 0;
    split.reversed = sqlite3_column_int(splits.value,5) != 0; result.snapshot.splits.push_back(std::move(split));
  }
  if (code != SQLITE_DONE) return Error(500,"Invalid split views");
  Statement saved(db_,"SELECT anchor,other,title,ratio,vertical,reversed FROM archived_splits ORDER BY anchor");
  if (!saved.value) return Error(500,"Archived split views unavailable");
  while ((code=sqlite3_step(saved.value)) == SQLITE_ROW) result.snapshot.archived_splits.push_back({Text(saved.value,0),Text(saved.value,1),Text(saved.value,2),
    sqlite3_column_int(saved.value,3),sqlite3_column_int(saved.value,4)!=0,sqlite3_column_int(saved.value,5)!=0});
  if (code != SQLITE_DONE || !Valid(result.snapshot)) return Error(500, "Invalid workspace tree");
  return result;
}
TreeResult TreeStore::Read() {
  if (!db_ || !Exec("BEGIN")) return Error(500, "Cannot read workspace");
  auto result = Load();
  if (!result || !Exec("COMMIT")) { Exec("ROLLBACK"); if (result) return Error(500, "Workspace read failed"); }
  return result;
}
TreeResult TreeStore::Apply(const TreeCommand& command, const TreeSnapshot* window) {
  if (!db_ || !Exec("BEGIN IMMEDIATE")) return Error(503, "Workspace database busy");
  auto rollback = [&](int code, const char* message) { Exec("ROLLBACK"); return Error(code, message); };
  auto result = Load();
  if (!result) return rollback(result.code, result.error.c_str());
  if (command.expected_revision != result.snapshot.revision) return rollback(409, "Workspace changed; refresh and retry");
  const auto before = result.snapshot;
  auto& tree = result.snapshot;
  const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
  if (command.action == TreeAction::AutoArchive) {
    std::vector<std::string> ids{command.id};
    const auto group=std::find_if(tree.splits.begin(),tree.splits.end(),[&](const auto& s) { return s.anchor==command.id || s.other==command.id; });
    if (group!=tree.splits.end()) ids={group->anchor,group->other};
    for (const auto& id:ids) {
      const auto* item=FindConst(tree,id);
      if (!item || item->folder || item->pinned || item->favorite || item->archived || !item->parent.empty() ||
          item->last_active_at<=0 || item->last_active_at>command.not_after) return rollback(409,"Tab is no longer idle Today");
    }
    if (group!=tree.splits.end()) { tree.archived_splits.push_back(*group); tree.splits.erase(group); }
    for (const auto& id:ids) {
      auto* item=Find(tree,id); item->archived=true; item->archived_at=now;
      if (tree.active_id==id) tree.active_id.clear();
      for (auto& space:tree.spaces) if (space.active_id==id) space.active_id.clear();
    }
    if (!Valid(tree)) return rollback(400,"Invalid automatic archive");
    return Save(std::move(result),before);
  }
  // The persistent tree is shared; selections belong to the command's window.
  // Validate the projection against current nodes before applying a mutation.
  if (window && FindSpace(tree, window->active_space)) {
    tree.active_space = window->active_space;
    tree.active_id = window->active_id;
    if (const auto* active = FindConst(tree, tree.active_id); !active || active->folder || active->archived ||
        (!SpaceOf(tree, *active).empty() && SpaceOf(tree, *active) != tree.active_space)) tree.active_id.clear();
    for (auto& space : tree.spaces) for (const auto& saved : window->spaces)
      if (space.id == saved.id) space.active_id = saved.active_id;
  }
  auto node = Find(tree, command.id);
  // Where the entry lived before this command (MoveNode resets nesting).
  const std::string node_space = node ? SpaceOf(tree, *node) : "";
  const bool space_command = command.action == TreeAction::CreateSpace || command.action == TreeAction::EditSpace ||
      command.action == TreeAction::DeleteSpace || command.action == TreeAction::SwitchSpace || command.action == TreeAction::MoveSpace;
  const bool split_command = command.action >= TreeAction::CreateSplit && command.action <= TreeAction::SwapSplit;
  if (split_command) {
    auto group = std::find_if(tree.splits.begin(),tree.splits.end(),[&](const auto& split) { return split.anchor == command.id; });
    if (command.action == TreeAction::CreateSplit) {
      const auto* other = FindConst(tree,command.parent);
      if (!node || !other || node->folder || other->folder || node->archived || other->archived || node->favorite || node->id == other->id ||
          (!other->favorite && SpaceOf(tree,*node) != SpaceOf(tree,*other))) return rollback(400,"Choose two distinct tabs in this space");
      for (const auto& split : tree.splits) if (split.anchor == node->id || split.other == node->id || split.anchor == other->id || split.other == other->id)
        return rollback(409,"Separate an existing split before combining these tabs");
      tree.splits.push_back({node->id,other->id,"",5000,command.value,command.reversed}); tree.active_id = node->id;
    } else {
      if (group == tree.splits.end()) return rollback(404,"Split view is unavailable");
      if (command.action == TreeAction::RemoveSplit) tree.splits.erase(group);
      else if (command.action == TreeAction::RenameSplit) {
        if (!SafeText(command.title,240,true)) return rollback(400,"Invalid split title"); group->title = command.title;
      } else if (command.action == TreeAction::LayoutSplit) {
        if (command.position < 1000 || command.position > 9000) return rollback(400,"Invalid split ratio");
        group->ratio = command.position; group->vertical = command.value;
      } else { group->reversed = !group->reversed; group->ratio = 10000 - group->ratio; }
    }
  } else if (space_command) {
    auto* space = FindSpace(tree, command.id);
    if (command.action != TreeAction::CreateSpace && !space) return rollback(404, "Unknown space");
    switch (command.action) {
      case TreeAction::CreateSpace: {
        if (tree.spaces.size() >= kMaxSpaces) return rollback(409, "Too many spaces");
        if (!SafeText(command.title, 40) || !ValidIcon(command.icon)) return rollback(400, "Invalid space name or icon");
        const auto* current = tree.ActiveSpace();
        Space created;
        created.id = NewId(); if (created.id.empty()) return rollback(500, "Cannot generate space identity");
        created.name = command.title; created.icon = command.icon;
        created.position = static_cast<int>(tree.spaces.size());
        // A new space gets its own color (golden angle from the current one).
        created.theme_hue = current ? (current->theme_hue + 137) % 360 : 307;
        created.theme_mode = current ? current->theme_mode : 0; created.theme_noise = current ? current->theme_noise : 0;
        result.created_id = created.id;
        tree.spaces.push_back(created);
        tree.active_space = created.id; tree.active_id.clear();
        break;
      }
      case TreeAction::EditSpace:
        if (!SafeText(command.title, 40) || !ValidIcon(command.icon)) return rollback(400, "Invalid space name or icon");
        space->name = command.title; space->icon = command.icon; break;
      case TreeAction::SwitchSpace:
        tree.active_space = space->id; tree.active_id = SpaceLanding(tree, *space, command.live_nodes); break;
      case TreeAction::MoveSpace: {
        const int count = static_cast<int>(tree.spaces.size());
        if (command.position < 0 || command.position >= count) return rollback(400, "Invalid space position");
        std::sort(tree.spaces.begin(), tree.spaces.end(), [](const auto& a, const auto& b) { return a.position < b.position; });
        auto moving = std::find_if(tree.spaces.begin(), tree.spaces.end(), [&](const auto& s) { return s.id == command.id; });
        Space copy = *moving; tree.spaces.erase(moving);
        tree.spaces.insert(tree.spaces.begin() + command.position, copy);
        for (int i = 0; i < count; ++i) tree.spaces[i].position = i;
        break;
      }
      case TreeAction::DeleteSpace: {
        // Deletes the space with everything in it (Arc). Its open pages close.
        if (tree.spaces.size() < 2) return rollback(409, "The last space cannot be deleted");
        const auto gone = space->id;
        std::set<std::string> removed;
        for (const auto& n : tree.nodes) if (SpaceOf(tree, n) == gone) removed.insert(n.id);
        std::erase_if(tree.nodes, [&](const auto& n) { return removed.contains(n.id); });
        Normalize(tree, "");
        std::sort(tree.spaces.begin(), tree.spaces.end(), [](const auto& a, const auto& b) { return a.position < b.position; });
        const auto index = std::find_if(tree.spaces.begin(), tree.spaces.end(), [&](const auto& s) { return s.id == gone; }) - tree.spaces.begin();
        tree.spaces.erase(tree.spaces.begin() + index);
        for (size_t i = 0; i < tree.spaces.size(); ++i) tree.spaces[i].position = static_cast<int>(i);
        if (tree.active_space == gone) {
          const auto& next = tree.spaces[std::min<size_t>(index > 0 ? index - 1 : 0, tree.spaces.size() - 1)];
          tree.active_space = next.id; tree.active_id = SpaceLanding(tree, next, command.live_nodes);
        } else if (removed.contains(tree.active_id)) {
          tree.active_id.clear();
        }
        break;
      }
      default: break;
    }
  } else if (command.action == TreeAction::CreateFolder || command.action == TreeAction::CreateTab) {
    if (!SafeText(command.title, 240) || (command.action == TreeAction::CreateTab && !SafeUrl(command.url)))
      return rollback(400, "Invalid title or URL");
    const auto* parent = Find(tree, command.parent);
    if (!command.parent.empty() && (!parent || !parent->folder)) return rollback(400, "Parent must be a folder in this profile");
    TreeNode created;
    // Only native callers can supply an identity (adopting a transient Peek).
    // UI CreateTab has a closed field set and cannot pass command.id.
    created.id = command.id.empty() ? NewId() : command.id;
    if (created.id.size()!=36 || FindConst(tree,created.id)) return rollback(400,"Invalid or duplicate node identity");
    created.parent = command.parent; created.folder = command.action == TreeAction::CreateFolder;
    if (command.parent.empty()) created.space = tree.active_space;
    created.pinned = created.folder || !command.parent.empty();
    created.title = command.title; created.url = created.folder ? "" : command.url; created.last_active_at=now;
    const auto* after = command.after.empty() ? nullptr : FindConst(tree, command.after);
    if (!created.folder && command.parent.empty() && after && after->parent.empty() && !after->folder && !after->pinned &&
        !after->favorite && !after->archived && after->space == tree.active_space) {
      // A link's tab opens right below the Today tab it came from (Chrome). Root siblings share one
      // position space with Pinned/Favorites; shifting those after it keeps every relative order.
      const int at = after->position + 1;
      for (auto& other : tree.nodes) if (other.parent.empty() && other.position >= at) ++other.position;
      created.position = at;
    } else if (!created.folder && command.parent.empty()) {
      // New Today tabs open first (Arc). Root siblings share one position
      // space with Pinned/Favorites; shifting all keeps their relative order.
      for (auto& other : tree.nodes) if (other.parent.empty()) ++other.position;
      created.position = 0;
    } else {
      created.position = static_cast<int>(std::count_if(tree.nodes.begin(), tree.nodes.end(), [&](const auto& n) { return n.parent == command.parent; }));
    }
    result.created_id = created.id;
    if (!created.folder && command.value) tree.active_id = created.id;
    tree.nodes.push_back(std::move(created));
    if (!command.split_with.empty()) {
      const auto* source=FindConst(tree,command.split_with);
      if (command.action!=TreeAction::CreateTab || !command.parent.empty() || !source || source->folder || source->archived ||
          source->id==result.created_id || (!source->favorite && SpaceOf(tree,*source)!=tree.active_space)) return rollback(400,"Preview source cannot enter this split");
      if (source->pinned) {
        if (!SafeUrl(command.split_source_url) || !SafeText(command.split_source_title,240)) return rollback(400,"Invalid copied source page");
        TreeNode copy; copy.id=NewId(); copy.space=tree.active_space; copy.url=command.split_source_url; copy.title=command.split_source_title;
        copy.custom_title=source->custom_title; copy.last_active_at=now; copy.expanded=true; copy.position=0;
        for (auto& sibling:tree.nodes) if (sibling.parent.empty()) ++sibling.position;
        tree.splits.push_back({copy.id,result.created_id,"",5000,false,false}); tree.nodes.push_back(std::move(copy));
      } else {
        for (const auto& split:tree.splits) if (split.anchor==source->id || split.other==source->id) return rollback(409,"Source is already in a split");
        tree.splits.push_back({source->id,result.created_id,"",5000,false,false});
      }
    }
  } else {
    if (!node) return rollback(404, "Unknown node in this profile");
    switch (command.action) {
      case TreeAction::Rename:
        if (!SafeText(command.title, 240)) return rollback(400, "Invalid title");
        node->title = command.title; node->custom_title = !node->folder; break;
      case TreeAction::AutomaticTitle:
        if (node->folder) return rollback(400, "Only pages have automatic titles");
        node->custom_title = false; break;
      case TreeAction::Pin: {
        const bool was_pinned = node->pinned;
        if (node->archived || (node->folder && !command.value)) return rollback(400, "Folders belong to Pinned");
        if (!command.value && !MoveNode(tree, *node, "", -1)) return rollback(400, "Cannot unpin tab");
        node->pinned = command.value; node->favorite = false;
        if (command.value != was_pinned && !node->folder && !command.url.empty()) {
          if (!SafeUrl(command.url)) return rollback(400, "Invalid pinned URL");
          node->url = command.url;
        }
        break;
      }
      case TreeAction::Expand:
        if (!node->folder) return rollback(400, "Only folders can expand");
        node->expanded = command.value; break;
      case TreeAction::Activate:
        if (node->folder || node->archived) return rollback(400, "Entry is not an available page");
        // Showing a page of another space (e.g. from history) enters that space.
        if (!node_space.empty()) tree.active_space = node_space;
        tree.active_id = node->id; node->last_active_at=now; break;
      case TreeAction::MoveToSpace: {
        if (!node->parent.empty() || node->favorite || node->archived) return rollback(400, "Only top-level Pinned or Today entries move between spaces");
        if (!FindSpace(tree, command.space)) return rollback(404, "Unknown space");
        if (command.space == node->space) break;
        // Keep the shared root order; the entry just shows in the other space.
        node->space = command.space;
        if (tree.active_id == node->id) tree.active_id = command.next_active;
        // The page on screen may have been inside the moved folder.
        if (const auto* active = FindConst(tree, tree.active_id)) {
          const auto owner = SpaceOf(tree, *active);
          if (!owner.empty() && owner != tree.active_space) tree.active_id.clear();
        } else {
          tree.active_id.clear();
        }
        break;
      }
      case TreeAction::SetUrl:
        // A pinned page's saved address changes only on purpose (its menu's
        // "Edit Pinned Page", section "home"), never through navigation.
        if (node->folder || node->archived || !SafeUrl(command.url) || ((node->pinned || node->favorite) && command.section != "home"))
          return rollback(400, "Cannot replace a persistent home URL through navigation");
        node->url = command.url; break;
      case TreeAction::Place:
      case TreeAction::Move: {
        if (node->archived) return rollback(400, "Restore archived tabs before moving");
        if (command.action == TreeAction::Place && command.section == "favorites" &&
            std::any_of(tree.splits.begin(),tree.splits.end(),[&](const auto& split) { return split.anchor == node->id; }))
          return rollback(400,"Split views belong to Pinned or Today");
        const bool was_pinned = node->pinned;
        if (command.action == TreeAction::Place) {
          if (command.section != "pinned" && command.section != "today" && command.section != "favorites") return rollback(400, "Unknown sidebar section");
          if ((command.section != "pinned" && (node->folder || !command.parent.empty()))) return rollback(400, "Folders belong to Pinned");
        }
        if (!MoveNode(tree, *node, command.parent, command.position)) return rollback(400, "Invalid folder placement, cycle or sibling position");
        if (command.action == TreeAction::Place) {
          node->pinned = command.section != "today"; node->favorite = command.section == "favorites";
        }
        if (was_pinned != node->pinned && !command.url.empty()) {
          if (!SafeUrl(command.url)) return rollback(400, "Invalid pinned URL");
          node->url = command.url;
        }
        break;
      }
      case TreeAction::Unload:
      case TreeAction::Archive:
        if (node->folder || node->archived) return rollback(400, "Entry is not an available page");
        if (command.action == TreeAction::Archive) {
          if (!node->pinned && !command.url.empty()) {
            if (!SafeUrl(command.url)) return rollback(400, "Invalid final page URL");
            node->url = command.url;
          }
          if (!MoveNode(tree, *node, "", -1)) return rollback(400, "Cannot archive tab");
          node->pinned = false; node->favorite = false; node->archived = true;
          node->archived_at = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        }
        if (tree.active_id == node->id) tree.active_id = command.next_active;
        break;
      case TreeAction::Restore:
        if (!node->archived) return rollback(400, "Tab is not archived");
        if (const auto saved=std::find_if(tree.archived_splits.begin(),tree.archived_splits.end(),[&](const auto& s) { return s.anchor==node->id || s.other==node->id; }); saved!=tree.archived_splits.end()) {
          const auto split=*saved;
          for (const auto& member:{split.anchor,split.other}) {
            auto* page=Find(tree,member); page->archived=false; page->archived_at=0; page->space=tree.active_space; page->last_active_at=now;
          }
          tree.splits.push_back(split); tree.archived_splits.erase(saved);
        }
        // Restored into the space on screen.
        node->archived = false; node->archived_at = 0; node->space = tree.active_space; node->last_active_at=now; tree.active_id = node->id; break;
      case TreeAction::RemoveFolder: {
        const auto ids=FolderNodeIds(tree,command.id);
        if (ids.empty() || ids!=command.confirmed_nodes) return rollback(409,"Folder contents changed; review removal again");
        const std::set<std::string> scope(ids.begin(),ids.end());
        const auto parent=node->parent;
        for (const auto& split:tree.splits)
          if (scope.contains(split.anchor) && scope.contains(split.other)) tree.archived_splits.push_back(split);
        for (auto& item:tree.nodes) if (scope.contains(item.id) && !item.folder) {
          if (!MoveNode(tree,item,"",-1)) return rollback(400,"Cannot archive folder tab");
          item.pinned=false; item.favorite=false; item.archived=true; item.archived_at=now; item.space=node_space;
        }
        if (scope.contains(tree.active_id)) tree.active_id.clear();
        for (auto& space:tree.spaces) if (scope.contains(space.active_id)) space.active_id.clear();
        std::erase_if(tree.nodes,[&](const auto& item) { return item.folder && scope.contains(item.id); });
        Normalize(tree,parent); Normalize(tree,""); break;
      }
      case TreeAction::RemoveLeaf: {
        if (std::any_of(tree.nodes.begin(), tree.nodes.end(), [&](const auto& n) { return n.parent == node->id; }))
          return rollback(409, "Move children before removing a folder");
        auto parent = node->parent;
        if (tree.active_id == node->id) tree.active_id.clear();
        std::erase_if(tree.nodes, [&](const auto& n) { return n.id == command.id; }); Normalize(tree, parent); break;
      }
      default: return rollback(400, "Unsupported tree operation");
    }
  }
  // Only top-level Pinned/Today/archived entries carry a space. An entry that
  // became top-level keeps the space it was in; a new one joins the active space.
  for (auto& n : tree.nodes) {
    if (!n.parent.empty() || n.favorite) n.space.clear();
    else if (n.space.empty()) n.space = n.id == command.id && !node_space.empty() ? node_space : tree.active_space;
  }
  // Remember the page last shown in each space.
  if (command.action == TreeAction::MoveToSpace || command.action == TreeAction::Move || command.action == TreeAction::Place) {
    for (const auto& split : tree.splits) {
      const auto* old_anchor = FindConst(before,split.anchor);
      const auto* anchor = FindConst(tree,split.anchor); auto* other = Find(tree,split.other);
      if (!old_anchor || !anchor || !other || anchor->favorite || other->favorite || anchor->archived || other->archived) continue;
      const auto destination = SpaceOf(tree,*anchor);
      if (destination == SpaceOf(before,*old_anchor) || destination == SpaceOf(tree,*other)) continue;
      if (!MoveNode(tree,*other,"",-1)) return rollback(400,"Cannot move split member to the destination space");
      other->space = destination;
    }
    if (const auto* active = FindConst(tree,tree.active_id); active && !active->favorite && SpaceOf(tree,*active) != tree.active_space) tree.active_id.clear();
  }
  std::erase_if(tree.splits,[&](const auto& split) {
    const auto* a = FindConst(tree,split.anchor); const auto* b = FindConst(tree,split.other);
    return !a || !b || a->archived || b->archived || a->favorite ||
      (!b->favorite && SpaceOf(tree,*a) != SpaceOf(tree,*b)) ||
      (command.action == TreeAction::Unload && (split.anchor == command.id || split.other == command.id));
  });
  std::erase_if(tree.archived_splits,[&](const auto& split) {
    const auto* a=FindConst(tree,split.anchor); const auto* b=FindConst(tree,split.other);
    return !a || !b || !a->archived || !b->archived;
  });
  if (auto* space = FindSpace(tree, tree.active_space); space && !tree.active_id.empty()) space->active_id = tree.active_id;
  if (!Valid(tree)) return rollback(400, "Tree limits or invariants violated (10000 nodes, 64 levels)");
  return Save(std::move(result), before);
}
TreeResult TreeStore::Save(TreeResult result, const TreeSnapshot& before) {
  auto& tree = result.snapshot;
  auto rollback = [&](int code, const char* message) { Exec("ROLLBACK"); return Error(code, message); };
  if (tree.splits != before.splits) {
    if (!Exec("DELETE FROM splits")) return rollback(500,"Cannot update split views");
    Statement put(db_,"INSERT INTO splits(anchor,other,title,ratio,vertical,reversed) VALUES(?,?,?,?,?,?)");
    if (!put.value) return rollback(500,"Cannot prepare split views");
    for (const auto& split : tree.splits) {
      put.Text(1,split.anchor); put.Text(2,split.other); put.Text(3,split.title);
      sqlite3_bind_int(put.value,4,split.ratio); sqlite3_bind_int(put.value,5,split.vertical); sqlite3_bind_int(put.value,6,split.reversed);
      if (sqlite3_step(put.value) != SQLITE_DONE) return rollback(500,"Cannot save split view"); sqlite3_reset(put.value);
    }
  }
  if (tree.archived_splits!=before.archived_splits) {
    if (!Exec("DELETE FROM archived_splits")) return rollback(500,"Cannot update archived splits");
    Statement put(db_,"INSERT INTO archived_splits(anchor,other,title,ratio,vertical,reversed) VALUES(?,?,?,?,?,?)");
    if (!put.value) return rollback(500,"Cannot save archived splits");
    for (const auto& split:tree.archived_splits) {
      put.Text(1,split.anchor); put.Text(2,split.other); put.Text(3,split.title);
      sqlite3_bind_int(put.value,4,split.ratio); sqlite3_bind_int(put.value,5,split.vertical); sqlite3_bind_int(put.value,6,split.reversed);
      if (sqlite3_step(put.value)!=SQLITE_DONE) return rollback(500,"Cannot save archived split"); sqlite3_reset(put.value);
    }
  }
  Statement write(db_, "INSERT INTO nodes(id,parent_id,kind,title,url,position,pinned,expanded,favorite,archived,page_title,custom_title,archived_at,space_id,last_active_at) VALUES(?,NULLIF(?,''),?,?,?,?,?,?,?,?,?,?,?,?,?) "
      "ON CONFLICT(id) DO UPDATE SET parent_id=excluded.parent_id,title=excluded.title,url=excluded.url,position=excluded.position,pinned=excluded.pinned,expanded=excluded.expanded,favorite=excluded.favorite,archived=excluded.archived,page_title=excluded.page_title,custom_title=excluded.custom_title,archived_at=excluded.archived_at,space_id=excluded.space_id,last_active_at=excluded.last_active_at");
  Statement erase(db_, "DELETE FROM nodes WHERE id=?");
  if (!write.value || !erase.value) return rollback(500, "Cannot prepare workspace update");
  for (const auto& old : before.nodes) {
    if (!Find(tree, old.id)) { erase.Text(1, old.id); if (sqlite3_step(erase.value) != SQLITE_DONE) return rollback(500, "Workspace delete failed"); sqlite3_reset(erase.value); }
  }
  for (const auto& n : tree.nodes) {
    auto previous = std::find_if(before.nodes.begin(), before.nodes.end(), [&](const auto& old) { return old.id == n.id; });
    if (previous != before.nodes.end() && *previous == n) continue;
    write.Text(1, n.id); write.Text(2, n.parent); sqlite3_bind_int(write.value, 3, n.folder);
    write.Text(4, n.title); write.Text(5, n.url); sqlite3_bind_int(write.value, 6, n.position);
    sqlite3_bind_int(write.value, 7, n.pinned); sqlite3_bind_int(write.value, 8, n.expanded);
    sqlite3_bind_int(write.value, 9, n.favorite); sqlite3_bind_int(write.value, 10, n.archived);
    write.Text(11, n.page_title); sqlite3_bind_int(write.value, 12, n.custom_title);
    sqlite3_bind_int64(write.value, 13, n.archived_at); write.Text(14, n.space);
    sqlite3_bind_int64(write.value,15,n.last_active_at);
    if (sqlite3_step(write.value) != SQLITE_DONE) return rollback(500, "Workspace write failed");
    sqlite3_reset(write.value);
  }
  if (tree.spaces != before.spaces) {
    Statement drop(db_, "DELETE FROM spaces WHERE id=?");
    Statement put(db_, "INSERT INTO spaces(id,name,icon,position,active_id,theme_hue,theme_intensity,theme_mode,theme_noise,theme_palette) VALUES(?,?,?,?,NULLIF(?,''),?,?,?,?,?) "
        "ON CONFLICT(id) DO UPDATE SET name=excluded.name,icon=excluded.icon,position=excluded.position,active_id=excluded.active_id,"
        "theme_hue=excluded.theme_hue,theme_intensity=excluded.theme_intensity,theme_mode=excluded.theme_mode,theme_noise=excluded.theme_noise,theme_palette=excluded.theme_palette");
    if (!drop.value || !put.value) return rollback(500, "Cannot prepare spaces update");
    for (const auto& old : before.spaces) {
      if (std::any_of(tree.spaces.begin(), tree.spaces.end(), [&](const auto& s) { return s.id == old.id; })) continue;
      drop.Text(1, old.id); if (sqlite3_step(drop.value) != SQLITE_DONE) return rollback(500, "Space delete failed"); sqlite3_reset(drop.value);
    }
    for (const auto& s : tree.spaces) {
      put.Text(1, s.id); put.Text(2, s.name); put.Text(3, s.icon); sqlite3_bind_int(put.value, 4, s.position); put.Text(5, s.active_id);
      sqlite3_bind_int(put.value, 6, s.theme_hue); sqlite3_bind_int(put.value, 7, s.theme_intensity);
      sqlite3_bind_int(put.value, 8, s.theme_mode); sqlite3_bind_int(put.value, 9, s.theme_noise);
      put.Text(10,EncodePalette(s.theme_palette));
      if (sqlite3_step(put.value) != SQLITE_DONE) return rollback(500, "Space write failed");
      sqlite3_reset(put.value);
    }
  }
  Statement state(db_, "UPDATE state SET revision=revision+1,active_id=NULLIF(?,''),active_space=? WHERE singleton=1");
  if (!state.value) return rollback(500, "Cannot prepare workspace state");
  state.Text(1, tree.active_id); state.Text(2, tree.active_space);
  if (sqlite3_step(state.value) != SQLITE_DONE || !Exec("COMMIT")) return rollback(500, "Workspace commit failed");
  ++tree.revision;
  return result;
}
TreeResult TreeStore::Import(const TreeSnapshot& incoming, const std::filesystem::path& backup) {
  if (!db_ || !Valid(incoming)) return Error(400, "Invalid imported tree; nothing imported");
  auto preview = Read(); if (!preview) return preview;
  for (const auto& s : incoming.spaces) if (FindSpace(preview.snapshot, s.id)) return Error(409, "These tabs and bookmarks were already imported. Nothing added twice.");
  if (preview.snapshot.spaces.size() + incoming.spaces.size() > kMaxSpaces || preview.snapshot.nodes.size() + incoming.nodes.size() > 10000)
    return Error(409, "Import exceeds Sol's limits (24 Spaces / 10,000 entries)");
  // SQLite's backup API includes committed WAL pages. Never copy a live DB file.
  std::error_code ec; std::filesystem::create_directories(backup.parent_path(), ec);
  if (ec || std::filesystem::exists(backup, ec)) return Error(500, "Cannot create pre-import backup");
  sqlite3* copy = nullptr; const auto path = backup.u8string();
  if (sqlite3_open_v2(reinterpret_cast<const char*>(path.c_str()), &copy, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) != SQLITE_OK) {
    sqlite3_close(copy); return Error(500, "Cannot open pre-import backup");
  }
  auto job = sqlite3_backup_init(copy, "main", db_, "main");
  const bool copied = job && sqlite3_backup_step(job, -1) == SQLITE_DONE;
  const int finish = job ? sqlite3_backup_finish(job) : SQLITE_ERROR; sqlite3_close(copy);
  if (!copied || finish != SQLITE_OK) return Error(500, "Pre-import backup failed; nothing imported");
  if (!Exec("BEGIN IMMEDIATE")) return Error(503, "Workspace database busy");
  auto rollback = [&](int code, const char* message) { Exec("ROLLBACK"); return Error(code, message); };
  auto result = Load(); if (!result) return rollback(result.code, result.error.c_str());
  const auto before = result.snapshot; auto& tree = result.snapshot;
  for (const auto& s : incoming.spaces) if (FindSpace(tree, s.id)) return rollback(409, "These tabs and bookmarks were already imported. Nothing added twice.");
  if (tree.spaces.size() + incoming.spaces.size() > kMaxSpaces) return rollback(409, "Not enough room for the imported Spaces (maximum 24)");
  if (tree.nodes.size() + incoming.nodes.size() > 10000) return rollback(409, "Import would exceed 10,000 entries");
  for (auto s : incoming.spaces) { s.position = static_cast<int>(tree.spaces.size()); tree.spaces.push_back(s); }
  int root_position = static_cast<int>(std::count_if(tree.nodes.begin(), tree.nodes.end(), [](const auto& n) { return n.parent.empty(); }));
  int favorites = static_cast<int>(std::count_if(tree.nodes.begin(), tree.nodes.end(), [](const auto& n) { return n.favorite; }));
  for (auto n : incoming.nodes) {
    if (Find(tree, n.id)) return rollback(409, "This profile contains entries that are already imported");
    if (n.favorite && ++favorites > 12) { n.favorite = false; n.space = incoming.active_space; }
    if (n.parent.empty()) n.position = root_position++;
    tree.nodes.push_back(std::move(n));
  }
  if (!Valid(tree)) return rollback(400, "Imported hierarchy is invalid; nothing imported");
  return Save(std::move(result), before);
}
TreeResult TreeStore::TouchTabs(const std::map<std::string,int64_t>& activity) {
  if (!db_ || activity.size()>10000 || std::any_of(activity.begin(),activity.end(),[](const auto& item) { return item.second<=0; }) ||
      !Exec("BEGIN IMMEDIATE")) return Error(500,"Cannot save tab activity");
  Statement put(db_,"UPDATE nodes SET last_active_at=? WHERE id=? AND archived=0");
  if (!put.value) { Exec("ROLLBACK"); return Error(500,"Cannot prepare tab activity"); }
  for (const auto& [id,at]:activity) {
    sqlite3_bind_int64(put.value,1,at); put.Text(2,id);
    if (sqlite3_step(put.value)!=SQLITE_DONE) { Exec("ROLLBACK"); return Error(500,"Cannot update tab activity"); }
    sqlite3_reset(put.value);
  }
  if (!Exec("COMMIT")) { Exec("ROLLBACK"); return Error(500,"Cannot commit tab activity"); }
  return Read();
}
TreeResult TreeStore::SetPageTitle(const std::string& id, const std::string& title) {
  if (!db_ || id.size() != 36 || !SafeText(title, 240, true)) return Error(400, "Invalid page title");
  Statement update(db_, "UPDATE nodes SET page_title=? WHERE id=? AND kind=0");
  if (!update.value) return Error(500, "Cannot prepare page title");
  update.Text(1, title); update.Text(2, id);
  if (sqlite3_step(update.value) != SQLITE_DONE) return Error(500, "Cannot save page title");
  return Read();
}
TreeResult TreeStore::SetSidebarWidth(int width) {
  if (!db_ || width < 200 || width > 480) return Error(400, "Invalid sidebar width");
  Statement update(db_, "UPDATE state SET sidebar_width=? WHERE singleton=1");
  if (!update.value) return Error(500, "Cannot prepare sidebar preference");
  sqlite3_bind_int(update.value, 1, width);
  if (sqlite3_step(update.value) != SQLITE_DONE) return Error(500, "Cannot save sidebar width");
  return Read();
}
TreeResult TreeStore::SetTheme(int hue, int intensity, int mode, int noise, const std::string& space, const std::optional<ThemePalette>& palette) {
  if (!db_ || hue < 0 || hue > 359 || intensity < 0 || intensity > 160 || mode < 0 || mode > 2 || noise < 0 || noise > 100 || (palette && !ValidPalette(*palette)))
    return Error(400, "Invalid theme");
  Statement update(db_, "UPDATE spaces SET theme_hue=?,theme_intensity=?,theme_mode=?,theme_noise=?,theme_palette=? WHERE id=CASE WHEN ?='' THEN (SELECT active_space FROM state WHERE singleton=1) ELSE ? END");
  if (!update.value) return Error(500, "Cannot prepare theme");
  sqlite3_bind_int(update.value, 1, hue); sqlite3_bind_int(update.value, 2, intensity);
  sqlite3_bind_int(update.value, 3, mode); sqlite3_bind_int(update.value, 4, noise);
  update.Text(5,EncodePalette(palette));update.Text(6, space); update.Text(7, space);
  if (sqlite3_step(update.value) != SQLITE_DONE) return Error(500, "Cannot save theme");
  if (sqlite3_changes(db_) != 1) return Error(404,"Space no longer exists");
  return Read();
}
}  // namespace red
