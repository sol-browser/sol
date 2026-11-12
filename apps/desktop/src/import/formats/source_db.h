#pragma once
#include <sqlite3.h>
#include <cstdint>
#include <filesystem>
#include <string>

namespace red::importer {
// Read-only SQLite access to another browser's database, inside one read
// transaction so the rows are consistent. The source file is never modified.
class SourceDb {
 public:
  explicit SourceDb(const std::filesystem::path& path) {
    const auto utf = path.u8string();
    if (sqlite3_open_v2(reinterpret_cast<const char*>(utf.c_str()), &db, SQLITE_OPEN_READONLY | SQLITE_OPEN_NOMUTEX, nullptr) != SQLITE_OK) { sqlite3_close(db); db = nullptr; }
    if (db) { sqlite3_busy_timeout(db, 1500); sqlite3_exec(db, "PRAGMA query_only=ON; BEGIN", nullptr, nullptr, nullptr); }
  }
  ~SourceDb() { if (db) sqlite3_close(db); }
  SourceDb(const SourceDb&) = delete;
  SourceDb& operator=(const SourceDb&) = delete;
  sqlite3* db = nullptr;
};
class Query {
 public:
  Query(sqlite3* db, const char* sql) { if (db) sqlite3_prepare_v2(db, sql, -1, &s, nullptr); }
  ~Query() { sqlite3_finalize(s); }
  Query(const Query&) = delete;
  Query& operator=(const Query&) = delete;
  bool Row() {
    if (!s) return false;
    code = sqlite3_step(s);
    return code == SQLITE_ROW;
  }
  bool Done() const { return s && code == SQLITE_DONE; }
  std::string Text(int col) { const auto* p = sqlite3_column_blob(s, col); return p ? std::string(static_cast<const char*>(p), sqlite3_column_bytes(s, col)) : ""; }
  int64_t Number(int col) { return sqlite3_column_int64(s, col); }
  sqlite3_stmt* s = nullptr; int code = SQLITE_DONE;
};
}  // namespace red::importer
