#include "history/history_service.h"
#include <chrono>
#include <algorithm>
#include "include/base/cef_callback.h"
#include "include/cef_parser.h"
#include "include/wrapper/cef_closure_task.h"

namespace red {
namespace {
int64_t Now() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}
constexpr int64_t kRetentionMs = 90LL * 24 * 60 * 60 * 1000;
}  // namespace

bool HistoryService::Recordable(const std::string& url) {
  CefURLParts parts;
  if (url.size() > 4096 || !CefParseURL(url, parts)) return false;
  const auto scheme = CefString(&parts.scheme).ToString();
  return (scheme == "https" || scheme == "http") && !CefString(&parts.host).empty();
}
void HistoryService::Start(const std::filesystem::path& path, std::function<void(std::vector<DownloadRecord>)> restored) {
  worker_ = CefThread::CreateThread("History database");
  if (!worker_) return;
  auto store = std::make_shared<HistoryStore>();
  store_ = store;
  Run<std::vector<DownloadRecord>>([path](HistoryStore& s) {
    std::error_code ignored; std::filesystem::create_directories(path.parent_path(), ignored);
    if (!s.Open(path)) return std::vector<DownloadRecord>{};
    s.Prune(Now() - kRetentionMs);
    return s.Downloads(30);
  }, [weak = state_, restored = std::move(restored)](std::vector<DownloadRecord> records) {
    if (auto state = weak.lock()) {
      auto data = CefDictionaryValue::Create(); data->SetInt("downloads", static_cast<int>(records.size()));
      state->Record("history_opened", data);
    }
    restored(std::move(records));
  });
}
void HistoryService::Stop() {
  if (!worker_) return;
  // Close the database on its own thread, then join it.
  worker_->GetTaskRunner()->PostTask(CefCreateClosureTask(base::BindOnce(
      [](std::shared_ptr<HistoryStore>* store) { store->reset(); }, &store_)));
  worker_->Stop(); worker_ = nullptr;
}
template <typename Result>
void HistoryService::Run(std::function<Result(HistoryStore&)> work, std::function<void(Result)> reply) {
  if (!worker_) return;
  worker_->GetTaskRunner()->PostTask(CefCreateClosureTask(base::BindOnce(
      [](std::shared_ptr<HistoryStore> store, std::function<Result(HistoryStore&)> work, std::function<void(Result)> reply) {
        Result result = work(*store);
        if (reply) CefPostTask(TID_UI, base::BindOnce([](std::function<void(Result)> reply, Result result) {
          reply(std::move(result));
        }, std::move(reply), std::move(result)));
      }, store_, std::move(work), std::move(reply))));
}
void HistoryService::ImportVisits(std::vector<Visit> visits, std::function<void(int)> done) {
  if (!worker_) { done(-1); return; }
  std::erase_if(visits, [](const auto& v) { return !Recordable(v.url) || v.title.size() > 240 || v.visited_at <= 0 || v.visited_at > Now(); });
  Run<int>([visits = std::move(visits)](HistoryStore& s) { return s.ImportVisits(visits); }, std::move(done));
}
void HistoryService::Post(std::function<void(HistoryStore&)> work) {
  Run<bool>([work = std::move(work)](HistoryStore& s) { work(s); return true; }, nullptr);
}
void HistoryService::RecordVisit(const std::string& url, const std::string& title) {
  if (!Recordable(url)) return;
  const auto now = Now();
  Post([url, title = title.substr(0, 400), now](HistoryStore& s) { s.AddVisit(url, title, now); });
  if (auto state = state_.lock(); state && state->probe) {
    auto data = CefDictionaryValue::Create(); data->SetString("url", url.substr(0, 200)); state->Record("history_visit", data);
  }
}
void HistoryService::UpdateTitle(const std::string& url, const std::string& title) {
  if (!Recordable(url) || title.empty()) return;
  Post([url, title = title.substr(0, 400)](HistoryStore& s) { s.UpdateTitle(url, title); });
}
void HistoryService::Days(int limit, const std::string& query, std::function<void(std::vector<VisitDay>)> done) {
  Run<std::vector<VisitDay>>([limit, query](HistoryStore& s) { return s.Days(limit, query); }, std::move(done));
}
void HistoryService::Visits(int64_t before, int limit, const std::string& query, std::function<void(std::vector<Visit>)> done) {
  Run<std::vector<Visit>>([before, limit, query](HistoryStore& s) { return s.Visits(before, limit, query); }, std::move(done));
}
void HistoryService::Suggest(const std::string& query, int limit, std::function<void(std::vector<Visit>)> done) {
  Run<std::vector<Visit>>([query, limit](HistoryStore& s) { return s.Suggest(query, limit); }, std::move(done));
}
void HistoryService::DeleteVisit(int64_t id, std::function<void(bool)> done) {
  Run<bool>([id](HistoryStore& s) { return s.DeleteVisit(id); }, std::move(done));
}
void HistoryService::DeleteRange(int64_t from, int64_t to, std::function<void(int)> done) {
  Run<int>([from, to](HistoryStore& s) { return s.DeleteRange(from, to); }, std::move(done));
}
void HistoryService::SaveDownload(const DownloadRecord& record, std::function<void(int64_t)> done) {
  Run<int64_t>([record](HistoryStore& s) { return s.SaveDownload(record); }, std::move(done));
}
void HistoryService::DeleteDownload(int64_t id) { Post([id](HistoryStore& s) { s.DeleteDownload(id); }); }
void HistoryService::ClearDownloads() { Post([](HistoryStore& s) { s.ClearDownloads(); }); }
void HistoryService::DownloadsBefore(int64_t before, int limit, std::function<void(std::vector<DownloadRecord>)> done) {
  Run<std::vector<DownloadRecord>>([before, limit](HistoryStore& s) { return s.DownloadsBefore(before, limit); }, std::move(done));
}
void HistoryService::Download(int64_t id, std::function<void(std::optional<DownloadRecord>)> done) {
  Run<std::optional<DownloadRecord>>([id](HistoryStore& s) { return s.Download(id); }, std::move(done));
}
void HistoryService::SaveFavicon(const std::string& host, const std::string& png) {
  const auto now = Now();
  Post([host, png, now](HistoryStore& s) { s.SaveFavicon(host, png, now); });
}
void HistoryService::LoadFavicons(std::function<void(std::vector<std::pair<std::string, std::string>>)> done) {
  Run<std::vector<std::pair<std::string, std::string>>>([](HistoryStore& s) { return s.Favicons(1000); }, std::move(done));
}
}  // namespace red
