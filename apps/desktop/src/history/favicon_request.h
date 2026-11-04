#pragma once
#include <cstdint>
#include <optional>
#include <string>
namespace red {
// One selection per document. Viewport/favicon notifications may repeat while
// DevTools disables caching. Reloads and real URL changes remain new selections.
class FaviconRequest {
 public:
  void NewDocument() { ++serial_; url_.clear(); pending_=false; }
  std::optional<uint64_t> Begin(const std::string& url) {
    if (url.empty() || url.size()>65536 || url==url_) return std::nullopt;
    url_=url; pending_=true; return ++serial_;
  }
  bool Complete(uint64_t serial) {
    if (!pending_ || serial!=serial_) return false;
    pending_=false; return true;
  }
 private:
  uint64_t serial_=0;
  std::string url_;
  bool pending_=false;
};
}
