#pragma once
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include "include/cef_urlrequest.h"

namespace red {
bool ValidSuggestionQuery(const std::string& text);
// UI-thread only. Fixed HTTPS service, no cookies/credentials or disk cache.
class SearchSuggestions : public std::enable_shared_from_this<SearchSuggestions> {
 public:
  using Reply = std::function<void(std::vector<std::string>)>;
  void Fetch(const std::string& text, Reply done);
  void Cancel();
 private:
  void Complete(unsigned long long generation, std::vector<std::string> suggestions);
  CefRefPtr<CefRequestContext> context_;
  CefRefPtr<CefURLRequest> request_;
  Reply reply_;
  std::string text_;
  unsigned long long generation_ = 0;
  struct Cached { unsigned long long time; std::vector<std::string> suggestions; };
  std::map<std::string, Cached> cache_;
};
}  // namespace red
