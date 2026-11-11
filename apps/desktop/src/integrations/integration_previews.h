#pragma once
#include <chrono>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include "include/cef_browser.h"
#include "include/cef_urlrequest.h"

namespace red {
struct State;
struct BrowsingProfile;
class LiveRead;

// UI thread. A short-lived OSR browser reads one canonical view at a time;
// only the bounded snapshot survives, in memory in the selected profile.
class IntegrationPreviews : public std::enable_shared_from_this<IntegrationPreviews> {
 public:
  explicit IntegrationPreviews(std::weak_ptr<State> state) : state_(std::move(state)) {}
  void Update(std::shared_ptr<BrowsingProfile> profile, const std::vector<std::string>& targets);
  CefRefPtr<CefDictionaryValue> Cached(const std::string& target) const;
  bool Queued(const std::string& target) const { return entries_.contains(target); }
  void Refresh(const std::string& target);
  void RefreshAfterMailOpen(const std::string& target);
  // Opening a card: read again when its snapshot is older than |age| (the old one stays meanwhile).
  void RefreshIfOlder(const std::string& target, std::chrono::seconds age);
  // A visible mail tab back on its inbox with a new title (Gmail shows the unread count there):
  // mail was read or arrived, so read the mailbox again instead of waiting for the next round.
  void ObserveMailTitle(CefRefPtr<CefBrowser> browser, const std::string& title);
  void ObserveOutlookInbox(CefRefPtr<CefBrowser> browser);
  // The off-screen reader running right now, if any (app/memory_report.cpp).
  CefRefPtr<CefBrowser> worker() const { return closing_ ? nullptr : browser_; }
  void Stop();
  void OnClosed(int browser);
  void OnFailed(int browser, bool sign_in_required = false);
  // Gmail reads its Atom feed instead of loading Gmail (integrations/gmail_feed.h).
  void OnGmailFeed(const std::string& target, int status, const std::string& body);
 private:
  using Clock = std::chrono::steady_clock;
  struct Entry {
    Clock::time_point due, updated;
    CefRefPtr<CefDictionaryValue> snapshot;
  };
  void Queue();
  void Tick();
  void Start(const std::string& target);
  void AdvanceGmailSearch();
  void Finish(CefRefPtr<CefDictionaryValue> result);
  void StoreSnapshot(const std::string& target, CefRefPtr<CefDictionaryValue> result, bool foreground);
  void ReadForeground(Clock::time_point now);
  void StartGmailFeed(const std::string& target);
  void ClearForeground();
  void Close();
  std::weak_ptr<State> state_;
  std::shared_ptr<BrowsingProfile> profile_;
  std::map<std::string,Entry> entries_;
  std::map<std::string,std::string> inbox_titles_;  // Last inbox title seen per mailbox target.
  std::string active_;
  CefRefPtr<CefBrowser> browser_;
  CefRefPtr<CefURLRequest> feed_request_;
  std::shared_ptr<LiveRead> read_;
  Clock::time_point deadline_, next_read_;
  Clock::time_point search_deadline_;
  CefRefPtr<CefBrowser> foreground_browser_;
  std::shared_ptr<LiveRead> foreground_read_;
  std::string foreground_source_, foreground_target_;
  Clock::time_point foreground_due_, foreground_deadline_;
  int revision_ = 0;
  bool pending_ = false, closing_ = false, stopped_ = false;
  bool empty_ready_ = false;
  enum class GmailView { Inbox, Unread };
  GmailView gmail_view_ = GmailView::Inbox;
};
}  // namespace red
