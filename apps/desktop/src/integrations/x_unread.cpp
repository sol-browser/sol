#include "integrations/x_unread.h"

#include "app/browsing_profiles.h"
#include "app/state.h"
#include "experiments/integration_live_check.h"
#include "include/base/cef_callback.h"
#include "include/cef_cookie.h"
#include "include/cef_parser.h"
#include "include/wrapper/cef_closure_task.h"

namespace red {
namespace {
// The X web app's public client token, sent by every x.com page with the user's own session.
constexpr char kXWebBearer[] =
    "Bearer AAAAAAAAAAAAAAAAAAAAANRILgAAAAAAnNwIzUejRCOuH5E6I8xnZz4puTs%3D1Zv7ttfk8LF81IUq16cHjhLTvJu4FA33AGWWjCpTnA";
constexpr int kPollMs = 60 * 1000, kRetryMs = 3 * 60 * 1000;

// Finds the profile's ct0 cookie; reports it (or nothing) when CEF releases the visitor.
class TokenVisitor final : public CefCookieVisitor {
 public:
  TokenVisitor(std::weak_ptr<XUnread> owner, int generation) : owner_(std::move(owner)), generation_(generation) {}
  ~TokenVisitor() override {
    CefPostTask(TID_UI, base::BindOnce([](std::weak_ptr<XUnread> weak, int generation, std::string token) {
      if (const auto owner = weak.lock()) owner->OnToken(generation, token);
    }, owner_, generation_, token_));
  }
  bool Visit(const CefCookie& cookie, int, int, bool&) override {
    if (CefString(&cookie.name) == "ct0") token_ = CefString(&cookie.value).ToString();
    return true;
  }
 private:
  std::weak_ptr<XUnread> owner_;
  const int generation_;
  std::string token_;
  IMPLEMENT_REFCOUNTING(TokenVisitor);
};

class CountClient final : public CefURLRequestClient {
 public:
  CountClient(std::weak_ptr<XUnread> owner, int generation) : owner_(std::move(owner)), generation_(generation) {}
  void OnRequestComplete(CefRefPtr<CefURLRequest> request) override {
    const auto response = request->GetResponse();
    const int status = request->GetRequestStatus() == UR_SUCCESS && response ? response->GetStatus() : 0;
    CefPostTask(TID_UI, base::BindOnce([](std::weak_ptr<XUnread> weak, int generation, int code, std::string data) {
      if (const auto owner = weak.lock()) owner->OnReply(generation, code, data);
    }, owner_, generation_, status, body_));
  }
  void OnDownloadData(CefRefPtr<CefURLRequest>, const void* data, size_t length) override {
    if (body_.size() + length <= 16 * 1024) body_.append(static_cast<const char*>(data), length);
  }
  void OnUploadProgress(CefRefPtr<CefURLRequest>, int64_t, int64_t) override {}
  void OnDownloadProgress(CefRefPtr<CefURLRequest>, int64_t, int64_t) override {}
  bool GetAuthCredentials(bool, const CefString&, int, const CefString&, const CefString&,
                          CefRefPtr<CefAuthCallback>) override { return false; }
 private:
  std::weak_ptr<XUnread> owner_;
  const int generation_;
  std::string body_;
  IMPLEMENT_REFCOUNTING(CountClient);
};

void Record(const std::shared_ptr<State>& state, int status, std::optional<int> count) {
  if (!state || !IntegrationLiveCheckEnabled(state)) return;
  auto result = CefDictionaryValue::Create();
  result->SetString("status", count ? "ready" : "unsupported");
  result->SetList("items", CefListValue::Create());
  result->SetBool("composeAvailable", false);
  result->SetInt("httpStatus", status);
  if (count) result->SetInt("unread", *count);
  RecordIntegrationPreviewCheck(state, "twitter", result, false);
}
}  // namespace

std::optional<int> ParseXBadgeCount(const std::string& json) {
  if (json.empty() || json.size() > 16 * 1024) return std::nullopt;
  const auto value = CefParseJSON(json, JSON_PARSER_RFC);
  if (!value || value->GetType() != VTYPE_DICTIONARY) return std::nullopt;
  const auto d = value->GetDictionary();
  const auto number = [&](const char* key) -> std::optional<int> {
    if (d->GetType(key) == VTYPE_INT) return d->GetInt(key);
    if (d->GetType(key) == VTYPE_DOUBLE) return static_cast<int>(d->GetDouble(key));
    return std::nullopt;
  };
  if (const auto total = number("total_unread_count"); total && *total >= 0 && *total < 1000000) return total;
  const auto notifications = number("ntab_unread_count"), messages = number("dm_unread_count");
  if (notifications && messages && *notifications >= 0 && *messages >= 0 && *notifications + *messages < 1000000)
    return *notifications + *messages;
  return std::nullopt;
}

void XUnread::Update(std::shared_ptr<BrowsingProfile> profile, bool wanted) {
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
void XUnread::RefreshSoon() { if (wanted_ && !stopped_) Schedule(0); }
void XUnread::Stop() {
  stopped_ = true; wanted_ = false; ++generation_;
  if (request_) { request_->Cancel(); request_ = nullptr; }
}
void XUnread::Schedule(int delay_ms) {
  const int generation = ++generation_;
  if (request_) { request_->Cancel(); request_ = nullptr; }
  CefPostDelayedTask(TID_UI, base::BindOnce([](std::weak_ptr<XUnread> weak, int g) {
    if (const auto self = weak.lock()) self->Fetch(g);
  }, weak_from_this(), generation), delay_ms);
}
void XUnread::Fetch(int generation) {
  if (generation != generation_ || !wanted_ || stopped_ || !profile_ || !profile_->context) return;
  const auto state = state_.lock();
  if (!state) return;
  if (state->probe) {
    // Probes never reach x.com: the sidebar probe supplies the count it expects.
    OnReply(generation, state->x_unread_fixture >= 0 ? 200 : 0,
            state->x_unread_fixture >= 0 ? "{\"total_unread_count\":" + std::to_string(state->x_unread_fixture) + "}" : "");
    return;
  }
  const auto cookies = profile_->context->GetCookieManager(nullptr);
  if (!cookies || !cookies->VisitUrlCookies("https://x.com/", true, new TokenVisitor(weak_from_this(), generation)))
    OnToken(generation, {});
}
void XUnread::OnToken(int generation, const std::string& token) {
  if (generation != generation_ || !wanted_ || stopped_ || !profile_ || !profile_->context) return;
  // Signed out of X (no CSRF cookie): nothing to ask.
  if (token.empty() || token.size() > 256) { OnReply(generation, 0, {}); return; }
  auto request = CefRequest::Create();
  request->SetURL(kXBadgeCountUrl);
  request->SetMethod("GET");
  CefRequest::HeaderMap headers;
  headers.emplace("authorization", kXWebBearer);
  headers.emplace("x-csrf-token", token);
  headers.emplace("x-twitter-auth-type", "OAuth2Session");
  headers.emplace("x-twitter-active-user", "yes");
  request->SetHeaderMap(headers);
  request->SetReferrer("https://x.com/home", REFERRER_POLICY_DEFAULT);
  request->SetFlags(UR_FLAG_ALLOW_STORED_CREDENTIALS | UR_FLAG_SKIP_CACHE);
  request_ = CefURLRequest::Create(request, new CountClient(weak_from_this(), generation), profile_->context);
  if (!request_) OnReply(generation, 0, {});
}
void XUnread::OnReply(int generation, int status, const std::string& body) {
  if (generation != generation_ || stopped_) return;
  request_ = nullptr;
  const auto count = status == 200 ? ParseXBadgeCount(body) : std::nullopt;
  Record(state_.lock(), status, count);
  Set(count);
  if (wanted_) Schedule(count ? kPollMs : kRetryMs);
}
void XUnread::Set(std::optional<int> value) {
  if (value == count_) return;
  count_ = value;
  if (changed_) changed_();
}
}  // namespace red
