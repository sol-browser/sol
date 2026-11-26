#include "integrations/reddit_unread.h"

#include <algorithm>
#include <chrono>

#include "app/browsing_profiles.h"
#include "app/state.h"
#include "experiments/integration_live_check.h"
#include "include/base/cef_callback.h"
#include "include/base/cef_logging.h"
#include "include/cef_cookie.h"
#include "include/wrapper/cef_closure_task.h"
#include "integrations/reddit_bell.h"

namespace red {
namespace {
constexpr int kPollMs = 60 * 1000, kRetryMs = 3 * 60 * 1000;
constexpr int kBackoffMs = 10 * 60 * 1000, kMaxBackoffMs = 60 * 60 * 1000;
constexpr size_t kMaxBody = 512 * 1024;  // Each header item is a few KB.
// The sidebar probe's replies: the header items as captured on the user's account, the chat badge
// with the fixture count and the bell without one.
std::string ChatFixture(int count) {
  return R"(<reddit-chat-header-button initial-view="closed"><button rpl class="button-medium" id="header-action-item-chat-button">)"
         R"(<svg fill="currentColor" height="20" icon-name="chat" viewBox="0 0 20 20" width="20"></svg></button>)"
         R"(<div class="absolute top-0 end-0 pointer-events-none"><dynamic-badge id="header-action-item-chat-button-badge" initial-count=")" +
         std::to_string(count) + R"(" set-count-event="" appearance="ALERT"></dynamic-badge></div></reddit-chat-header-button>)";
}
constexpr char kBellFixture[] =
    R"(<a rpl class="shrink-0 button-medium" href="/notifications" id="notifications-inbox-button">)"
    R"(<svg fill="currentColor" height="20" icon-name="notifications" viewBox="0 0 20 20" width="20"><path d="M11 18h1"></path></svg></a>)";

// Collects the profile's reddit.com cookies (HttpOnly included) into one Cookie header value.
class CookieVisitor final : public CefCookieVisitor {
 public:
  CookieVisitor(std::weak_ptr<RedditUnread> owner, int generation) : owner_(std::move(owner)), generation_(generation) {}
  ~CookieVisitor() override {
    CefPostTask(TID_UI, base::BindOnce([](std::weak_ptr<RedditUnread> weak, int generation, std::string header) {
      if (const auto owner = weak.lock()) owner->OnCookies(generation, header);
    }, owner_, generation_, header_));
  }
  bool Visit(const CefCookie& cookie, int, int, bool&) override {
    const auto name = CefString(&cookie.name).ToString(), value = CefString(&cookie.value).ToString();
    if (name.empty() || name.find_first_of(";=\r\n") != std::string::npos || value.find_first_of(";\r\n") != std::string::npos) return true;
    if (header_.size() + name.size() + value.size() > 32 * 1024) return false;
    header_ += (header_.empty() ? "" : "; ") + name + "=" + value;
    return true;
  }
 private:
  std::weak_ptr<RedditUnread> owner_;
  const int generation_;
  std::string header_;
  IMPLEMENT_REFCOUNTING(CookieVisitor);
};

class Client final : public CefURLRequestClient {
 public:
  Client(std::weak_ptr<RedditUnread> owner, int generation, RedditUnread::Step step)
      : owner_(std::move(owner)), generation_(generation), step_(step) {}
  void OnRequestComplete(CefRefPtr<CefURLRequest> request) override {
    const auto response = request->GetResponse();
    const int status = request->GetRequestStatus() == UR_SUCCESS && response ? response->GetStatus() : 0;
    CefPostTask(TID_UI, base::BindOnce([](std::weak_ptr<RedditUnread> weak, int generation, RedditUnread::Step step, int code, std::string data) {
      if (const auto owner = weak.lock()) owner->OnReply(generation, step, code, data);
    }, owner_, generation_, step_, overflow_ ? 0 : status, body_));
  }
  void OnDownloadData(CefRefPtr<CefURLRequest>, const void* data, size_t length) override {
    if (body_.size() + length <= kMaxBody) body_.append(static_cast<const char*>(data), length);
    else overflow_ = true;
  }
  void OnUploadProgress(CefRefPtr<CefURLRequest>, int64_t, int64_t) override {}
  void OnDownloadProgress(CefRefPtr<CefURLRequest>, int64_t, int64_t) override {}
  bool GetAuthCredentials(bool, const CefString&, int, const CefString&, const CefString&,
                          CefRefPtr<CefAuthCallback>) override { return false; }
 private:
  std::weak_ptr<RedditUnread> owner_;
  const int generation_;
  const RedditUnread::Step step_;
  std::string body_;
  bool overflow_ = false;
  IMPLEMENT_REFCOUNTING(Client);
};

void Record(const std::shared_ptr<State>& state, int status, std::optional<int> count) {
  if (!state || !IntegrationLiveCheckEnabled(state)) return;
  auto result = CefDictionaryValue::Create();
  result->SetString("status", count ? "ready" : "unsupported");
  result->SetList("items", CefListValue::Create());
  result->SetBool("composeAvailable", false);
  result->SetInt("httpStatus", status);
  if (count) result->SetInt("unread", *count);
  RecordIntegrationPreviewCheck(state, "reddit", result, false);
}
long long NowMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
std::string Outcome(int status, std::optional<int> count) {
  return "HTTP " + std::to_string(status) + (count ? ", unread " + std::to_string(*count) : std::string(", no count"));
}
}  // namespace

void RedditUnread::Update(std::shared_ptr<BrowsingProfile> profile, bool wanted) {
  if (stopped_) return;
  const bool changed_profile = profile != profile_;
  if (changed_profile) { profile_ = std::move(profile); count_.reset(); }
  const bool was = wanted_;
  wanted_ = wanted && profile_ && profile_->context;
  if (wanted_ && (!was || changed_profile)) Schedule(changed_profile ? 1500 : 0);
  if (!wanted_ && (was || changed_profile)) {
    ++generation_;
    if (request_) { request_->Cancel(); request_ = nullptr; }
  }
  if (was != wanted_ && changed_) changed_();
}
void RedditUnread::RefreshSoon() { if (wanted_ && !stopped_) Schedule(0); }
void RedditUnread::FocusChanged() {
  const bool focused = focused_ && focused_();
  if (focused == was_focused_) return;
  was_focused_ = focused;
  if (!wanted_ || stopped_ || !focused) return;
  // A Reddit tab became active: the user is reading Reddit, so the dot goes now and nothing is
  // asked until it is in the background again (the regular check then sets it for anything new).
  Set(0);
  Schedule(kPollMs);
}
void RedditUnread::Stop() {
  stopped_ = true; wanted_ = false; ++generation_;
  if (request_) { request_->Cancel(); request_ = nullptr; }
}
void RedditUnread::Schedule(int delay_ms) {
  // Never earlier than a 429 pause allows.
  delay_ms = static_cast<int>(std::max<long long>(delay_ms, paused_until_ms_ - NowMs()));
  const int generation = ++generation_;
  if (request_) { request_->Cancel(); request_ = nullptr; }
  CefPostDelayedTask(TID_UI, base::BindOnce([](std::weak_ptr<RedditUnread> weak, int g) {
    if (const auto self = weak.lock()) self->Fetch(g);
  }, weak_from_this(), generation), delay_ms);
}
void RedditUnread::Fetch(int generation) {
  if (generation != generation_ || !wanted_ || stopped_ || !profile_ || !profile_->context) return;
  const auto state = state_.lock();
  if (!state) return;
  if (focused_ && focused_()) {
    // A Reddit tab is active: no request, no dot.
    was_focused_ = true;
    Set(0);
    Schedule(kPollMs);
    return;
  }
  chat_.reset(); bell_.reset(); chat_status_ = bell_status_ = 0;
  if (state->probe) {
    // Probes never reach Reddit: the sidebar probe supplies the replies it expects.
    const int n = state->reddit_unread_fixture;
    OnReply(generation, Step::Chat, n >= 0 ? 200 : 0, n >= 0 ? ChatFixture(n) : std::string());
    OnReply(generation, Step::Bell, n >= 0 ? 200 : 0, n >= 0 ? kBellFixture : "");
    return;
  }
  const auto cookies = profile_->context->GetCookieManager(nullptr);
  if (!cookies || !cookies->VisitUrlCookies("https://www.reddit.com/", true, new CookieVisitor(weak_from_this(), generation)))
    OnCookies(generation, {});
}
void RedditUnread::OnCookies(int generation, const std::string& header) {
  if (generation != generation_ || !wanted_ || stopped_) return;
  cookie_header_ = header;
  if (cookie_header_.empty()) {
    // Never visited Reddit in this profile: nothing to ask.
    chat_status_ = bell_status_ = 0;
    Finish(generation);
    return;
  }
  Request(generation, Step::Chat);
}
void RedditUnread::Request(int generation, Step step) {
  if (generation != generation_ || !wanted_ || stopped_ || !profile_ || !profile_->context) return;
  auto request = CefRequest::Create();
  request->SetURL(step == Step::Chat ? kRedditChatUrl : kRedditBellUrl);
  request->SetMethod("GET");
  CefRequest::HeaderMap headers;
  headers.emplace("accept", "text/html");
  headers.emplace("cookie", cookie_header_);
  request->SetHeaderMap(headers);
  request->SetReferrer("https://www.reddit.com/", REFERRER_POLICY_DEFAULT);
  // No UR_FLAG_ALLOW_STORED_CREDENTIALS: the cookies are sent explicitly and the reply's Set-Cookie
  // headers are dropped, so an open Reddit page keeps its session and CSRF cookies.
  request->SetFlags(UR_FLAG_SKIP_CACHE | UR_FLAG_STOP_ON_REDIRECT);
  request_ = CefURLRequest::Create(request, new Client(weak_from_this(), generation, step), profile_->context);
  if (!request_) OnReply(generation, step, 0, {});
}
void RedditUnread::OnReply(int generation, Step step, int status, const std::string& body) {
  if (generation != generation_ || stopped_) return;
  request_ = nullptr;
  if (step == Step::Chat) {
    chat_status_ = status;
    chat_ = status == 200 ? RedditChatCount(body) : std::nullopt;
    const auto state = state_.lock();
    if (status == 429) { Finish(generation); return; }  // Rate-limited: do not ask again now.
    if (!state || !state->probe) Request(generation, Step::Bell);
    return;
  }
  bell_status_ = status;
  bell_ = status == 200 ? RedditBellCount(body) : std::nullopt;
  Finish(generation);
}
void RedditUnread::Finish(int generation) {
  if (generation != generation_ || stopped_) return;
  cookie_header_.clear();
  std::optional<int> count;
  if (chat_ || bell_) count = std::min(999999, chat_.value_or(0) + bell_.value_or(0));
  // One cef.log line per change of outcome: statuses and counts only, never the replies.
  const auto outcome = "chats " + Outcome(chat_status_, chat_) + "; bell " + Outcome(bell_status_, bell_);
  if (outcome != logged_) { LOG(WARNING) << "Reddit check: " << outcome; logged_ = outcome; }
  Record(state_.lock(), bell_status_ ? bell_status_ : chat_status_, count);
  const bool limited = chat_status_ == 429 || bell_status_ == 429;
  if (limited) {
    // Too many requests: pause, keep the last count, and back off further if it repeats.
    backoff_ms_ = backoff_ms_ ? std::min(backoff_ms_ * 2, kMaxBackoffMs) : kBackoffMs;
    paused_until_ms_ = NowMs() + backoff_ms_;
  } else {
    backoff_ms_ = 0;
    // A Reddit tab that became active while this check ran keeps the dot cleared.
    was_focused_ = focused_ && focused_();
    Set(was_focused_ && count ? 0 : count);
  }
  if (wanted_) Schedule(limited ? backoff_ms_ : !count ? kRetryMs : kPollMs);
}
void RedditUnread::Set(std::optional<int> value) {
  if (value == count_) return;
  count_ = value;
  if (changed_) changed_();
}
}  // namespace red
