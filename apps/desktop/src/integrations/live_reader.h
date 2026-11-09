#pragma once
#include <functional>
#include <memory>
#include <string>
#include "include/cef_browser.h"
#include "include/cef_registration.h"
#include "integrations/providers.h"
namespace red {
class LiveRead : public std::enable_shared_from_this<LiveRead> {
 public:
  using Reply = std::function<void(CefRefPtr<CefDictionaryValue>)>;
  static std::shared_ptr<LiveRead> Start(CefRefPtr<CefBrowser> browser, const LiveProvider& provider,
      std::string source, Reply reply, bool outlook_shape = false);
  enum class Action { Open, Compose };
  static void Click(CefRefPtr<CefBrowser> browser, const LiveProvider& provider, std::string source,
                    Action action, std::string token, std::function<void(bool)> reply);
  void Cancel();
  void Result(int browser, int query, const std::string& json);
 private:
  static std::shared_ptr<LiveRead> Evaluate(CefRefPtr<CefBrowser> browser, const LiveProvider& provider,
      std::string source, std::string action, std::string token, Reply reply, bool outlook_shape = false);
  void Finish(CefRefPtr<CefDictionaryValue> value = nullptr);
  CefRefPtr<CefBrowser> browser_;
  CefRefPtr<CefRegistration> observer_;
  Reply reply_;
  LiveProvider provider_;
  std::string source_;
  int query_ = 0;
  bool action_ = false;
  bool outlook_shape_ = false;
  bool done_ = false;
};
}  // namespace red
