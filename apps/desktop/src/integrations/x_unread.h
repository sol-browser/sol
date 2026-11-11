#pragma once
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include "include/cef_urlrequest.h"

namespace red {
struct State;
struct BrowsingProfile;

// X's unread count without loading x.com (~180 MB for a background tab, docs/memory.md). Sol asks
// the same counter the X web app polls, with the profile's own session cookies:
//   GET https://x.com/i/api/2/badge_count/badge_count.json
// It is not a documented API (the user chose it knowing it can break, 2026-11-10). On any failure
// the count is unknown and the sidebar falls back to a loaded X tab's title.
inline constexpr char kXBadgeCountUrl[] = "https://x.com/i/api/2/badge_count/badge_count.json?supports_ntab_urt=1";

// The total unread count from the counter's JSON ({"ntab_unread_count":N,"dm_unread_count":M,
// "total_unread_count":T,...}); nullopt when the reply has another shape.
std::optional<int> ParseXBadgeCount(const std::string& json);

// UI thread. One per workspace window; polls only while the profile has a pinned X home tab with
// its badge on and the X integration enabled.
class XUnread : public std::enable_shared_from_this<XUnread> {
 public:
  XUnread(std::weak_ptr<State> state, std::function<void()> changed) : state_(std::move(state)), changed_(std::move(changed)) {}
  void Update(std::shared_ptr<BrowsingProfile> profile, bool wanted);
  std::optional<int> count() const { return wanted_ ? count_ : std::nullopt; }
  void RefreshSoon();
  void Stop();
  // Internal: the profile's ct0 cookie (X's CSRF token) and the counter's reply.
  void OnToken(int generation, const std::string& token);
  void OnReply(int generation, int status, const std::string& body);
 private:
  void Schedule(int delay_ms);
  void Fetch(int generation);
  void Set(std::optional<int> value);
  std::weak_ptr<State> state_;
  std::function<void()> changed_;
  std::shared_ptr<BrowsingProfile> profile_;
  CefRefPtr<CefURLRequest> request_;
  std::optional<int> count_;
  bool wanted_ = false, stopped_ = false;
  int generation_ = 0;  // Bumped by every schedule, profile change and stop: late replies are ignored.
};
}  // namespace red
