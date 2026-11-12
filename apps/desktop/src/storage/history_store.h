#pragma once
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <optional>
#include <vector>

struct sqlite3;
namespace red {
struct Visit {
  int64_t id = 0;
  std::string url, title;
  int64_t visited_at = 0;  // Epoch milliseconds.
};
// One local calendar day of history: "YYYY-MM-DD", its visit count and the
// newest visit's time (a page request with before = latest + 1 starts there).
struct VisitDay {
  std::string date;
  int64_t count = 0, latest = 0;
};
struct DownloadRecord {
  int64_t id = 0;
  std::string name, origin, state;  // state: complete | canceled | interrupted.
  std::filesystem::path path;       // Native only; never sent to the UI.
  int64_t received = 0, total = -1, finished_at = 0;
};
// Browsing history and finished downloads in BrowserData/history.sqlite3.
// Synchronous; the owner runs it on a worker thread. Our own record only:
// Chromium keeps its own profile history, which CEF exposes no API to edit.
class HistoryStore {
 public:
  HistoryStore() = default;
  ~HistoryStore();
  HistoryStore(const HistoryStore&) = delete;
  HistoryStore& operator=(const HistoryStore&) = delete;
  bool Open(const std::filesystem::path& path);
  // A repeat of the latest visit to the same URL within 30 minutes refreshes it
  // instead of adding a row (reloads, redirects back, tab switching).
  int64_t AddVisit(const std::string& url, const std::string& title, int64_t now);
  int ImportVisits(const std::vector<Visit>& visits);  // Atomic, deduplicates URL + timestamp.
  void UpdateTitle(const std::string& url, const std::string& title);
  // Newest first, older than |before| (0 = now), optionally matching |query| in
  // the title or URL (plain text, not a pattern).
  std::vector<Visit> Visits(int64_t before, int limit, const std::string& query);
  // Days that have visits (matching |query|), newest first, in local time.
  std::vector<VisitDay> Days(int limit, const std::string& query);
  // Most recent visit per URL that matches |query|, for address suggestions.
  std::vector<Visit> Suggest(const std::string& query, int limit);
  bool DeleteVisit(int64_t id);
  // Removes visits with from <= visited_at < to; returns the number removed.
  int DeleteRange(int64_t from, int64_t to);
  void Prune(int64_t older_than);
  // Inserts when record.id is 0; returns the stored ID (0 on failure).
  int64_t SaveDownload(const DownloadRecord& record);
  std::vector<DownloadRecord> Downloads(int limit);
  // Newest first, finished before |before| (0 = any), for the downloads page.
  std::vector<DownloadRecord> DownloadsBefore(int64_t before, int limit);
  std::optional<DownloadRecord> Download(int64_t id);
  void DeleteDownload(int64_t id);
  void ClearDownloads();
  // Site icons by host (PNG bytes), newest first; bounded to 1000 hosts.
  void SaveFavicon(const std::string& host, const std::string& png, int64_t now);
  // Atomically adds missing icons and returns only the new records. A failed
  // write returns nullopt so the caller does not publish uncommitted icons.
  std::optional<std::map<std::string, std::string>> ImportFavicons(std::map<std::string, std::string> icons, int64_t now);
  std::vector<std::pair<std::string, std::string>> Favicons(int limit);
 private:
  bool Exec(const char* sql);
  sqlite3* db_ = nullptr;
};
}  // namespace red
