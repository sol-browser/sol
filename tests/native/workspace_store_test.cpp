#include "storage/tree_store.h"
#include <windows.h>
#include <algorithm>
#include <cstdio>
#include <vector>
#include <chrono>
#include <sqlite3.h>

namespace {
int failures = 0;
void Check(bool test, const char* name) { if (!test) { ++failures; std::fprintf(stderr, "FAIL: %s\n", name); } }
red::TreeNode Find(const red::TreeSnapshot& snapshot, const std::string& id) {
  for (const auto& n : snapshot.nodes) if (n.id == id) return n;
  return {};
}
}
int wmain(int argc, wchar_t** argv) {
  if (argc == 3 && std::wstring(argv[1]) == L"--crash-write") {
    sqlite3* db = nullptr;
    sqlite3_open16(argv[2], &db);
    if (sqlite3_exec(db, "BEGIN IMMEDIATE; UPDATE nodes SET title='uncommitted'; UPDATE state SET revision=999;", nullptr, nullptr, nullptr) != SQLITE_OK) return 88;
    ExitProcess(77); // Deliberate interrupted writer: no destructors/rollback.
  }
  const auto path = std::filesystem::temp_directory_path() / (L"red-tree-" + std::to_wstring(GetCurrentProcessId()) + L".sqlite3");
  const auto foreign_path = path.wstring() + L"-foreign";
  {
    sqlite3* db = nullptr; sqlite3_open16(foreign_path.c_str(), &db);
    sqlite3_exec(db, "CREATE TABLE foreign_data(value); INSERT INTO foreign_data VALUES('preserved');", nullptr, nullptr, nullptr);
    { red::TreeStore store; Check(!store.Open(foreign_path), "refuse unrelated SQLite file"); }
    sqlite3_stmt* query = nullptr; sqlite3_prepare_v2(db, "SELECT value FROM foreign_data", -1, &query, nullptr);
    Check(sqlite3_step(query) == SQLITE_ROW && std::string(reinterpret_cast<const char*>(sqlite3_column_text(query, 0))) == "preserved", "unrelated database remains intact");
    sqlite3_finalize(query);
    sqlite3_exec(db, "PRAGMA user_version=99", nullptr, nullptr, nullptr);
    { red::TreeStore store; Check(!store.Open(foreign_path), "refuse future schema version"); }
    sqlite3_close(db);
    std::error_code error; std::filesystem::remove(foreign_path, error);
  }
  std::string folder, nested, tab, other;
  {
    const auto legacy_path = path.wstring() + L"-v1";
    sqlite3* db = nullptr; sqlite3_open16(legacy_path.c_str(), &db);
    const char* legacy = "CREATE TABLE nodes(id TEXT PRIMARY KEY,parent_id TEXT,kind INTEGER,title TEXT,url TEXT,position INTEGER,pinned INTEGER,expanded INTEGER);"
        "CREATE TABLE state(singleton INTEGER PRIMARY KEY,revision INTEGER,active_id TEXT);"
        "INSERT INTO nodes VALUES('11111111-1111-4111-8111-111111111111',NULL,1,'Legacy folder','',0,0,0);"
        "INSERT INTO nodes VALUES('22222222-2222-4222-8222-222222222222','11111111-1111-4111-8111-111111111111',0,'Legacy page','https://example.com/legacy',0,0,1);"
        "INSERT INTO nodes VALUES('33333333-3333-4333-8333-333333333333',NULL,0,'History','red://ui/history.html',1,0,0);"
        "INSERT INTO state VALUES(1,7,'22222222-2222-4222-8222-222222222222'); PRAGMA application_id=1380271170; PRAGMA user_version=1;";
    Check(sqlite3_exec(db, legacy, nullptr, nullptr, nullptr) == SQLITE_OK, "create original v1 fixture"); sqlite3_close(db);
    {
      red::TreeStore store; const auto r = store.Open(legacy_path);
      Check(static_cast<bool>(r) && r.snapshot.revision == 7 && r.snapshot.nodes.size() == 3 &&
          Find(r.snapshot, r.snapshot.active_id).url == "https://example.com/legacy" && Find(r.snapshot, r.snapshot.active_id).pinned &&
          !Find(r.snapshot, "11111111-1111-4111-8111-111111111111").expanded &&
          Find(r.snapshot, r.snapshot.active_id).custom_title && r.snapshot.sidebar_width == 256,
          "v1 migration preserves identities, revision, URLs, nesting, saved pinned names and default width");
      Check(Find(r.snapshot, "33333333-3333-4333-8333-333333333333").url == "sol://ui/history.html", "Red's own pages move to sol://");
    }
    sqlite3_open16(legacy_path.c_str(), &db);
    Check(sqlite3_exec(db, "UPDATE nodes SET page_title='Away title' WHERE pinned=1 AND kind=0;"
        "UPDATE nodes SET page_title='History title' WHERE pinned=0 AND kind=0; PRAGMA user_version=10;",
        nullptr, nullptr, nullptr) == SQLITE_OK, "seed legacy titles before v11 migration");
    sqlite3_close(db);
    {
      red::TreeStore store; const auto r = store.Open(legacy_path);
      const auto pin = Find(r.snapshot, r.snapshot.active_id);
      Check(r && pin.page_title.empty() && pin.custom_title && pin.title == "Legacy page" &&
          Find(r.snapshot, "33333333-3333-4333-8333-333333333333").page_title == "History title",
          "migration discards unverified pin metadata and preserves saved labels and Today titles");
    }
    for (const auto& suffix : {L"", L"-wal", L"-shm"}) { std::error_code error; std::filesystem::remove(legacy_path + suffix, error); }
  }
  red::TreeSnapshot expected;
  {
    red::TreeStore store;
    auto opened = store.Open(path);
    Check(static_cast<bool>(opened), "fresh schema migration");
    if (!opened) { std::fprintf(stderr, "%s\n", opened.error.c_str()); return 1; }
    auto apply = [&](red::TreeCommand c) { c.expected_revision = store.Read().snapshot.revision; return store.Apply(c); };
    red::TreeCommand c{red::TreeAction::CreateFolder}; c.title = "Работа";
    auto r = apply(c); Check(static_cast<bool>(r), "create Unicode root folder"); folder = r.created_id;
    c.parent = folder; c.title = "Nested"; r = apply(c); nested = r.created_id;
    c = {red::TreeAction::CreateTab}; c.parent = nested; c.title = "Saved tab"; c.url = "https://example.com/";
    r = apply(c); tab = r.created_id; Check(!tab.empty(), "stable generated tab identity");
    c.parent = ""; c.title = "Other"; r = apply(c); other = r.created_id;
    c = {red::TreeAction::Pin}; c.id = tab; c.value = true; Check(static_cast<bool>(apply(c)), "pin nested tab");
    c = {red::TreeAction::Expand}; c.id = folder; c.value = false; Check(static_cast<bool>(apply(c)), "collapse preserves descendants");
    c = {red::TreeAction::Activate}; c.id = tab; Check(static_cast<bool>(apply(c)), "activate saved tab");
    const auto metadata_revision = store.Read().snapshot.revision;
    const std::string home = "https://example.com/";
    auto metadata = store.SetPageTitle(tab, home, "Website title");
    Check(metadata && Find(metadata.snapshot, tab).page_title == "Website title" &&
          !Find(metadata.snapshot, tab).custom_title && metadata.snapshot.revision == metadata_revision,
          "site title persists independently of manual name and tree revision");
    metadata = store.SetPageTitle(tab, home + "away", "Away title");
    Check(metadata && Find(metadata.snapshot, tab).page_title == "Website title" &&
          metadata.snapshot.revision == metadata_revision, "another address cannot replace a saved title");
    metadata = store.SetSidebarWidth(336);
    Check(metadata && metadata.snapshot.sidebar_width == 336 && metadata.snapshot.revision == metadata_revision,
          "saved sidebar preference does not conflict with tree edits");
    Check(store.SetSidebarWidth(199).code == 400 && store.SetSidebarWidth(481).code == 400 &&
          store.SetPageTitle(tab, home, std::string("bad\0title", 9)).code == 400, "invalid preferences and metadata rejected");
    const auto fresh = store.Read().snapshot;
    Check(fresh.spaces.size() == 1 && fresh.spaces[0].name == "Personal" && fresh.ActiveSpace() &&
          fresh.ActiveSpace()->theme_hue == 307 && fresh.ActiveSpace()->theme_intensity == 100 &&
          fresh.ActiveSpace()->theme_mode == 0 && fresh.ActiveSpace()->theme_noise == 0, "a fresh workspace has one default space");
    metadata = store.SetTheme(145, 120, 2, 35);
    Check(metadata && metadata.snapshot.ActiveSpace()->theme_hue == 145 && metadata.snapshot.ActiveSpace()->theme_intensity == 120 &&
          metadata.snapshot.ActiveSpace()->theme_mode == 2 && metadata.snapshot.ActiveSpace()->theme_noise == 35 &&
          metadata.snapshot.revision == metadata_revision, "space theme persists without touching the tree revision");
    Check(store.SetTheme(360, 100, 0, 0).code == 400 && store.SetTheme(0, 161, 0, 0).code == 400 &&
          store.SetTheme(0, 100, 3, 0).code == 400 && store.SetTheme(0, 100, 0, 101).code == 400, "invalid themes rejected");
    Check(!metadata.snapshot.ActiveSpace()->theme_palette,"legacy theme has no palette after migration");
    red::ThemePalette palette;palette.count=3;palette.harmony=2;palette.fill=78;
    const auto own=fresh.active_space;
    const auto family="\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x91\xa7\xe2\x80\x8d\xf0\x9f\x91\xa6";
    red::TreeCommand icon_edit{red::TreeAction::EditSpace};icon_edit.id=own;icon_edit.title="Family";icon_edit.icon=family;
    Check(apply(icon_edit)&&store.Read().snapshot.ActiveSpace()->icon==family,"complete family emoji persists beyond the old16-byte limit");
    {red::TreeStore reopened;Check(reopened.Open(path)&&reopened.Read().snapshot.ActiveSpace()->icon==family,"family emoji survives a fresh connection");}
    for(const auto& bad:std::vector<std::string>{std::string(17,'x'),std::string(1,static_cast<char>(0xff)),"a b"}){icon_edit.icon=bad;Check(apply(icon_edit).code==400,"invalid/oversized space icons rejected");}
    const auto palette_revision=store.Read().snapshot.revision;metadata=store.SetTheme(145,42,2,35,own,palette);
    Check(metadata&&metadata.snapshot.ActiveSpace()->theme_palette==palette&&metadata.snapshot.revision==palette_revision,"palette persists without tree revision changes");
    c={red::TreeAction::Rename};c.id=tab;c.title="My name";
    Check(apply(c)&&store.Read().snapshot.ActiveSpace()->theme_palette==palette,"tree updates retain palette metadata");
    auto orbit=palette;orbit.version=2;orbit.circles={red::ThemeCircle{32,850,27},red::ThemeCircle{154,600,38},red::ThemeCircle{274,950,37}};
    orbit.rotation=237;
    metadata=store.SetTheme(32,42,1,31,own,orbit);
    Check(metadata&&store.Read().snapshot.ActiveSpace()->theme_palette==orbit,"independent circle palette round trips SQLite");
    Check(apply(c)&&store.Read().snapshot.ActiveSpace()->theme_palette==orbit,"tree writes retain independent circles");
    {red::TreeStore reopened;Check(reopened.Open(path)&&reopened.Read().snapshot.ActiveSpace()->theme_palette==orbit,"palette rotation survives a fresh SQLite connection");}
    palette.radius=951;Check(store.SetTheme(145,42,2,35,own,palette).code==400,"out of range palette rejected");
    Check(store.SetTheme(145,42,2,35,"00000000-0000-0000-0000-000000000000").code==404,"missing space cannot receive a palette");
    metadata=store.SetTheme(145,120,2,35,own);
    Check(metadata&&!metadata.snapshot.ActiveSpace()->theme_palette,"legacy reset clears the palette");
    c = {red::TreeAction::Rename}; c.id = tab; c.title = "My name";
    Check(apply(c) && Find(store.Read().snapshot, tab).custom_title, "explicit rename opts out of automatic naming");
    metadata = store.SetPageTitle(tab, home, "Changed website title");
    Check(metadata && Find(metadata.snapshot, tab).title == "My name" && Find(metadata.snapshot, tab).custom_title,
          "dynamic page title cannot overwrite the manual name");
    c = {red::TreeAction::AutomaticTitle}; c.id = tab;
    Check(apply(c) && !Find(store.Read().snapshot, tab).custom_title, "automatic website naming can be restored");
    const auto before = store.Read().snapshot;
    c = {red::TreeAction::Move}; c.id = folder; c.parent = nested;
    Check(apply(c).code == 400, "reject ancestry cycle");
    c.parent = folder; Check(apply(c).code == 400, "reject self parent");
    c.id = other; c.parent = tab; Check(apply(c).code == 400, "reject tab as parent");
    c.parent = "other-profile-id"; Check(apply(c).code == 400, "reject unknown/cross-profile parent");
    c.parent = folder; c.position = 99; Check(apply(c).code == 400, "reject invalid sibling index");
    c = {red::TreeAction::Activate}; c.id = folder; Check(apply(c).code == 400, "folder activation cannot create browser");
    c = {red::TreeAction::RemoveLeaf}; c.id = folder; Check(apply(c).code == 409, "nonempty folder deletion cannot cascade");
    c = {red::TreeAction::Rename}; c.id = tab; c.title = "stale"; c.expected_revision = before.revision - 1;
    Check(store.Apply(c).code == 409, "reject stale mutation");
    c.title = std::string("bad\0title", 9); Check(apply(c).code == 400, "reject embedded NUL");
    // Local files (file:///) are valid tabs; network file URLs are not.
    for (auto url : {"chrome://settings/", "file://server/share/win.ini", "chrome-extension://not-an-id/page.html",
                     "sol://ui/index.html", "sol://ui/history.html?x", "sol://ui/panel.html"}) {
      c = {red::TreeAction::CreateTab}; c.title = "Rejected"; c.url = url;
      Check(apply(c).code == 400, "reject internal, network file and malformed extension URLs");
    }
    const auto unchanged = store.Read().snapshot;
    Check(unchanged.nodes == before.nodes && unchanged.revision == before.revision, "all rejected commands leave database unchanged");
    c = {red::TreeAction::Move}; c.id = other; c.parent = nested; c.position = 0;
    r = apply(c); Check(static_cast<bool>(r) && Find(r.snapshot, other).position == 0 && Find(r.snapshot, tab).position == 1,
                          "atomic move and sibling reordering");
    sqlite3* injection = nullptr;
    sqlite3_open16(path.c_str(), &injection);
    sqlite3_exec(injection, "CREATE TRIGGER fail_write BEFORE UPDATE ON nodes BEGIN SELECT RAISE(ABORT,'injected failure'); END", nullptr, nullptr, nullptr);
    Check(store.SetPageTitle(tab, home, "unsaved").code == 500 && Find(store.Read().snapshot, tab).page_title == "Changed website title",
          "metadata write failure surfaces and retains committed title");
    const auto rollback_before = store.Read().snapshot;
    c = {red::TreeAction::Move}; c.id = tab; c.parent = "";
    Check(apply(c).code == 500, "surface failed database write");
    r = store.Read(); Check(r.snapshot.nodes == rollback_before.nodes && r.snapshot.revision == rollback_before.revision,
                            "failed transaction rolls back node order and revision");
    sqlite3_exec(injection, "DROP TRIGGER fail_write", nullptr, nullptr, nullptr); sqlite3_close(injection);
    c = {red::TreeAction::Place}; c.id = other; c.parent = ""; c.section = "today";
    r = apply(c); Check(static_cast<bool>(r) && !Find(r.snapshot, other).pinned && Find(r.snapshot, other).parent.empty(), "drop from folder into Today unpins and reparents atomically");
    Check(static_cast<bool>(store.SetPageTitle(other, home, "Old destination")), "save old Today title");
    red::TreeCommand change{red::TreeAction::SetUrl}; change.id = other; change.url = home + "new";
    r = apply(change);
    Check(r && Find(r.snapshot, other).page_title.empty(), "changing the destination discards its old title");
    c.section = "favorites"; c.url = "https://example.com/current";
    c.page_title = "New destination";
    Check(static_cast<bool>(store.SetPageTitle(other, change.url, "Pending Today title")), "save title before changing destination again");
    r = apply(c); Check(r && Find(r.snapshot, other).favorite && Find(r.snapshot, other).url == c.url &&
          Find(r.snapshot, other).page_title == "New destination", "favorite captures live URL and title atomically on promotion");
    r = store.SetPageTitle(other, change.url, "Late title from previous destination");
    Check(r && Find(r.snapshot, other).page_title == "New destination", "a delayed title write cannot change the new destination's title");
    c = {red::TreeAction::SetUrl}; c.id = other; c.url = "https://example.com/changed";
    Check(apply(c).code == 400, "navigation cannot overwrite persistent home URL");
    c = {red::TreeAction::Unload}; c.id = other;
    r = apply(c); Check(static_cast<bool>(r) && !Find(r.snapshot, other).archived && Find(r.snapshot, other).favorite, "unloading keeps favorite identity and home");
    c = {red::TreeAction::Archive}; c.id = other;
    r = apply(c); Check(static_cast<bool>(r) && Find(r.snapshot, other).archived && !Find(r.snapshot, other).favorite, "explicit removal goes to recoverable archive");
    Check(Find(r.snapshot, other).archived_at > 1700000000000LL, "archiving records when it happened (epoch ms)");
    c = {red::TreeAction::Activate}; c.id = other; Check(apply(c).code == 400, "archived entry cannot activate without restore");
    c = {red::TreeAction::Restore}; c.id = other;
    r = apply(c); Check(static_cast<bool>(r) && r.snapshot.active_id == other && !Find(r.snapshot, other).archived && !Find(r.snapshot, other).pinned, "restore retains identity and returns to Today");
    Check(Find(r.snapshot, other).archived_at == 0, "restore clears the archive time");
    c = {red::TreeAction::Place}; c.id = folder; c.section = "today";
    Check(apply(c).code == 400, "folder cannot become a Today tab");
    c = {red::TreeAction::CreateTab}; c.title = "Extension"; c.url = "chrome-extension://mnjggcdmjocbbbhaepdhchncahnbgone/help/index.html";
    r = apply(c); Check(static_cast<bool>(r) && Find(r.snapshot, r.created_id).url == c.url, "extension-opened page persists as a Today tab");
    c = {red::TreeAction::CreateTab}; c.title = "History"; c.url = "sol://ui/history.html";
    r = apply(c); Check(static_cast<bool>(r) && Find(r.snapshot, r.created_id).url == c.url, "exact history page address persists as a tab");
    {
      int roots = 0; bool first = false;
      for (const auto& n : r.snapshot.nodes) if (n.parent.empty()) { ++roots; if (n.id == r.created_id) first = n.position == 0; }
      std::vector<int> positions;
      for (const auto& n : r.snapshot.nodes) if (n.parent.empty()) positions.push_back(n.position);
      std::sort(positions.begin(), positions.end());
      bool dense = true; for (int i = 0; i < roots; ++i) dense = dense && positions[i] == i;
      Check(first && dense, "new Today tab opens first; root order stays dense");
    }
    {
      // A link's tab opens right below its opener; the next link from it goes below that one.
      const auto opener = r.created_id;
      c = {red::TreeAction::CreateTab}; c.title = "Link one"; c.url = "https://example.com/1"; c.after = opener;
      r = apply(c); const auto one = r.created_id;
      c = {red::TreeAction::CreateTab}; c.title = "Link two"; c.url = "https://example.com/2"; c.after = one;
      r = apply(c); const auto two = r.created_id;
      const int at = Find(r.snapshot, opener).position;
      Check(static_cast<bool>(r) && Find(r.snapshot, one).position == at + 1 && Find(r.snapshot, two).position == at + 2,
            "link tabs open below their opener in click order");
      std::vector<int> positions;
      for (const auto& n : r.snapshot.nodes) if (n.parent.empty()) positions.push_back(n.position);
      std::sort(positions.begin(), positions.end());
      bool dense = true; for (size_t i = 0; i < positions.size(); ++i) dense = dense && positions[i] == static_cast<int>(i);
      Check(dense, "root order stays dense after a link tab");
      c = {red::TreeAction::CreateTab}; c.title = "From pinned"; c.url = "https://example.com/3"; c.after = folder;
      r = apply(c); Check(static_cast<bool>(r) && Find(r.snapshot, r.created_id).position == 0, "an opener outside Today leaves the tab first");
    }
    c = {red::TreeAction::Activate}; c.id = tab; Check(static_cast<bool>(apply(c)), "restore active selection for restart");
    for (int i = 0; i < 13; ++i) {
      c = {red::TreeAction::CreateTab}; c.title = "Favorite cap"; c.url = "https://example.com/";
      r = apply(c); const auto id = r.created_id;
      c = {red::TreeAction::Place}; c.id = id; c.section = "favorites";
      r = apply(c); Check(i < 12 ? static_cast<bool>(r) : r.code == 400, "twelve-favorite limit is enforced natively");
    }
    expected = store.Read().snapshot;
  }
  {
    red::TreeStore store;
    const auto r = store.Open(path);
    Check(static_cast<bool>(r) && r.snapshot.nodes == expected.nodes && r.snapshot.active_id == tab && r.snapshot.revision == expected.revision &&
          r.snapshot.sidebar_width == 336 && r.snapshot.ActiveSpace() && r.snapshot.ActiveSpace()->theme_hue == 145 &&
          r.snapshot.ActiveSpace()->theme_noise == 35,
          "restart restores exact tree, page metadata, width, theme and active ID");
    Check(Find(r.snapshot, tab).pinned && !Find(r.snapshot, folder).expanded, "saved flags restored");
  }
  {
    // Spaces: own Pinned/Today, shared Favorites, remembered page, move, delete.
    const auto spaces_path = path.wstring() + L"-spaces";
    {
    red::TreeStore store; auto opened = store.Open(spaces_path);
    Check(static_cast<bool>(opened), "open spaces fixture");
    auto apply = [&](red::TreeCommand c) { c.expected_revision = store.Read().snapshot.revision; return store.Apply(c); };
    const auto home = store.Read().snapshot.active_space;
    red::TreeCommand c{red::TreeAction::CreateTab}; c.title = "Home page"; c.url = "https://example.com/home"; c.value = true;
    const auto home_tab = apply(c).created_id;
    c = {red::TreeAction::CreateTab}; c.title = "Shared"; c.url = "https://example.com/fav";
    const auto favorite = apply(c).created_id;
    c = {red::TreeAction::Place}; c.id = favorite; c.section = "favorites"; apply(c);
    c = {red::TreeAction::CreateSpace}; c.title = "Work"; c.icon = "💼";
    auto r = apply(c); const auto work = r.created_id;
    Check(r && r.snapshot.active_space == work && r.snapshot.active_id.empty() && r.snapshot.spaces.size() == 2 &&
          r.snapshot.ActiveSpace()->icon == "💼" && r.snapshot.ActiveSpace()->theme_hue != r.snapshot.spaces[0].theme_hue,
          "a new space opens empty with its own color");
    c = {red::TreeAction::CreateTab}; c.title = "Work page"; c.url = "https://example.com/work"; c.value = true;
    r = apply(c); const auto work_tab = r.created_id;
    Check(r && Find(r.snapshot, work_tab).space == work && Find(r.snapshot, home_tab).space == home &&
          Find(r.snapshot, favorite).space.empty(), "new tabs join the active space; Favorites belong to none");
    c = {red::TreeAction::SwitchSpace}; c.id = home; r = apply(c);
    Check(r && r.snapshot.active_space == home && r.snapshot.active_id == home_tab, "switching returns to the page last shown there");
    c = {red::TreeAction::SwitchSpace}; c.id = work; c.live_nodes.emplace(); r = apply(c);
    Check(r && r.snapshot.active_space == work && r.snapshot.active_id.empty(), "with nothing loaded a space opens on the empty tab");
    c = {red::TreeAction::SwitchSpace}; c.id = home; c.live_nodes = std::vector<std::string>{home_tab}; r = apply(c);
    Check(r && r.snapshot.active_id == home_tab, "a loaded page is still where the space opens");
    c = {red::TreeAction::SwitchSpace}; c.id = work; c.live_nodes = std::vector<std::string>{}; r = apply(c);
    Check(r && r.snapshot.active_id.empty(), "the remembered page is kept for later but not loaded");
    c = {red::TreeAction::SwitchSpace}; c.id = home; r = apply(c);
    c = {red::TreeAction::Activate}; c.id = favorite; r = apply(c);
    Check(r && r.snapshot.active_space == home, "a Favorite opens in the current space");
    c = {red::TreeAction::Activate}; c.id = work_tab; r = apply(c);
    Check(r && r.snapshot.active_space == work, "showing another space's page enters that space");
    c = {red::TreeAction::MoveToSpace}; c.id = home_tab; c.space = work; r = apply(c);
    Check(r && Find(r.snapshot, home_tab).space == work, "a tab moves to another space");
    c = {red::TreeAction::MoveToSpace}; c.id = favorite; c.space = home;
    Check(apply(c).code == 400, "Favorites do not move between spaces");
    c = {red::TreeAction::EditSpace}; c.id = work; c.title = "Job"; c.icon = "J"; r = apply(c);
    Check(r && r.snapshot.ActiveSpace()->name == "Job", "rename a space");
    c = {red::TreeAction::EditSpace}; c.id = work; c.title = "  "; c.icon = "";
    Check(apply(c).code == 400, "a space needs a name");
    c = {red::TreeAction::MoveSpace}; c.id = work; c.position = 0; r = apply(c);
    Check(r && r.snapshot.spaces[0].id == work && r.snapshot.spaces[0].position == 0, "reorder spaces");
    c = {red::TreeAction::DeleteSpace}; c.id = work; r = apply(c);
    Check(r && r.snapshot.spaces.size() == 1 && r.snapshot.active_space == home && Find(r.snapshot, work_tab).id.empty() &&
          Find(r.snapshot, home_tab).id.empty() && !Find(r.snapshot, favorite).id.empty(),
          "deleting a space removes its entries, keeps Favorites and lands in a remaining space");
    c = {red::TreeAction::DeleteSpace}; c.id = home;
    Check(apply(c).code == 409, "the last space stays");
    }
    for (const auto& suffix : {L"", L"-wal", L"-shm"}) { std::error_code error; std::filesystem::remove(spaces_path + suffix, error); }
  }
  // Native Peek promotion adopts a live browser identity once, without remapping it.
  const auto peek_path=path.wstring()+L"-peek";
  {
    red::TreeStore store; auto result=store.Open(peek_path);
    red::TreeCommand command{red::TreeAction::CreateTab}; command.expected_revision=result.snapshot.revision;
    command.id="f22dc282-4817-4b71-bc6d-98857c97ca55"; command.url="https://example.com/peek"; command.title="Live preview"; command.value=true;
    result=store.Apply(command);
    Check(result && result.created_id==command.id && result.snapshot.active_id==command.id && !Find(result.snapshot,command.id).pinned,
      "Peek promotion creates one Today entry with the existing live identity");
    command.expected_revision=result.snapshot.revision;
    const auto before=store.Read().snapshot;
    Check(store.Apply(command).code==400 && store.Read().snapshot.nodes==before.nodes && store.Read().snapshot.revision==before.revision,
      "duplicate Peek adoption is rejected atomically");
    command={red::TreeAction::CreateTab}; command.expected_revision=before.revision; command.title="Second preview";
    command.id="d53dd772-9cb1-4f08-8b63-ecfb526cb953"; command.url="https://example.com/second"; command.value=true;
    command.split_with=before.active_id;
    result=store.Apply(command);
    Check(result && result.snapshot.splits.size()==1 && result.snapshot.splits[0].anchor==before.active_id &&
      result.snapshot.splits[0].other==command.id && result.snapshot.active_id==command.id,"Peek adoption and split creation commit together");
    const auto split_before=store.Read().snapshot;
    command.expected_revision=split_before.revision; command.id="2270beb4-82bf-4f32-8d05-dd9300cbf006";
    Check(store.Apply(command).code==409 && store.Read().snapshot.nodes==split_before.nodes && store.Read().snapshot.revision==split_before.revision,
      "raced source split rejects Peek creation without leaving an orphan Today entry");
    command={red::TreeAction::CreateTab}; command.expected_revision=split_before.revision; command.title="Pinned original"; command.url="https://example.com/home";
    result=store.Apply(command); const auto persistent=result.created_id;
    command={red::TreeAction::Pin}; command.expected_revision=result.snapshot.revision; command.id=persistent; command.value=true; result=store.Apply(command);
    command={red::TreeAction::CreateTab}; command.expected_revision=result.snapshot.revision; command.id="27716aca-a501-4afb-b896-79398529a452";
    command.url="https://example.com/peek-copy"; command.title="Peek copy"; command.value=true; command.split_with=persistent;
    command.split_source_url="https://example.com/current"; command.split_source_title="Pinned current"; result=store.Apply(command);
    Check(result && Find(result.snapshot,persistent).pinned && Find(result.snapshot,persistent).url=="https://example.com/home" &&
      result.snapshot.splits.back().anchor!=persistent && !Find(result.snapshot,result.snapshot.splits.back().anchor).pinned &&
      Find(result.snapshot,result.snapshot.splits.back().anchor).url=="https://example.com/current" && result.snapshot.splits.back().other==command.id,
      "Pinned Peek split creates two Today members and keeps the original home unchanged");
  }
  for (const auto& suffix:{L"",L"-wal",L"-shm"}) { std::error_code error; std::filesystem::remove(peek_path+suffix,error); }
  // Split membership is separate from tab identity and original placement.
  const auto split_path = path.wstring() + L"-splits";
  std::string split_first, split_second;
  red::SplitGroup split_saved;
  {
    red::TreeStore store; Check(static_cast<bool>(store.Open(split_path)), "split database opens");
    auto apply = [&](red::TreeCommand c) { c.expected_revision = store.Read().snapshot.revision; return store.Apply(c); };
    red::TreeCommand c{red::TreeAction::CreateTab}; c.title = "First"; c.url = "https://example.com/a";
    auto r = apply(c); split_first = r.created_id;
    c.title = "Second"; c.url = "https://example.com/b"; r = apply(c); split_second = r.created_id;
    c = {red::TreeAction::Pin}; c.id = split_first; c.value = true; Check(static_cast<bool>(apply(c)), "split anchor can be pinned");
    const auto original = store.Read().snapshot.nodes;
    c = {red::TreeAction::CreateSplit}; c.id = split_first; c.parent = split_first;
    Check(apply(c).code == 400 && store.Read().snapshot.splits.empty(), "self split rejected without partial write");
    c.parent = split_second; c.reversed = true; r = apply(c);
    Check(r && r.snapshot.splits.size() == 1 && r.snapshot.nodes == original && r.snapshot.splits[0].reversed,
          "split references original pages without moving or renaming them");
    Check(apply(c).code == 409, "duplicate split membership rejected");
    c = {red::TreeAction::LayoutSplit}; c.id = split_first; c.position = 9999;
    const auto before = store.Read().snapshot;
    Check(apply(c).code == 400 && store.Read().snapshot.revision == before.revision && store.Read().snapshot.splits == before.splits,
          "invalid split ratio rolls back atomically");
    c.position = 2500; c.value = true; r = apply(c);
    Check(r && r.snapshot.splits[0].ratio == 2500 && r.snapshot.splits[0].vertical, "split ratio and direction save together");
    c = {red::TreeAction::SwapSplit}; c.id = split_first; r = apply(c);
    Check(r && r.snapshot.splits[0].ratio == 7500 && !r.snapshot.splits[0].reversed, "swap preserves each page allocation");
    c = {red::TreeAction::Rename}; c.id = split_first; c.title = "First renamed"; apply(c);
    c.id = split_second; c.title = "Second renamed"; r = apply(c);
    Check(r && Find(r.snapshot,split_first).title == "First renamed" && Find(r.snapshot,split_second).title == "Second renamed",
          "split pages have independent real tab titles");
    if (r && r.snapshot.splits.size() == 1) split_saved = r.snapshot.splits[0];
    else Check(false,"split remains available for reopen check");
  }
  {
    red::TreeStore store; auto r = store.Open(split_path);
    Check(r && r.snapshot.splits.size() == 1 && r.snapshot.splits[0] == split_saved, "split membership and layout survive reopen");
    auto apply = [&](red::TreeCommand c) { c.expected_revision = store.Read().snapshot.revision; return store.Apply(c); };
    red::TreeCommand move{red::TreeAction::CreateFolder}; move.title = "Split container";
    r = apply(move); const auto container = r.created_id;
    move = {red::TreeAction::Place}; move.id = split_first; move.parent = container; move.section = "pinned";
    r = apply(move);
    Check(r && r.snapshot.splits.size() == 1 && Find(r.snapshot,split_first).parent == container,
          "nesting a split moves its anchor without separating pages");
    const auto before_favorite = store.Read().snapshot;
    move.parent.clear(); move.section = "favorites";
    Check(apply(move).code == 400 && store.Read().snapshot.revision == before_favorite.revision &&
          store.Read().snapshot.splits == before_favorite.splits,"Favorites conversion rejects atomically without splitting pages");
    move = {red::TreeAction::CreateSpace}; move.title = "Split destination";
    r = apply(move); const auto destination = r.created_id;
    move = {red::TreeAction::MoveToSpace}; move.id = container; move.space = destination; r = apply(move);
    Check(r && r.snapshot.splits.size() == 1 && Find(r.snapshot,split_first).parent == container &&
          Find(r.snapshot,split_second).parent.empty() && Find(r.snapshot,split_second).space == destination,
          "moving a containing folder across spaces carries its split partner atomically");
    red::TreeCommand c{red::TreeAction::RemoveSplit}; c.id = split_first; r = apply(c);
    Check(r && r.snapshot.splits.empty() && Find(r.snapshot,split_first).title == "First renamed" &&
          Find(r.snapshot,split_second).title == "Second renamed" && !Find(r.snapshot,split_second).archived,
          "unsplit keeps both pages and independent names");
    c = {red::TreeAction::CreateSplit}; c.id = split_first; c.parent = split_second; apply(c);
    c = {red::TreeAction::Unload}; c.id = split_first; c.next_active = split_second; r = apply(c);
    Check(r && r.snapshot.splits.empty() && Find(r.snapshot,split_first).pinned && !Find(r.snapshot,split_second).archived,
          "confirmed pin unload dissolves group but preserves both entries");
    c = {red::TreeAction::CreateSplit}; c.id = split_first; c.parent = split_second; apply(c);
    c = {red::TreeAction::Archive}; c.id = split_second; c.next_active = split_first; r = apply(c);
    Check(r && r.snapshot.splits.empty() && Find(r.snapshot,split_second).archived && !Find(r.snapshot,split_first).archived,
          "archiving one pane preserves its partner and removes invalid group");
  }
  for (const auto& suffix : {L"",L"-wal",L"-shm"}) { std::error_code error; std::filesystem::remove(split_path + suffix,error); }
  const auto archive_path=path.wstring()+L"-auto-archive";
  std::string archive_first,archive_second; red::SplitGroup archived_layout;
  const auto archive_now=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
  {
    red::TreeStore store; Check(static_cast<bool>(store.Open(archive_path)),"auto-archive store opens");
    auto apply=[&](red::TreeCommand command) { command.expected_revision=store.Read().snapshot.revision; return store.Apply(command); };
    red::TreeCommand command{red::TreeAction::CreateTab}; command.title="Archive first"; command.url="https://example.com/first";
    archive_first=apply(command).created_id; command.title="Archive second"; command.url="https://example.com/second"; archive_second=apply(command).created_id;
    command={red::TreeAction::CreateSplit}; command.id=archive_first; command.parent=archive_second; command.reversed=true; apply(command);
    command={red::TreeAction::LayoutSplit}; command.id=archive_first; command.position=3500; command.value=true;
    auto result=apply(command); if (result && !result.snapshot.splits.empty()) archived_layout=result.snapshot.splits[0];
    const auto revision=result.snapshot.revision;
    result=store.TouchTabs({{archive_first,archive_now-13LL*3600000},{archive_second,archive_now-13LL*3600000}});
    Check(result && result.snapshot.revision==revision,"activity persistence leaves structural revision unchanged");
    command={red::TreeAction::AutoArchive}; command.id=archive_first; command.not_after=archive_now-12LL*3600000;
    result=apply(command);
    Check(result && result.snapshot.splits.empty() && result.snapshot.archived_splits.size()==1 &&
      Find(result.snapshot,archive_first).archived && Find(result.snapshot,archive_second).archived,"idle Today group archives atomically with layout metadata");
  }
  {
    red::TreeStore store; auto result=store.Open(archive_path);
    Check(result && result.snapshot.archived_splits.size()==1 && result.snapshot.archived_splits[0]==archived_layout,"archived split survives restart");
    auto apply=[&](red::TreeCommand command) { command.expected_revision=store.Read().snapshot.revision; return store.Apply(command); };
    red::TreeCommand command{red::TreeAction::Restore}; command.id=archive_second; result=apply(command);
    Check(result && result.snapshot.archived_splits.empty() && result.snapshot.splits.size()==1 &&
      result.snapshot.splits[0]==archived_layout && !Find(result.snapshot,archive_first).archived &&
      Find(result.snapshot,archive_second).last_active_at>archive_now-3600000,"restoring either member restores the whole group and renews its time");
    const auto revision=result.snapshot.revision;
    command={red::TreeAction::AutoArchive}; command.id=archive_first; command.not_after=archive_now-12LL*3600000;
    Check(apply(command).code==409 && store.Read().snapshot.revision==revision,"recent restored tabs reject stale expiry atomically");
    store.TouchTabs({{archive_first,archive_now-13LL*3600000},{archive_second,archive_now-13LL*3600000}});
    command={red::TreeAction::Pin}; command.id=archive_second; command.value=true; apply(command);
    command={red::TreeAction::AutoArchive}; command.id=archive_first; command.not_after=archive_now-12LL*3600000;
    Check(apply(command).code==409 && store.Read().snapshot.splits.size()==1,"mixed Pinned/Today split is exempt");
  }
  for (const auto& suffix:{L"",L"-wal",L"-shm"}) { std::error_code error; std::filesystem::remove(archive_path+suffix,error); }
  wchar_t exe[MAX_PATH]; GetModuleFileNameW(nullptr, exe, MAX_PATH);
  std::wstring command = L"\"" + std::wstring(exe) + L"\" --crash-write \"" + path.wstring() + L"\"";
  STARTUPINFOW startup{}; startup.cb = sizeof(startup); PROCESS_INFORMATION process{};
  if (CreateProcessW(exe, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process)) {
    auto wait = WaitForSingleObject(process.hProcess, 5000);
    if (wait != WAIT_OBJECT_0) TerminateProcess(process.hProcess, 89);
    DWORD code = 0; GetExitCodeProcess(process.hProcess, &code);
    Check(code == 77, "interrupted writer executed");
    CloseHandle(process.hThread); CloseHandle(process.hProcess);
    red::TreeStore store; const auto r = store.Open(path);
    Check(static_cast<bool>(r) && r.snapshot.nodes == expected.nodes && r.snapshot.revision == expected.revision,
          "recover database after process exit in uncommitted write");
  } else Check(false, "launch interrupted writer");
  for (const auto& suffix : {L"", L"-wal", L"-shm"}) {
    std::error_code error; std::filesystem::remove(path.wstring() + suffix, error);
  }
  {
    red::TreeStore store;auto start=store.Open(":memory:");const auto first=start.snapshot.active_space;
    red::TreeCommand create{red::TreeAction::CreateSpace};create.expected_revision=start.snapshot.revision;create.title="Palette scope";
    const auto second=store.Apply(create);Check(second&&second.snapshot.active_space!=first,"palette scope fixture switches space");
    red::ThemePalette palette;palette.count=2;palette.fill=47;
    const auto saved=store.SetTheme(40,42,1,11,first,palette);
    const auto old=std::find_if(saved.snapshot.spaces.begin(),saved.snapshot.spaces.end(),[&](const auto& item){return item.id==first;});
    Check(saved&&saved.snapshot.active_space==second.snapshot.active_space&&!saved.snapshot.ActiveSpace()->theme_palette&&
      old!=saved.snapshot.spaces.end()&&old->theme_palette==palette,"late explicit-space save leaves the active space untouched");
  }
  std::printf("Workspace storage tests: %s\n", failures ? "FAIL" : "PASS");
  return failures ? 1 : 0;
}
