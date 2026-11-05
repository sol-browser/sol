#include "app/web_profile_repair.h"
#include <windows.h>
#include <sqlite3.h>
#include <cstdio>
#include <string>

namespace red {
namespace fs = std::filesystem;

int64_t LatestWebVisit(const fs::path& history) {
  std::error_code error;
  if (!fs::is_regular_file(history, error)) return -1;
  // Read-only and immutable: nothing (journal, WAL) is written next to it.
  std::string uri = "file:///";
  for (const char8_t unit : history.generic_u8string()) {
    const char c = static_cast<char>(unit);
    if (c == '?' || c == '#' || c == '%' || c == ' ') { char hex[4]; std::snprintf(hex, sizeof(hex), "%%%02X", static_cast<unsigned char>(c)); uri += hex; }
    else uri.push_back(c);
  }
  uri += "?immutable=1";
  sqlite3* db = nullptr;
  int64_t latest = -1;
  if (sqlite3_open_v2(uri.c_str(), &db, SQLITE_OPEN_READONLY | SQLITE_OPEN_URI, nullptr) == SQLITE_OK) {
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(db, "SELECT max(last_visit_time) FROM urls WHERE url LIKE 'http://%' OR url LIKE 'https://%'", -1, &statement, nullptr) == SQLITE_OK &&
        sqlite3_step(statement) == SQLITE_ROW && sqlite3_column_type(statement, 0) != SQLITE_NULL)
      latest = sqlite3_column_int64(statement, 0);
    sqlite3_finalize(statement);
  }
  sqlite3_close(db);
  return latest;
}

WebProfileRepair RepairWebProfile(const fs::path& profile) {
  const auto shell = profile / L"Shell", web = profile / L"Default";
  const auto in_shell = LatestWebVisit(shell / L"History");
  if (in_shell <= 0 || in_shell <= LatestWebVisit(web / L"History")) return WebProfileRepair::None;
  std::error_code error;
  fs::path aside;
  if (fs::exists(web, error)) {
    SYSTEMTIME now{}; GetLocalTime(&now);
    wchar_t name[64];
    swprintf_s(name, L"Default (before repair %04u-%02u-%02u %02u%02u%02u)", now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond);
    aside = profile / name;
    // A rename fails while another Sol uses the profile: nothing changes then.
    if (!MoveFileExW(web.c_str(), aside.c_str(), 0)) return WebProfileRepair::Failed;
  }
  if (!MoveFileExW(shell.c_str(), web.c_str(), 0)) {
    if (!aside.empty()) MoveFileExW(aside.c_str(), web.c_str(), 0);
    return WebProfileRepair::Failed;
  }
  return WebProfileRepair::Moved;
}
}  // namespace red
