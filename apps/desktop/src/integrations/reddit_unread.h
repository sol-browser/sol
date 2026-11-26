#pragma once
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include "include/cef_urlrequest.h"

namespace red {
struct State;
struct BrowsingProfile;

// Reddit's notification bell and chats without loading reddit.com (docs/web-app-integrations.md,
// "Reddit notification dot"). Reddit's titles carry no count and its legacy inbox_count ignores
// what the new interface has read (the user found both, 2026-11-26). Each check fetches the two
// header items Reddit's own web app loads, rendered on Reddit's server with their badges, using the
// profile's cookies (no token): integrations/reddit_bell.h reads them.
// The cookies go in an explicit Cookie header, not through stored credentials, so Reddit's
// Set-Cookie replies (session and CSRF rotation) are never saved and an open Reddit page's cookies
// never change under it.
inline constexpr char kRedditBellUrl[] = "https://www.reddit.com/svc/shreddit/header-action-item-inbox?render-mode=partial";
inline constexpr char kRedditChatUrl[] = "https://www.reddit.com/svc/shreddit/header-action-item-chat";

// UI thread. One per workspace window; polls only while the profile has a pinned Reddit inbox pin
// (RedditInboxPin) and the Reddit integration is enabled. The count is bell + chats; either part
// alone is used when the other is unknown. Every 60 s while Reddit is in the background; while
// |focused| says a Reddit page is this window's active tab, nothing is asked and the count is 0
// (the user's rule, 2026-11-26). HTTP 429 pauses all checks for 10 minutes, doubling up to an hour,
// and keeps the last count. Checks every 10 s while Reddit was active (tried the same day) were
// followed by Reddit's "Request failed" toasts and HTTP 429 on the user's session; with the
// integration off they stopped.
class RedditUnread : public std::enable_shared_from_this<RedditUnread> {
 public:
  enum class Step { Chat, Bell };
  RedditUnread(std::weak_ptr<State> state, std::function<void()> changed, std::function<bool()> focused)
      : state_(std::move(state)), changed_(std::move(changed)), focused_(std::move(focused)) {}
  void Update(std::shared_ptr<BrowsingProfile> profile, bool wanted);
  std::optional<int> count() const { return wanted_ ? count_ : std::nullopt; }
  void RefreshSoon();
  // Re-reads |focused|; becoming active clears the count. Call when the active tab may have changed.
  void FocusChanged();
  void Stop();
  // Internal: the profile's reddit.com cookies as a Cookie header, and each request's reply.
  void OnCookies(int generation, const std::string& header);
  void OnReply(int generation, Step step, int status, const std::string& body);
 private:
  void Schedule(int delay_ms);
  void Fetch(int generation);
  void Request(int generation, Step step);
  void Finish(int generation);
  void Set(std::optional<int> value);
  std::weak_ptr<State> state_;
  std::function<void()> changed_;
  std::function<bool()> focused_;
  bool was_focused_ = false;
  long long paused_until_ms_ = 0;  // Reddit answered 429: no request before this.
  int backoff_ms_ = 0;
  std::shared_ptr<BrowsingProfile> profile_;
  CefRefPtr<CefURLRequest> request_;
  std::optional<int> count_, chat_, bell_;
  std::string cookie_header_;  // This check's cookies; cleared when it finishes.
  int chat_status_ = 0, bell_status_ = 0;
  bool wanted_ = false, stopped_ = false;
  int generation_ = 0;  // Bumped by every schedule, profile change and stop: late replies are ignored.
  std::string logged_;  // The last logged outcome, so cef.log gets one line per change.
};
}  // namespace red
