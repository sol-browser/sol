#include "storage/history_store.h"
#include <sqlite3.h>

namespace red {
namespace {
constexpr int kApplicationId = 0x52454448;  // "REDH"
constexpr int64_t kCollapseMs = 30LL * 60 * 1000;
constexpr char kPruneFavicons[] = "DELETE FROM favicons WHERE host NOT IN (SELECT host FROM favicons ORDER BY updated_at DESC LIMIT 1000)";
bool ValidFaviconRecord(const std::string& host, const std::string& png) {
  return !host.empty() && host.size() <= 253 && !png.empty() && png.size() <= 16 * 1024;
}
class Statement {
 public:
  Statement(sqlite3* db, const char* sql) { sqlite3_prepare_v2(db, sql, -1, &value, nullptr); }
  ~Statement() { sqlite3_finalize(value); }
  Statement(const Statement&) = delete;
  Statement& operator=(const Statement&) = delete;
  void Text(int index, const std::string& text) { sqlite3_bind_text(value, index, text.c_str(), static_cast<int>(text.size()), SQLITE_TRANSIENT); }
  void Int(int index, int64_t number) { sqlite3_bind_int64(value, index, number); }
  bool Row() { return value && sqlite3_step(value) == SQLITE_ROW; }
  bool Done() { return value && sqlite3_step(value) == SQLITE_DONE; }
  std::string Column(int index) {
    const auto* text = reinterpret_cast<const char*>(sqlite3_column_text(value, index));
    return text ? text : "";
  }
  int64_t Number(int index) { return sqlite3_column_int64(value, index); }
  sqlite3_stmt* value = nullptr;
};
// Escapes LIKE wildcards so the query is matched as plain text.
std::string Pattern(const std::string& query) {
  std::string out = "%";
  for (const char c : query) { if (c == '%' || c == '_' || c == '\\') out.push_back('\\'); out.push_back(c); }
  return out + "%";
}
Visit ReadVisit(Statement& s) { return {s.Number(0), s.Column(1), s.Column(2), s.Number(3)}; }
}  // namespace

HistoryStore::~HistoryStore() { if (db_) sqlite3_close(db_); }
bool HistoryStore::Exec(const char* sql) { return sqlite3_exec(db_, sql, nullptr, nullptr, nullptr) == SQLITE_OK; }
bool HistoryStore::Open(const std::filesystem::path& path) {
  if (db_) return false;
  const auto encoded = path.u8string();
  if (sqlite3_open_v2(reinterpret_cast<const char*>(encoded.c_str()), &db_,
      SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr) != SQLITE_OK) return false;
  sqlite3_busy_timeout(db_, 1500);
  if (!Exec("PRAGMA trusted_schema=OFF; PRAGMA journal_mode=WAL; PRAGMA synchronous=NORMAL;")) return false;
  int schema = -1, application = -1;
  { Statement v(db_, "PRAGMA user_version"); if (v.Row()) schema = static_cast<int>(v.Number(0)); }
  { Statement a(db_, "PRAGMA application_id"); if (a.Row()) application = static_cast<int>(a.Number(0)); }
  if (schema < 0 || application < 0 || schema > 2 || (application != 0 && application != kApplicationId)) return false;
  if (schema == 0) {
    { Statement t(db_, "SELECT count(*) FROM sqlite_schema WHERE name NOT LIKE 'sqlite_%'");
      if (!t.Row() || t.Number(0) != 0) return false; }  // Never adopt an unrelated database.
    if (!Exec("BEGIN IMMEDIATE;"
        "CREATE TABLE visits(id INTEGER PRIMARY KEY, url TEXT NOT NULL, title TEXT NOT NULL, visited_at INTEGER NOT NULL);"
        "CREATE INDEX visits_time ON visits(visited_at); CREATE INDEX visits_url ON visits(url, visited_at);"
        "CREATE TABLE downloads(id INTEGER PRIMARY KEY, name TEXT NOT NULL, path TEXT NOT NULL, origin TEXT NOT NULL,"
        " state TEXT NOT NULL CHECK(state IN('complete','canceled','interrupted')), received INTEGER NOT NULL,"
        " total INTEGER NOT NULL, finished_at INTEGER NOT NULL);"
        "PRAGMA application_id=1380271176; PRAGMA user_version=1; COMMIT;")) { Exec("ROLLBACK"); return false; }
  } else if (application != kApplicationId) return false;
  // v2: site icons survive restarts.
  if (schema < 2 && !Exec("BEGIN IMMEDIATE;"
      "CREATE TABLE favicons(host TEXT PRIMARY KEY, png BLOB NOT NULL, updated_at INTEGER NOT NULL);"
      "CREATE INDEX favicons_time ON favicons(updated_at); PRAGMA user_version=2; COMMIT;")) { Exec("ROLLBACK"); return false; }
  return true;
}
int64_t HistoryStore::AddVisit(const std::string& url, const std::string& title, int64_t now) {
  if (!db_) return 0;
  {
    Statement last(db_, "SELECT id, url, visited_at FROM visits ORDER BY visited_at DESC, id DESC LIMIT 1");
    if (last.Row() && last.Column(1) == url && now - last.Number(2) < kCollapseMs) {
      const int64_t id = last.Number(0);
      Statement update(db_, "UPDATE visits SET visited_at=?1, title=CASE WHEN ?2='' THEN title ELSE ?2 END WHERE id=?3");
      update.Int(1, now); update.Text(2, title); update.Int(3, id);
      return update.Done() ? id : 0;
    }
  }
  Statement insert(db_, "INSERT INTO visits(url, title, visited_at) VALUES(?1, ?2, ?3)");
  insert.Text(1, url); insert.Text(2, title); insert.Int(3, now);
  return insert.Done() ? sqlite3_last_insert_rowid(db_) : 0;
}
int HistoryStore::ImportVisits(const std::vector<Visit>& visits) {
  if (!db_ || !Exec("BEGIN IMMEDIATE")) return -1;
  Statement put(db_, "INSERT INTO visits(url,title,visited_at) SELECT ?1,?2,?3 WHERE NOT EXISTS(SELECT 1 FROM visits WHERE url=?1 AND visited_at=?3)");
  if (!put.value) { Exec("ROLLBACK"); return -1; }
  int count = 0;
  for (const auto& v : visits) {
    put.Text(1,v.url); put.Text(2,v.title); put.Int(3,v.visited_at);
    if (!put.Done()) { Exec("ROLLBACK"); return -1; }
    count += sqlite3_changes(db_); sqlite3_reset(put.value);
  }
  if (!Exec("COMMIT")) { Exec("ROLLBACK"); return -1; } return count;
}
void HistoryStore::UpdateTitle(const std::string& url, const std::string& title) {
  if (!db_ || title.empty()) return;
  Statement s(db_, "UPDATE visits SET title=?1 WHERE id=(SELECT id FROM visits WHERE url=?2 ORDER BY visited_at DESC LIMIT 1)");
  s.Text(1, title); s.Text(2, url); s.Done();
}
std::vector<Visit> HistoryStore::Visits(int64_t before, int limit, const std::string& query) {
  std::vector<Visit> out;
  if (!db_) return out;
  Statement s(db_, "SELECT id, url, title, visited_at FROM visits WHERE (?1=0 OR visited_at<?1)"
      " AND (?2='' OR title LIKE ?3 ESCAPE '\\' OR url LIKE ?3 ESCAPE '\\') ORDER BY visited_at DESC, id DESC LIMIT ?4");
  s.Int(1, before); s.Text(2, query); s.Text(3, Pattern(query)); s.Int(4, limit);
  while (s.Row()) out.push_back(ReadVisit(s));
  return out;
}
std::vector<VisitDay> HistoryStore::Days(int limit, const std::string& query) {
  std::vector<VisitDay> out;
  if (!db_) return out;
  Statement s(db_, "SELECT date(visited_at/1000,'unixepoch','localtime') AS day, count(*), max(visited_at) FROM visits"
      " WHERE (?1='' OR title LIKE ?2 ESCAPE '\\' OR url LIKE ?2 ESCAPE '\\') GROUP BY day ORDER BY day DESC LIMIT ?3");
  s.Text(1, query); s.Text(2, Pattern(query)); s.Int(3, limit);
  while (s.Row()) out.push_back({s.Column(0), s.Number(1), s.Number(2)});
  return out;
}
std::vector<Visit> HistoryStore::Suggest(const std::string& query, int limit) {
  std::vector<Visit> out;
  if (!db_ || query.empty()) return out;
  Statement s(db_, "SELECT id, url, title, max(visited_at) AS last FROM visits"
      " WHERE title LIKE ?1 ESCAPE '\\' OR url LIKE ?1 ESCAPE '\\' GROUP BY url ORDER BY last DESC LIMIT ?2");
  s.Text(1, Pattern(query)); s.Int(2, limit);
  while (s.Row()) out.push_back(ReadVisit(s));
  return out;
}
bool HistoryStore::DeleteVisit(int64_t id) {
  if (!db_) return false;
  Statement s(db_, "DELETE FROM visits WHERE id=?1"); s.Int(1, id);
  return s.Done() && sqlite3_changes(db_) == 1;
}
int HistoryStore::DeleteRange(int64_t from, int64_t to) {
  if (!db_) return 0;
  Statement s(db_, "DELETE FROM visits WHERE visited_at>=?1 AND visited_at<?2"); s.Int(1, from); s.Int(2, to);
  return s.Done() ? sqlite3_changes(db_) : 0;
}
void HistoryStore::Prune(int64_t older_than) {
  if (!db_) return;
  Statement s(db_, "DELETE FROM visits WHERE visited_at<?1"); s.Int(1, older_than); s.Done();
}
int64_t HistoryStore::SaveDownload(const DownloadRecord& r) {
  if (!db_) return 0;
  // id 0 inserts a new row (store-assigned ID; CEF IDs restart every session).
  Statement s(db_, "INSERT OR REPLACE INTO downloads(id, name, path, origin, state, received, total, finished_at)"
      " VALUES(nullif(?1, 0), ?2, ?3, ?4, ?5, ?6, ?7, ?8)");
  const auto path = r.path.u8string();
  s.Int(1, r.id); s.Text(2, r.name); s.Text(3, std::string(path.begin(), path.end())); s.Text(4, r.origin);
  s.Text(5, r.state); s.Int(6, r.received); s.Int(7, r.total); s.Int(8, r.finished_at);
  if (!s.Done()) return 0;
  const int64_t id = r.id ? r.id : sqlite3_last_insert_rowid(db_);
  // Bounded; the popover shows the newest 30, the downloads page all of these.
  Exec("DELETE FROM downloads WHERE id NOT IN (SELECT id FROM downloads ORDER BY finished_at DESC, id DESC LIMIT 1000)");
  return id;
}
std::vector<DownloadRecord> HistoryStore::Downloads(int limit) {
  std::vector<DownloadRecord> out;
  if (!db_) return out;
  Statement s(db_, "SELECT id, name, path, origin, state, received, total, finished_at FROM downloads"
      " ORDER BY finished_at DESC, id DESC LIMIT ?1");
  s.Int(1, limit);
  while (s.Row()) {
    const auto path = s.Column(2);
    out.push_back({s.Number(0), s.Column(1), s.Column(3), s.Column(4),
                   std::filesystem::path(std::u8string(path.begin(), path.end())), s.Number(5), s.Number(6), s.Number(7)});
  }
  return out;
}
std::vector<DownloadRecord> HistoryStore::DownloadsBefore(int64_t before, int limit) {
  std::vector<DownloadRecord> out;
  if (!db_) return out;
  Statement s(db_, "SELECT id, name, path, origin, state, received, total, finished_at FROM downloads"
      " WHERE (?1=0 OR finished_at<?1) ORDER BY finished_at DESC, id DESC LIMIT ?2");
  s.Int(1, before); s.Int(2, limit);
  while (s.Row()) {
    const auto path = s.Column(2);
    out.push_back({s.Number(0), s.Column(1), s.Column(3), s.Column(4),
                   std::filesystem::path(std::u8string(path.begin(), path.end())), s.Number(5), s.Number(6), s.Number(7)});
  }
  return out;
}
std::optional<DownloadRecord> HistoryStore::Download(int64_t id) {
  if (!db_) return std::nullopt;
  Statement s(db_, "SELECT id, name, path, origin, state, received, total, finished_at FROM downloads WHERE id=?1"); s.Int(1, id);
  if (!s.Row()) return std::nullopt;
  const auto path = s.Column(2);
  return DownloadRecord{s.Number(0), s.Column(1), s.Column(3), s.Column(4),
                        std::filesystem::path(std::u8string(path.begin(), path.end())), s.Number(5), s.Number(6), s.Number(7)};
}
void HistoryStore::DeleteDownload(int64_t id) {
  if (!db_) return;
  Statement s(db_, "DELETE FROM downloads WHERE id=?1"); s.Int(1, id); s.Done();
}
void HistoryStore::ClearDownloads() { if (db_) Exec("DELETE FROM downloads"); }
void HistoryStore::SaveFavicon(const std::string& host, const std::string& png, int64_t now) {
  if (!db_ || !ValidFaviconRecord(host, png)) return;
  Statement s(db_, "INSERT OR REPLACE INTO favicons(host, png, updated_at) VALUES(?1, ?2, ?3)");
  s.Text(1, host); sqlite3_bind_blob(s.value, 2, png.data(), static_cast<int>(png.size()), SQLITE_TRANSIENT); s.Int(3, now);
  if (!s.Done()) return;
  Exec(kPruneFavicons);
}
std::optional<std::map<std::string, std::string>> HistoryStore::ImportFavicons(std::map<std::string, std::string> icons, int64_t now) {
  if (!db_ || !Exec("BEGIN IMMEDIATE")) return std::nullopt;
  Statement put(db_, "INSERT INTO favicons(host,png,updated_at) VALUES(?1,?2,?3) ON CONFLICT(host) DO NOTHING");
  if (!put.value) { Exec("ROLLBACK"); return std::nullopt; }
  for (auto it = icons.begin(); it != icons.end();) {
    const auto& [host, png] = *it;
    if (!ValidFaviconRecord(host, png)) { it = icons.erase(it); continue; }
    put.Text(1, host);
    sqlite3_bind_blob(put.value, 2, png.data(), static_cast<int>(png.size()), SQLITE_TRANSIENT);
    put.Int(3, now);
    if (!put.Done()) { Exec("ROLLBACK"); return std::nullopt; }
    const bool added = sqlite3_changes(db_) != 0;
    sqlite3_reset(put.value);
    if (added) ++it; else it = icons.erase(it);
  }
  if ((!icons.empty() && !Exec(kPruneFavicons)) || !Exec("COMMIT")) { Exec("ROLLBACK"); return std::nullopt; }
  return icons;
}
std::vector<std::pair<std::string, std::string>> HistoryStore::Favicons(int limit) {
  std::vector<std::pair<std::string, std::string>> out;
  if (!db_) return out;
  Statement s(db_, "SELECT host, png FROM favicons ORDER BY updated_at DESC LIMIT ?1"); s.Int(1, limit);
  while (s.Row()) {
    const auto* bytes = static_cast<const char*>(sqlite3_column_blob(s.value, 1));
    out.emplace_back(s.Column(0), std::string(bytes ? bytes : "", static_cast<size_t>(sqlite3_column_bytes(s.value, 1))));
  }
  return out;
}
}  // namespace red
