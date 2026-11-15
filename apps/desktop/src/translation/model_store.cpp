#include "translation/model_store.h"

#include <algorithm>
#include <fstream>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <system_error>

#include "include/cef_urlrequest.h"

namespace red {
namespace fs = std::filesystem;
namespace {

constexpr int64_t kMaxFileBytes = 160ll * 1024 * 1024;  // Larger than any gzip model file (about 25 MB).

fs::path& Directory() {
  static fs::path directory;
  return directory;
}

std::string Key(const TranslationModel& model) { return std::string(model.from) + "-" + std::string(model.to); }

std::vector<const TranslationModelFile*> Files(const TranslationModel& model) {
  std::vector<const TranslationModelFile*> files;
  for (const auto* file : {&model.model, &model.shortlist, &model.vocab, &model.target_vocab})
    if (file->present()) files.push_back(file);
  return files;
}

// A rough share of the work: gzip model files are about 70% of their uncompressed size.
uint64_t Estimate(const TranslationModel& model) {
  uint64_t total = 0;
  for (const auto* file : Files(model)) total += file->size * 7ull / 10;
  return total;
}

struct Waiter {
  std::set<std::string> models;  // Still on their way.
  uint64_t total = 1, received = 0;
  int last_percent = -1;
  bool failed = false;
  std::function<void(int)> progress;
  std::function<void(bool)> done;
};

class Queue;
Queue& TheQueue();

// One file over HTTPS into <file>.part, renamed when complete.
class FileFetch final : public CefURLRequestClient {
 public:
  FileFetch(fs::path target, std::function<void(int64_t)> got, std::function<void(bool)> finished)
      : target_(std::move(target)), got_(std::move(got)), finished_(std::move(finished)) {}
  void Start(const std::string& url, CefRefPtr<CefRequestContext> context) {
    std::error_code error;
    fs::create_directories(target_.parent_path(), error);
    out_.open(partial(), std::ios::binary | std::ios::trunc);
    if (!out_) { Finish(false); return; }
    auto request = CefRequest::Create();
    request->SetURL(url); request->SetMethod("GET");
    request->SetFlags(UR_FLAG_DISABLE_CACHE);  // No cookies or credentials: UR_FLAG_ALLOW_STORED_CREDENTIALS is not set.
    request_ = CefURLRequest::Create(request, this, context);
    if (!request_) Finish(false);
  }
  void OnRequestComplete(CefRefPtr<CefURLRequest> request) override {
    const auto response = request->GetResponse();
    const bool ok = request->GetRequestStatus() == UR_SUCCESS && response && response->GetStatus() == 200 && !failed_ && written_ > 0;
    Finish(ok);
  }
  void OnUploadProgress(CefRefPtr<CefURLRequest>, int64_t, int64_t) override {}
  void OnDownloadProgress(CefRefPtr<CefURLRequest>, int64_t, int64_t) override {}
  void OnDownloadData(CefRefPtr<CefURLRequest> request, const void* data, size_t length) override {
    if (failed_) return;
    written_ += static_cast<int64_t>(length);
    if (written_ > kMaxFileBytes) { failed_ = true; request->Cancel(); return; }
    out_.write(static_cast<const char*>(data), static_cast<std::streamsize>(length));
    if (!out_) { failed_ = true; request->Cancel(); return; }
    got_(static_cast<int64_t>(length));
  }
  void Cancel() { if (request_) request_->Cancel(); }
  bool GetAuthCredentials(bool, const CefString&, int, const CefString&, const CefString&, CefRefPtr<CefAuthCallback>) override { return false; }

 private:
  fs::path partial() const { return fs::path(target_).concat(L".part"); }
  void Finish(bool ok) {
    if (done_) return;
    done_ = true; out_.close(); request_ = nullptr;
    std::error_code error;
    if (ok) fs::rename(partial(), target_, error);
    if (!ok || error) fs::remove(partial(), error);
    auto finished = std::move(finished_);
    finished(ok && !error);
  }
  fs::path target_;
  std::ofstream out_;
  CefRefPtr<CefURLRequest> request_;
  std::function<void(int64_t)> got_;
  std::function<void(bool)> finished_;
  int64_t written_ = 0;
  bool failed_ = false, done_ = false;
  IMPLEMENT_REFCOUNTING(FileFetch);
};

class Queue {
 public:
  void Stop() {
    waiters_.clear(); pending_.clear(); queued_.clear(); current_ = nullptr; file_index_ = 0; context_ = nullptr;
    if (auto fetch = std::move(fetch_)) fetch->Cancel();
    busy_ = false;
  }
  void Add(std::vector<const TranslationModel*> models, CefRefPtr<CefRequestContext> context, std::function<void(int)> progress,
           std::function<void(bool)> done) {
    auto waiter = std::make_shared<Waiter>();
    waiter->progress = std::move(progress); waiter->done = std::move(done);
    uint64_t total = 0;
    for (const auto* model : models) {
      if (!model || TranslationModelInstalled(*model)) continue;
      const auto key = Key(*model);
      waiter->models.insert(key); total += Estimate(*model);
      if (!queued_.contains(key)) { queued_.insert(key); pending_.push_back(model); }
    }
    if (waiter->models.empty()) { waiter->done(true); return; }
    waiter->total = total ? total : 1;
    waiters_.push_back(waiter);
    context_ = context;
    Next();
  }

 private:
  void Next() {
    if (busy_) return;
    if (file_index_ == 0) {
      if (pending_.empty()) return;
      current_ = pending_.front(); pending_.erase(pending_.begin());
    }
    const auto files = Files(*current_);
    while (file_index_ < files.size() && fs::exists(TranslationModelFilePath(*current_, *files[file_index_]))) ++file_index_;
    if (file_index_ >= files.size()) { ModelDone(true); return; }
    busy_ = true;
    const auto* file = files[file_index_];
    CefRefPtr<FileFetch> fetch = new FileFetch(TranslationModelFilePath(*current_, *file),
        [this](int64_t bytes) { Received(bytes); },
        [this](bool ok) {
          if (!busy_) return;  // Stopped.
          busy_ = false; fetch_ = nullptr;
          if (!ok) { ModelDone(false); return; }
          ++file_index_;
          Next();
        });
    fetch_ = fetch;
    fetch->Start(std::string(kTranslationModelBase) + std::string(file->path), context_);
  }
  void Received(int64_t bytes) {
    const auto key = Key(*current_);
    for (const auto& waiter : waiters_) {
      if (!waiter->models.contains(key)) continue;
      waiter->received += static_cast<uint64_t>(bytes);
      const int percent = static_cast<int>(std::min<uint64_t>(99, waiter->received * 100 / waiter->total));
      if (percent != waiter->last_percent && waiter->progress) { waiter->last_percent = percent; waiter->progress(percent); }
    }
  }
  void ModelDone(bool ok) {
    const auto key = Key(*current_);
    if (!ok) RemoveTranslationModel(*current_);
    queued_.erase(key); file_index_ = 0; current_ = nullptr;
    std::vector<std::shared_ptr<Waiter>> finished;
    for (auto it = waiters_.begin(); it != waiters_.end();) {
      auto& waiter = *it;
      if (waiter->models.erase(key)) {
        waiter->failed |= !ok;
        if (waiter->models.empty()) { finished.push_back(waiter); it = waiters_.erase(it); continue; }
      }
      ++it;
    }
    Next();
    for (const auto& waiter : finished) waiter->done(!waiter->failed);
  }

  std::vector<const TranslationModel*> pending_;
  std::set<std::string> queued_;
  std::vector<std::shared_ptr<Waiter>> waiters_;
  const TranslationModel* current_ = nullptr;
  size_t file_index_ = 0;
  bool busy_ = false;
  CefRefPtr<CefRequestContext> context_;
  CefRefPtr<FileFetch> fetch_;
};

Queue& TheQueue() {
  static Queue queue;
  return queue;
}

}  // namespace

void SetTranslationModelDirectory(const fs::path& directory) { Directory() = directory; }

fs::path TranslationModelFilePath(const TranslationModel& model, const TranslationModelFile& file) {
  return Directory() / (std::string(model.from) + "-" + std::string(model.to)) / std::string(file.name());
}

bool TranslationModelInstalled(const TranslationModel& model) {
  if (Directory().empty()) return false;
  std::error_code error;
  for (const auto* file : Files(model))
    if (!fs::is_regular_file(TranslationModelFilePath(model, *file), error)) return false;
  return true;
}

void RemoveTranslationModel(const TranslationModel& model) {
  if (Directory().empty()) return;
  std::error_code error;
  fs::remove_all(Directory() / (std::string(model.from) + "-" + std::string(model.to)), error);
}

void StopTranslationModelDownloads() { TheQueue().Stop(); }

void DownloadTranslationModels(std::vector<const TranslationModel*> models, CefRefPtr<CefRequestContext> context,
                               std::function<void(int)> progress, std::function<void(bool)> done) {
  if (Directory().empty()) { done(false); return; }
  TheQueue().Add(std::move(models), context, std::move(progress), std::move(done));
}

}  // namespace red
