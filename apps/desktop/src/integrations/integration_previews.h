#pragma once
#include <chrono>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include "include/cef_browser.h"

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
  void ObserveOutlookInbox(CefRefPtr<CefBrowser> browser);
  void Stop();
  void OnClosed(int browser);
  void OnFailed(int browser, bool sign_in_required = false);
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
  void ClearForeground();
  void Close();
  std::weak_ptr<State> state_;
  std::shared_ptr<BrowsingProfile> profile_;
  std::map<std::string,Entry> entries_;
  std::string active_;
  CefRefPtr<CefBrowser> browser_;
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
  enum class GmailView { Inbox, Unread, Recent };
  GmailView gmail_view_ = GmailView::Inbox;
};
}  // namespace red
