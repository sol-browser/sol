#include "integrations/integration_previews.h"

#include "app/browsing_profiles.h"
#include "app/session.h"
#include "experiments/google_integrations_probe.h"
#include "experiments/integration_live_check.h"
#include "integrations/gmail_feed.h"
#include "integrations/live_reader.h"
#include "windows/panel_popup.h"
#include "windows/window_close.h"
#include "workspace/controller.h"
#include "include/cef_client.h"
#include "include/base/cef_callback.h"
#include "include/wrapper/cef_closure_task.h"

namespace red {
namespace {
using namespace std::chrono_literals;
// The lease survives async browser destruction, including across workspaces.
// All operations, except test-only resource interception, run on TID_UI.
bool worker_busy = false;
class InboxClient final : public CefClient, public CefRenderHandler, public CefLifeSpanHandler,
                          public CefRequestHandler, public CefJSDialogHandler, public CefPermissionHandler,
                          public CefDownloadHandler, public CefDialogHandler {
 public:
  InboxClient(std::weak_ptr<State> state, std::weak_ptr<IntegrationPreviews> owner, std::string target)
      : state_(std::move(state)), owner_(std::move(owner)), target_(std::move(target)) {}
  CefRefPtr<CefRenderHandler> GetRenderHandler() override { return this; }
  CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return this; }
  CefRefPtr<CefRequestHandler> GetRequestHandler() override { return this; }
  CefRefPtr<CefJSDialogHandler> GetJSDialogHandler() override { return this; }
  CefRefPtr<CefPermissionHandler> GetPermissionHandler() override { return this; }
  CefRefPtr<CefDownloadHandler> GetDownloadHandler() override { return this; }
  CefRefPtr<CefDialogHandler> GetDialogHandler() override { return this; }
  void GetViewRect(CefRefPtr<CefBrowser>, CefRect& rect) override { rect = CefRect(0,0,940,680); }
  void OnPaint(CefRefPtr<CefBrowser>, PaintElementType, const RectList&, const void*, int, int) override {}
  void OnAfterCreated(CefRefPtr<CefBrowser> browser) override {
    browser->GetHost()->SetAudioMuted(true);
    if (const auto s = state_.lock()) {
      s->browsers[browser->GetIdentifier()] = browser;
      if (s->probe) {
        const auto profile = s->workspace ? s->workspace->ProfileForNode("") : nullptr;
        const auto context = browser->GetHost()->GetRequestContext();
        auto evidence = CefDictionaryValue::Create();
        evidence->SetBool("offscreen",browser->GetHost()->IsWindowRenderingDisabled() && !browser->GetHost()->GetWindowHandle());
        evidence->SetBool("same_profile",profile && context && context->IsSame(profile->context));
        s->Record("mailbox_worker_created",evidence);
      }
    }
  }
  void OnBeforeClose(CefRefPtr<CefBrowser> browser) override {
    worker_busy = false;
    if (const auto owner = owner_.lock()) owner->OnClosed(browser->GetIdentifier());
    if (const auto s = state_.lock()) {
      s->browsers.erase(browser->GetIdentifier());
      s->Record("mailbox_worker_closed");
      if (s->window_close) s->window_close->OnClosed(browser->GetIdentifier());
      if (!s->window && s->browsers.empty()) QuitIfLastWindow(s);
    }
  }
  bool OnBeforeBrowse(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
      CefRefPtr<CefRequest> request, bool, bool) override {
    if (!frame->IsMain()) return false;
    const auto url = request->GetURL().ToString();
    if (IsPreviewPage(target_,url) || IsGmailInboxSearch(target_,url,true) || IsGmailInboxSearch(target_,url,false)) return false;
    // HTTP redirects do not include the fragment. Allow only Gmail's same
    // account document to boot; the reader still requires #inbox.
    const auto source = IntegrationUrl(url), target = IntegrationUrl(target_);
    if (source && target && target->host == "mail.google.com" && source->host == target->host &&
        source->path == target->path && url.find('#') == url.npos) return false;
    // Never follow a sign-in flow, another mailbox, or an individual message.
    CefPostTask(TID_UI, base::BindOnce([](std::weak_ptr<IntegrationPreviews> weak, int id, bool sign_in) {
      if (const auto owner = weak.lock()) owner->OnFailed(id,sign_in);
    }, owner_, browser->GetIdentifier(), IsOutlookSignIn(target_,url)));
    return true;
  }
  CefRefPtr<CefResourceRequestHandler> GetResourceRequestHandler(CefRefPtr<CefBrowser>,
      CefRefPtr<CefFrame>, CefRefPtr<CefRequest> request, bool, bool, const CefString&, bool&) override {
    // The existing fixture checks both the explicit probe and disposable path.
    return IntegrationFixtureResource(state_.lock(), request->GetURL().ToString());
  }
  void OnRenderProcessTerminated(CefRefPtr<CefBrowser> browser, TerminationStatus, int, const CefString&) override {
    if (const auto owner = owner_.lock()) owner->OnFailed(browser->GetIdentifier());
  }
  bool OnBeforePopup(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, int, const CefString&, const CefString&,
      cef_window_open_disposition_t, bool, const CefPopupFeatures&, CefWindowInfo&, CefRefPtr<CefClient>&,
      CefBrowserSettings&, CefRefPtr<CefDictionaryValue>&, bool*) override { return true; }
  bool OnOpenURLFromTab(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, const CefString&, cef_window_open_disposition_t, bool) override { return true; }
  bool CanDownload(CefRefPtr<CefBrowser>, const CefString&, const CefString&) override { return false; }
  bool OnFileDialog(CefRefPtr<CefBrowser>, FileDialogMode, const CefString&, const CefString&,
      const std::vector<CefString>&, const std::vector<CefString>&, const std::vector<CefString>&,
      CefRefPtr<CefFileDialogCallback> cb) override { cb->Cancel(); return true; }
  bool OnJSDialog(CefRefPtr<CefBrowser>, const CefString&, JSDialogType, const CefString&, const CefString&,
      CefRefPtr<CefJSDialogCallback>, bool& suppress) override { suppress = true; return false; }
  bool OnBeforeUnloadDialog(CefRefPtr<CefBrowser>, const CefString&, bool, CefRefPtr<CefJSDialogCallback> cb) override {
    cb->Continue(true, ""); return true;
  }
  bool OnRequestMediaAccessPermission(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, const CefString&, uint32_t,
      CefRefPtr<CefMediaAccessCallback> cb) override { cb->Cancel(); return true; }
  bool OnShowPermissionPrompt(CefRefPtr<CefBrowser>, uint64_t, const CefString&, uint32_t,
      CefRefPtr<CefPermissionPromptCallback> cb) override { cb->Continue(CEF_PERMISSION_RESULT_DENY); return true; }
 private:
  std::weak_ptr<State> state_;
  std::weak_ptr<IntegrationPreviews> owner_;
  const std::string target_;
  IMPLEMENT_REFCOUNTING(InboxClient);
};
// Collects the Gmail feed response (at most 1 MB) and hands it to the owner on the UI thread.
class GmailFeedClient final : public CefURLRequestClient {
 public:
  GmailFeedClient(std::weak_ptr<IntegrationPreviews> owner, std::string target) : owner_(std::move(owner)), target_(std::move(target)) {}
  void OnRequestComplete(CefRefPtr<CefURLRequest> request) override {
    const auto response = request->GetResponse();
    const int status = request->GetRequestStatus() == UR_SUCCESS && response ? response->GetStatus() : 0;
    CefPostTask(TID_UI, base::BindOnce([](std::weak_ptr<IntegrationPreviews> weak, std::string target, int code, std::string data) {
      if (const auto owner = weak.lock()) owner->OnGmailFeed(target, code, data);
    }, owner_, target_, status, body_));
  }
  void OnDownloadData(CefRefPtr<CefURLRequest>, const void* data, size_t length) override {
    if (body_.size() + length <= (1u << 20)) body_.append(static_cast<const char*>(data), length);
  }
  void OnUploadProgress(CefRefPtr<CefURLRequest>, int64_t, int64_t) override {}
  void OnDownloadProgress(CefRefPtr<CefURLRequest>, int64_t, int64_t) override {}
  bool GetAuthCredentials(bool, const CefString&, int, const CefString&, const CefString&,
                          CefRefPtr<CefAuthCallback>) override { return false; }
 private:
  std::weak_ptr<IntegrationPreviews> owner_;
  const std::string target_;
  std::string body_;
  IMPLEMENT_REFCOUNTING(GmailFeedClient);
};

// The message time as the card shows it, in the user's Windows format: the time for today's
// mail, the short weekday and time otherwise (the card only lists the last week).
std::string MailTime(int64_t utc_ms) {
  // 100 ns ticks since 1601, split into FILETIME's two halves by arithmetic (not through a union).
  const ULONGLONG ticks = static_cast<ULONGLONG>(utc_ms) * 10000ULL + 116444736000000000ULL;
  FILETIME utc{}; utc.dwLowDateTime = static_cast<DWORD>(ticks & 0xFFFFFFFFULL); utc.dwHighDateTime = static_cast<DWORD>(ticks >> 32);
  SYSTEMTIME universal{}, local{}, now{};
  if (!FileTimeToSystemTime(&utc, &universal) || !SystemTimeToTzSpecificLocalTime(nullptr, &universal, &local)) return {};
  GetLocalTime(&now);
  wchar_t clock[32]{}, day[16]{};
  if (!GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT, TIME_NOSECONDS, &local, nullptr, clock, 32)) return {};
  std::wstring text = clock;
  if ((local.wYear != now.wYear || local.wMonth != now.wMonth || local.wDay != now.wDay) &&
      GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, 0, &local, L"ddd", day, 16, nullptr)) text = std::wstring(day) + L" " + text;
  const int size = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
  std::string out(size > 0 ? size : 0, '\0');
  if (size > 0) WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), out.data(), size, nullptr, nullptr);
  return out.size() <= 24 ? out : std::string();
}
}  // namespace

void IntegrationPreviews::StartGmailFeed(const std::string& target) {
  const auto s = state_.lock();
  const auto provider = GoogleIntegration(target);
  if (!s || !provider || !profile_ || !profile_->context) return;
  active_ = target;
  const auto url = provider->base + "feed/atom";
  if (const auto fixture = GmailFeedFixture(s, url); !fixture.empty()) {
    CefPostTask(TID_UI, base::BindOnce([](std::weak_ptr<IntegrationPreviews> weak, std::string t, std::string body) {
      if (const auto self = weak.lock()) self->OnGmailFeed(t, 200, body);
    }, weak_from_this(), target, fixture));
    feed_request_ = nullptr;
    return;
  }
  auto request = CefRequest::Create();
  request->SetURL(url);
  request->SetMethod("GET");
  // The profile's Google cookies identify the account; nothing else is sent.
  request->SetFlags(UR_FLAG_ALLOW_STORED_CREDENTIALS | UR_FLAG_SKIP_CACHE);
  feed_request_ = CefURLRequest::Create(request, new GmailFeedClient(weak_from_this(), target), profile_->context);
  if (!feed_request_) OnGmailFeed(target, 0, {});
}
void IntegrationPreviews::OnGmailFeed(const std::string& target, int status, const std::string& body) {
  if (target != active_) return;  // Closed or replaced meanwhile.
  feed_request_ = nullptr;
  active_.clear();
  const auto feed = status == 200 ? ParseGmailFeed(body) : std::nullopt;
  CefRefPtr<CefDictionaryValue> result;
  if (feed) {
    result = CefDictionaryValue::Create();
    result->SetString("status", "ready");
    result->SetString("mailView", "unreadAll");
    result->SetBool("composeAvailable", true);
    auto items = CefListValue::Create();
    FILETIME now{}; GetSystemTimeAsFileTime(&now);
    ULARGE_INTEGER t{}; t.LowPart = now.dwLowDateTime; t.HighPart = now.dwHighDateTime;
    const int64_t now_ms = static_cast<int64_t>((t.QuadPart - 116444736000000000ULL) / 10000ULL);
    for (const auto& entry : feed->entries) {
      if (items->GetSize() == 8) break;
      // The same week as the search used before (kGmailUnreadQuery): older unread mail stays out.
      if (now_ms - entry.issued_ms > 7LL * 24 * 3600 * 1000) continue;
      auto row = CefDictionaryValue::Create();
      row->SetString("title", entry.title.substr(0, 400));
      row->SetString("detail", entry.sender.substr(0, 400));
      row->SetString("token", entry.message_id);
      if (const auto time = MailTime(entry.issued_ms); !time.empty()) row->SetString("displayTime", time);
      items->SetDictionary(items->GetSize(), row);
    }
    result->SetList("items", items);
  }
  StoreSnapshot(target, result, false);
  Queue();
}

void IntegrationPreviews::Update(std::shared_ptr<BrowsingProfile> profile, const std::vector<std::string>& targets) {
  if (stopped_) return;
  if (profile != profile_) {
    entries_.clear(); inbox_titles_.clear(); Close(); ClearForeground();
    profile_ = std::move(profile);
  }
  std::erase_if(entries_, [&](const auto& entry) { return std::find(targets.begin(), targets.end(), entry.first) == targets.end(); });
  if (!active_.empty() && !entries_.contains(active_)) Close();
  if (foreground_browser_ && !entries_.contains(foreground_target_)) ClearForeground();
  if (profile_) for (const auto& target : targets)
    entries_.try_emplace(target, Entry{Clock::now() + 1200ms, {}, nullptr});
  Queue();
}
CefRefPtr<CefDictionaryValue> IntegrationPreviews::Cached(const std::string& target) const {
  const auto it = entries_.find(target);
  if (it == entries_.end() || !it->second.snapshot || Clock::now() - it->second.updated > 4min) return nullptr;
  return it->second.snapshot;
}
void IntegrationPreviews::Refresh(const std::string& target) {
  const auto it = entries_.find(target);
  if (it == entries_.end() || target == active_) return;
  // Coalesce repeated clicks; keep the current snapshot during a refresh.
  it->second.due = std::min(it->second.due, std::max(Clock::now(), it->second.updated + 10s));
  Queue();
}
void IntegrationPreviews::RefreshAfterMailOpen(const std::string& target) {
  const auto it = entries_.find(target);
  if (it == entries_.end()) return;
  // An in-flight worker may have loaded the inbox before the read changed.
  // Retain its previous snapshot and give the opened message time to sync.
  if (active_ == target) Close();
  it->second.due = Clock::now() + 1200ms;
  Queue();
}
void IntegrationPreviews::RefreshIfOlder(const std::string& target, std::chrono::seconds age) {
  const auto it = entries_.find(target);
  if (stopped_ || it == entries_.end() || target == active_) return;
  if (it->second.snapshot && Clock::now() - it->second.updated < age) return;
  it->second.due = std::min(it->second.due, Clock::now());
  Queue();
}
void IntegrationPreviews::ObserveMailTitle(CefRefPtr<CefBrowser> browser, const std::string& title) {
  if (stopped_ || !profile_ || !profile_->context || !browser || !browser->IsValid() ||
      !profile_->context->IsSame(browser->GetHost()->GetRequestContext())) return;
  const auto frame = browser->GetMainFrame();
  if (!frame) return;
  const auto source = frame->GetURL().ToString();
  const auto provider = LiveIntegration(source);
  const auto target = MailboxTarget(source);
  // Only the inbox view: an open message or a search has its own title.
  if (!provider || !provider->IsMail() || !target || !entries_.contains(*target) || !IsMailboxInbox(*target, source)) return;
  auto& seen = inbox_titles_[*target];
  const bool first = seen.empty();
  if (seen == title) return;
  seen = title;
  if (!first) RefreshAfterMailOpen(*target);
}
void IntegrationPreviews::ObserveOutlookInbox(CefRefPtr<CefBrowser> browser) {
  if (stopped_ || !profile_ || !profile_->context || !browser || !browser->IsValid() ||
      !profile_->context->IsSame(browser->GetHost()->GetRequestContext())) return;
  const auto frame = browser->GetMainFrame();
  if (!frame) return;
  const auto source = frame->GetURL().ToString();
  const auto provider = LiveIntegration(source);
  if (!provider || provider->id != "outlookMail") return;
  const auto target = MailboxTarget(source);
  if (!target || !entries_.contains(*target) || !IsMailboxInbox(*target,source)) return;
  const auto snapshot = Cached(*target);
  if ((snapshot && snapshot->GetString("status") == "ready") ||
      (foreground_browser_ && foreground_browser_->GetIdentifier() == browser->GetIdentifier() && foreground_source_ == source)) return;
  ClearForeground();
  foreground_browser_ = browser;
  foreground_source_ = source; foreground_target_ = *target;
  foreground_due_ = Clock::now(); foreground_deadline_ = foreground_due_ + 12s;
  Queue();
}
void IntegrationPreviews::ClearForeground() {
  if (foreground_read_) { foreground_read_->Cancel(); foreground_read_.reset(); }
  foreground_browser_ = nullptr;
  foreground_source_.clear(); foreground_target_.clear();
}
void IntegrationPreviews::ReadForeground(Clock::time_point now) {
  if (!foreground_browser_) return;
  const auto frame = foreground_browser_->IsValid() ? foreground_browser_->GetMainFrame() : nullptr;
  if (!frame || frame->GetURL() != foreground_source_ || now >= foreground_deadline_) { ClearForeground(); return; }
  if (foreground_read_ || now < foreground_due_) return;
  const auto provider = LiveIntegration(foreground_source_);
  if (!provider) { ClearForeground(); return; }
  foreground_read_ = LiveRead::Start(foreground_browser_,*provider,foreground_source_,[weak = weak_from_this()](CefRefPtr<CefDictionaryValue> result) {
    const auto self = weak.lock(); if (!self) return;
    self->foreground_read_.reset();
    if (!result || result->GetString("status") != "ready") { self->foreground_due_ = Clock::now() + 650ms; return; }
    const auto target = self->foreground_target_;
    self->ClearForeground();
    if (self->active_ == target) self->Close();
    self->StoreSnapshot(target,result,true);
  });
}
void IntegrationPreviews::Stop() {
  stopped_ = true; entries_.clear(); profile_.reset(); Close(); ClearForeground();
}
void IntegrationPreviews::Queue() {
  if (pending_ || stopped_ || entries_.empty()) return;
  pending_ = true;
  CefPostDelayedTask(TID_UI, base::BindOnce([](std::weak_ptr<IntegrationPreviews> weak) {
    if (const auto self = weak.lock()) { self->pending_ = false; self->Tick(); }
  }, weak_from_this()), 650);
}
void IntegrationPreviews::Tick() {
  const auto s = state_.lock();
  if (!s || !s->window || !s->workspace || !s->workspace->ready() || stopped_ || entries_.empty()) return;
  const auto now = Clock::now();
  ReadForeground(now);
  if (browser_ && !closing_) {
    const auto frame = browser_->GetMainFrame();
    const auto url = frame ? frame->GetURL().ToString() : std::string{};
    if (now >= deadline_) Finish(nullptr);
    else if (gmail_view_ != GmailView::Inbox && now >= search_deadline_) AdvanceGmailSearch();
    else if (!read_ && now >= next_read_ &&
             (gmail_view_==GmailView::Inbox ? IsPreviewPage(active_,url) :
                 IsGmailInboxSearch(active_,url,gmail_view_==GmailView::Unread))) {
      if (const auto provider = LiveIntegration(url)) {
        const auto id = browser_->GetIdentifier();
        read_ = LiveRead::Start(browser_, *provider, url, [weak = weak_from_this(), id](CefRefPtr<CefDictionaryValue> result) {
          const auto self = weak.lock();
          if (!self || self->closing_ || !self->browser_ || self->browser_->GetIdentifier() != id) return;
          self->read_.reset();
          const bool ready = result && result->GetString("status") == "ready";
          if (ready && result->GetList("items")->GetSize()) self->Finish(result);
          // Two empty reads in a row: nothing is unread. Mail cards show unread mail only.
          else if (ready && self->empty_ready_) self->Finish(result);
          else {
            self->empty_ready_ = ready;
            self->next_read_ = Clock::now() + 1250ms;
          }
        }, IntegrationLiveCheckEnabled(s));
      }
    }
  } else if (!browser_ && !feed_request_ && active_.empty() && !worker_busy && profile_ && profile_->initialized && profile_->context) {
    const auto next = std::min_element(entries_.begin(), entries_.end(),
        [](const auto& a, const auto& b) { return a.second.due < b.second.due; });
    if (next->second.due <= now) Start(next->first);
  }
  Queue();
}
void IntegrationPreviews::Start(const std::string& target) {
  const auto s = state_.lock();
  if (!s || worker_busy) return;
  if (const auto provider = LiveIntegration(target); provider && provider->id == "gmail") { StartGmailFeed(target); return; }
  active_ = target; worker_busy = true;
  empty_ready_ = false;
  const auto now = Clock::now();
  deadline_ = now + 16s;
  search_deadline_ = now + 7s;
  next_read_ = now + 1200ms;
  CefWindowInfo window;
  window.SetAsWindowless(nullptr);
  window.runtime_style = CEF_RUNTIME_STYLE_ALLOY;
  CefBrowserSettings settings;
  settings.windowless_frame_rate = 1;
  const auto search = GmailInboxSearch(target,true);
  gmail_view_ = search.empty() ? GmailView::Inbox : GmailView::Unread;
  const auto provider = LiveIntegration(target);
  const auto url = !search.empty() ? search : provider && provider->id == "googleCalendar" ? provider->base + "r/day" : target;
  const auto fixture = PreviewFixtureUrl(s,url);
  browser_ = CefBrowserHost::CreateBrowserSync(window, new InboxClient(s, weak_from_this(), target),
      fixture.empty() ? url : fixture, settings, nullptr, profile_->context);
  if (!browser_) { worker_busy = false; Finish(nullptr); }
}
void IntegrationPreviews::AdvanceGmailSearch() {
  // The unread search did not load in time: read the ordinary inbox's unread rows instead.
  gmail_view_ = GmailView::Inbox;
  empty_ready_ = false;
  if (read_) { read_->Cancel(); read_.reset(); }
  const auto frame = browser_ ? browser_->GetMainFrame() : nullptr;
  if (!frame) { Finish(nullptr); return; }
  const auto now = Clock::now();
  next_read_ = now + 900ms;
  search_deadline_ = now + 5s;
  const auto fixture = PreviewFixtureUrl(state_.lock(),active_);
  frame->LoadURL(fixture.empty() ? active_ : fixture);
}
void IntegrationPreviews::Finish(CefRefPtr<CefDictionaryValue> result) {
  StoreSnapshot(active_,result,false);
  Close();
}
void IntegrationPreviews::StoreSnapshot(const std::string& target, CefRefPtr<CefDictionaryValue> result, bool foreground) {
  const auto found = entries_.find(target);
  if (found != entries_.end()) {
    const auto updated = Clock::now();
    const auto provider = LiveIntegration(target);
    auto value = result ? result : CefDictionaryValue::Create();
    if (!result) {
      value->SetString("status","unsupported"); value->SetBool("composeAvailable",false);
      value->SetList("items",CefListValue::Create());
    }
    // Mail cards list unread mail only (the user's choice, 2026-11-09): rows a reader found while
    // nothing was unread ("recent", "recentAll") are read mail, which looked like a stale card.
    if (value->GetType("mailView") == VTYPE_STRING) {
      const auto view = value->GetString("mailView").ToString();
      if (view == "recent" || view == "recentAll") {
        value->SetList("items",CefListValue::Create());
        value->SetString("mailView", view == "recentAll" ? "unreadAll" : "unread");
      }
    }
    // A transient failed Outlook refresh can leave the last authenticated
    // inbox useful until it expires. Explicit sign-in redirects clear it.
    const auto previous = Cached(target);
    const bool keep = !result && provider && provider->id == "outlookMail" &&
        previous && previous->GetString("status") == "ready";
    const auto due = updated + (value->GetString("status") == "ready" ? 90s : 120s);
    if (keep) found->second.due = due;
    else {
      value->SetInt("revision", ++revision_);
      found->second = {due, updated, value};
    }
    if (const auto s = state_.lock()) {
      if (provider) RecordIntegrationPreviewCheck(s,provider->id,value,foreground);
      if (foreground && s->probe) s->Record("outlook_foreground_snapshot");
      if (!keep && s->panel_popup) s->panel_popup->Publish();
    }
  }
}
void IntegrationPreviews::Close() {
  if (read_) { read_->Cancel(); read_.reset(); }
  if (feed_request_) { feed_request_->Cancel(); feed_request_ = nullptr; }
  active_.clear();
  if (browser_ && !closing_) {
    closing_ = true;
    browser_->GetHost()->CloseBrowser(true);
  }
}
void IntegrationPreviews::OnClosed(int browser) {
  if (!browser_ || browser_->GetIdentifier() != browser) return;
  if (read_) { read_->Cancel(); read_.reset(); }
  browser_ = nullptr; closing_ = false; active_.clear();
  Queue();
}
void IntegrationPreviews::OnFailed(int browser, bool sign_in_required) {
  if (!browser_ || closing_ || browser_->GetIdentifier() != browser) return;
  auto result = sign_in_required ? CefDictionaryValue::Create() : nullptr;
  if (result) {
    result->SetString("status","signInRequired"); result->SetBool("composeAvailable",false);
    result->SetList("items",CefListValue::Create());
  }
  Finish(result);
}
}  // namespace red
