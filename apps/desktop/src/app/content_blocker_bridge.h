#pragma once
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include "include/cef_browser.h"
#include "include/cef_registration.h"
#include "include/cef_request_context.h"
#include "include/cef_values.h"

namespace red {
struct State;
// Talks to the built-in uBlock Origin (engine patch red_17, docs/ublock-origin.md)
// for Sol's own ad blocker settings and site panel. uBO takes its privileged
// commands ("dashboard", "popupPanel" channels) only from its own pages, so the
// bridge keeps one of them (no-dashboard.html) in a hidden windowless browser
// of a web profile and calls vAPI.messaging.send there through DevTools
// Runtime.evaluate. Nothing new is exposed to websites or to uBO's pages; the
// page closes again after a minute without calls. UI thread only.
class ContentBlockerBridge : public std::enable_shared_from_this<ContentBlockerBridge> {
 public:
  // |error| is empty on success; |result| is uBO's reply (may be null).
  using Reply = std::function<void(CefRefPtr<CefValue> result, const std::string& error)>;
  ContentBlockerBridge(std::weak_ptr<State> state, CefRefPtr<CefRequestContext> context)
      : state_(std::move(state)), context_(context) {}
  // |channel|: "dashboard" or "popupPanel"; |message| must have a string "what".
  void Send(const std::string& channel, CefRefPtr<CefDictionaryValue> message, Reply done);
  void Stop();
  CefRefPtr<CefRequestContext> context() const { return context_; }

  // From the hidden page's client and DevTools observer.
  void OnCreated(CefRefPtr<CefBrowser> browser);
  void OnLoaded(int browser, bool ok);
  void OnResult(int message, bool ok, const std::string& json);
  void OnClosed(int browser);

 private:
  struct Call { std::string expression; Reply done; };
  void Open();
  void Flush();
  void Fail(const std::string& error);
  void ScheduleIdleClose();
  std::weak_ptr<State> state_;
  CefRefPtr<CefRequestContext> context_;
  CefRefPtr<CefBrowser> browser_;
  CefRefPtr<CefRegistration> observer_;
  bool opening_ = false, loaded_ = false, stopped_ = false;
  int idle_generation_ = 0;
  std::vector<Call> queued_;
  std::map<int, Reply> waiting_;  // DevTools message id -> reply.
};

// The JavaScript that sends one message to uBO and returns its reply as JSON
// text (unit-tested: JSON-encoded inputs only, no string concatenation of data).
std::string ContentBlockerExpression(const std::string& channel_json, const std::string& message_json);
}  // namespace red
