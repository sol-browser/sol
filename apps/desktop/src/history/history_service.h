#pragma once
#include <functional>
#include <memory>
#include <vector>
#include "app/state.h"
#include "include/cef_thread.h"
#include "storage/history_store.h"

namespace red {
// Owns HistoryStore on a worker thread (like the workspace database). Callers
// use the UI thread; results come back on the UI thread.
class HistoryService : public std::enable_shared_from_this<HistoryService> {
 public:
  explicit HistoryService(std::weak_ptr<State> state) : state_(std::move(state)) {}
  // Opens BrowserData/history.sqlite3, prunes visits older than 90 days and
  // hands the saved downloads to |restored|.
  void Start(const std::filesystem::path& path, std::function<void(std::vector<DownloadRecord>)> restored);
  void Stop();
  // Only http(s) top-level pages are recorded.
  static bool Recordable(const std::string& url);
  void RecordVisit(const std::string& url, const std::string& title);
  void ImportVisits(std::vector<Visit> visits, std::function<void(int)> done);
  void UpdateTitle(const std::string& url, const std::string& title);
  void Visits(int64_t before, int limit, const std::string& query, std::function<void(std::vector<Visit>)> done);
  void Suggest(const std::string& query, int limit, std::function<void(std::vector<Visit>)> done);
  void DeleteVisit(int64_t id, std::function<void(bool)> done);
  void DeleteRange(int64_t from, int64_t to, std::function<void(int)> done);
  void SaveDownload(const DownloadRecord& record, std::function<void(int64_t)> done);
  void DeleteDownload(int64_t id);
  void ClearDownloads();
  void DownloadsBefore(int64_t before, int limit, std::function<void(std::vector<DownloadRecord>)> done);
  void Download(int64_t id, std::function<void(std::optional<DownloadRecord>)> done);
  void SaveFavicon(const std::string& host, const std::string& png);
  void LoadFavicons(std::function<void(std::vector<std::pair<std::string, std::string>>)> done);
 private:
  // Runs |work| with the store on the worker; |reply| (optional) on the UI thread.
  template <typename Result>
  void Run(std::function<Result(HistoryStore&)> work, std::function<void(Result)> reply);
  void Post(std::function<void(HistoryStore&)> work);
  std::weak_ptr<State> state_;
  CefRefPtr<CefThread> worker_;
  std::shared_ptr<HistoryStore> store_;  // Touched only on the worker thread.
};
}  // namespace red
