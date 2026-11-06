#include "extensions/shell_reset.h"
#include "app/web_profile_repair.h"
#include <windows.h>
#include <sqlite3.h>
#include <string>
#include <cstdio>
#include <fstream>

namespace {
int failures = 0;
void Check(bool test, const char* name) { if (!test) { ++failures; std::fprintf(stderr, "FAIL: %s\n", name); } }
// A Chrome-like History database with these (url, last_visit_time) rows.
void History(const std::filesystem::path& file, std::initializer_list<std::pair<const char*, long long>> rows) {
  std::filesystem::create_directories(file.parent_path());
  sqlite3* db = nullptr; sqlite3_open16(file.c_str(), &db);
  sqlite3_exec(db, "CREATE TABLE IF NOT EXISTS urls(id INTEGER PRIMARY KEY, url LONGVARCHAR, last_visit_time INTEGER)", nullptr, nullptr, nullptr);
  for (const auto& [url, time] : rows) {
    const auto sql = std::string("INSERT INTO urls(url,last_visit_time) VALUES('") + url + "'," + std::to_string(time) + ")";
    sqlite3_exec(db, sql.c_str(), nullptr, nullptr, nullptr);
  }
  sqlite3_close(db);
}
int Count(const std::filesystem::path& profile, const wchar_t* prefix) {
  int n = 0;
  for (const auto& e : std::filesystem::directory_iterator(profile)) n += e.path().filename().wstring().starts_with(prefix) ? 1 : 0;
  return n;
}
}
int wmain() {
  namespace fs = std::filesystem;
  const auto profile = fs::temp_directory_path() / (L"sol-shell-reset-" + std::to_wstring(GetCurrentProcessId()));
  std::error_code ignored; fs::remove_all(profile, ignored);
  fs::create_directories(profile / L"Shell" / L"Extensions" / L"dhdgffkkebhmkfjojejmpbldmpobfkfo");
  fs::create_directories(profile / L"Default");
  std::ofstream(profile / L"Default" / L"Bookmarks") << "{}";
  std::ofstream(profile / L"Shell" / L"Preferences") << "{}";
  std::ofstream(profile / L"Shell" / L"Login Data") << "legacy-logins";

  Check(red::ShellResetAllowed(profile, fs::file_time_type::clock::now()), "first reset is allowed");
  Check(red::ResetShellProfile(L"relative") == red::ShellReset::Failed, "a relative profile is refused");
  Check(red::ResetShellProfile(profile) == red::ShellReset::Done, "reset");
  Check(!fs::exists(profile / L"Shell"), "the Shell path is available for a clean profile");
  Check(Count(profile, L"Shell (reset") == 1, "Shell without History is quarantined");
  for (const auto& entry : fs::directory_iterator(profile)) {
    if (!entry.path().filename().wstring().starts_with(L"Shell (reset")) continue;
    std::string logins;
    std::ifstream(entry.path() / L"Login Data") >> logins;
    Check(logins == "legacy-logins", "quarantine keeps logins without relying on History");
  }
  Check(fs::exists(profile / L"Default" / L"Bookmarks"), "the web profile is untouched");
  const auto now = fs::file_time_type::clock::now();
  Check(!red::ShellResetAllowed(profile, now), "a violation right after a reset closes instead");
  Check(red::ShellResetAllowed(profile, now + std::chrono::minutes(11)), "a later violation may reset again");
  Check(red::ResetShellProfile(profile) == red::ShellReset::Done, "reset works when Shell is already gone");
  Check(!red::ShellResetScheduled(), "nothing is scheduled unless the guard asks");
  red::ScheduleShellReset();
  Check(red::ShellResetScheduled(), "the request is remembered");
  fs::remove_all(profile, ignored);

  // A locked source cannot be renamed. Recovery must leave every file in place.
  const auto locked = profile / L"Shell";
  fs::create_directories(locked);
  std::ofstream(locked / L"Login Data") << "locked-profile-logins";
  const HANDLE lock = CreateFileW(locked.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
      nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
  Check(lock != INVALID_HANDLE_VALUE, "lock Shell without delete sharing");
  if (lock != INVALID_HANDLE_VALUE) {
    Check(red::ResetShellProfile(profile) == red::ShellReset::Failed, "a failed quarantine aborts reset");
    std::string logins;
    std::ifstream(locked / L"Login Data") >> logins;
    Check(logins == "locked-profile-logins", "failed reset keeps the original files");
    Check(!fs::exists(profile / L"shell-reset-done"), "a failed reset leaves no completion marker");
    CloseHandle(lock);
  }
  fs::remove_all(profile, ignored);

  // Sol up to 0.1.2 could run websites in Shell (web_profile_repair.h).
  const auto web = fs::temp_directory_path() / (L"sol web repair " + std::to_wstring(GetCurrentProcessId()));
  fs::remove_all(web, ignored);
  History(web / L"Default" / L"History", {{"https://old.test/", 100}});
  History(web / L"Shell" / L"History", {{"sol://ui/index.html", 900}});
  Check(red::LatestWebVisit(web / L"Shell" / L"History") == -1 && red::LatestWebVisit(web / L"Default" / L"History") == 100, "only website visits count");
  Check(red::RepairWebProfile(web) == red::WebProfileRepair::None && fs::exists(web / L"Shell"), "a UI-only Shell stays");
  History(web / L"Shell" / L"History", {{"https://new.test/", 500}});
  std::ofstream(web / L"Shell" / L"Login Data") << "logins";
  Check(red::RepairWebProfile(web) == red::WebProfileRepair::Moved, "newer websites in Shell: repaired");
  Check(fs::exists(web / L"Default" / L"Login Data") && !fs::exists(web / L"Shell"), "Shell became Default");
  Check(Count(web, L"Default (before repair") == 1, "the old Default is kept aside");
  Check(red::RepairWebProfile(web) == red::WebProfileRepair::None, "repair runs once");
  History(web / L"Shell" / L"History", {{"https://later.test/", 50}});
  Check(red::RepairWebProfile(web) == red::WebProfileRepair::None, "older website visits in Shell do not move Default");
  Check(red::ResetShellProfile(web) == red::ShellReset::Done && !fs::exists(web / L"Shell") && Count(web, L"Shell (reset") == 1,
        "a reset keeps a Shell with website history aside");
  fs::remove_all(web, ignored);
  if (failures) return 1;
  std::puts("shell reset: ok");
  return 0;
}
