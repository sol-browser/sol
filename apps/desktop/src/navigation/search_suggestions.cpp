#include "navigation/search_suggestions.h"
#include <algorithm>
#include <windows.h>
#include "include/cef_parser.h"
#include "include/cef_request_context_handler.h"
#include "include/base/cef_callback.h"
#include "include/wrapper/cef_closure_task.h"

namespace red {
namespace {
constexpr size_t kMaxBytes = 32768;
class SuggestClient final : public CefURLRequestClient {
 public:
  explicit SuggestClient(SearchSuggestions::Reply done) : done_(std::move(done)) {}
  void OnRequestComplete(CefRefPtr<CefURLRequest> request) override {
    std::vector<std::string> result;
    const auto response = request->GetResponse();
    if (request->GetRequestStatus() == UR_SUCCESS && response && response->GetStatus() == 200 && body_.size() <= kMaxBytes) {
      const auto json = CefParseJSON(body_, JSON_PARSER_RFC);
      if (json && json->GetType() == VTYPE_LIST) {
        const auto list = json->GetList();
        if (list->GetSize() >= 2 && list->GetType(1) == VTYPE_LIST) {
          const auto entries = list->GetList(1);
          for (size_t i = 0; i < std::min(entries->GetSize(), size_t{20}) && result.size() < 5; ++i) {
            if (entries->GetType(i) != VTYPE_STRING) continue;
            const auto text = entries->GetString(i).ToString();
            if (!text.empty() && text.size() <= 400 && std::none_of(text.begin(),text.end(),[](unsigned char c) { return c < 32 || c == 127; }) &&
                std::find(result.begin(),result.end(),text) == result.end()) result.push_back(text);
          }
        }
      }
    }
    if (auto done = std::move(done_)) done(std::move(result));
  }
  void OnUploadProgress(CefRefPtr<CefURLRequest>, int64_t, int64_t) override {}
  void OnDownloadProgress(CefRefPtr<CefURLRequest> request, int64_t current, int64_t total) override {
    if (current > static_cast<int64_t>(kMaxBytes) || total > static_cast<int64_t>(kMaxBytes)) request->Cancel();
  }
  void OnDownloadData(CefRefPtr<CefURLRequest> request, const void* data, size_t length) override {
    if (length > kMaxBytes - body_.size()) { request->Cancel(); return; }
    body_.append(static_cast<const char*>(data), length);
  }
  bool GetAuthCredentials(bool, const CefString&, int, const CefString&, const CefString&, CefRefPtr<CefAuthCallback>) override { return false; }
 private:
  std::string body_;
  SearchSuggestions::Reply done_;
  IMPLEMENT_REFCOUNTING(SuggestClient);
};
}
bool ValidSuggestionQuery(const std::string& text) {
  return text.size() >= 2 && text.size() <= 200 && text.find_first_not_of(' ') != std::string::npos &&
      text.find_first_of(".:/\\@=#") == std::string::npos &&
      std::none_of(text.begin(),text.end(),[](unsigned char c) { return c < 32 || c == 127; });
}
void SearchSuggestions::Cancel() {
  ++generation_;
  auto pending = std::move(request_); auto reply = std::move(reply_);
  if (pending) pending->Cancel();
  if (reply) reply({});
}
void SearchSuggestions::Complete(unsigned long long generation, std::vector<std::string> suggestions) {
  if (generation != generation_) return;
  request_ = nullptr;
  if (!suggestions.empty()) {
    if (cache_.size() >= 32) {
      const auto oldest = std::min_element(cache_.begin(),cache_.end(),[](const auto& a, const auto& b) { return a.second.time < b.second.time; });
      cache_.erase(oldest);
    }
    cache_[text_] = {GetTickCount64(),suggestions};
  }
  if (auto reply = std::move(reply_)) reply(std::move(suggestions));
}
void SearchSuggestions::Fetch(const std::string& text, Reply done) {
  Cancel();
  if (!ValidSuggestionQuery(text)) { done({}); return; }
  if (const auto cached = cache_.find(text); cached != cache_.end() && GetTickCount64() - cached->second.time < 300000) {
    done(cached->second.suggestions); return;
  }
  if (!context_) context_ = CefRequestContext::CreateContext(CefRequestContextSettings{}, nullptr);
  if (!context_) { done({}); return; }
  reply_ = std::move(done); text_ = text;
  const auto generation = generation_;
  auto request = CefRequest::Create();
  request->SetURL("https://suggestqueries.google.com/complete/search?client=firefox&q=" + CefURIEncode(text, true).ToString());
  request->SetMethod("GET");
  request->SetFlags(UR_FLAG_DISABLE_CACHE | UR_FLAG_STOP_ON_REDIRECT | UR_FLAG_NO_RETRY_ON_5XX);
  request_ = CefURLRequest::Create(request, new SuggestClient([weak = weak_from_this(), generation](std::vector<std::string> results) {
    if (const auto self = weak.lock()) self->Complete(generation,std::move(results));
  }), context_);
  if (!request_) { Complete(generation,{}); return; }
  CefPostDelayedTask(TID_UI, base::BindOnce([](std::weak_ptr<SearchSuggestions> weak, unsigned long long generation) {
    if (const auto self = weak.lock(); self && self->generation_ == generation && self->request_) self->Cancel();
  }, weak_from_this(), generation), 3000);
}
}  // namespace red
