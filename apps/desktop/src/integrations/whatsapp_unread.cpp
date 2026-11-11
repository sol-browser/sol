#include "integrations/whatsapp_unread.h"

#include "app/browsing_profiles.h"
#include "app/session.h"
#include "app/state.h"
#include "experiments/google_integrations_probe.h"
#include "experiments/integration_live_check.h"
#include "integrations/providers.h"
#include "windows/window_close.h"
#include "include/base/cef_callback.h"
#include "include/cef_client.h"
#include "include/wrapper/cef_closure_task.h"

namespace red {
namespace {
using namespace std::chrono_literals;
constexpr int kIntervalMs = 5 * 60 * 1000;  // Between checks while no WhatsApp tab is loaded.
constexpr int kFirstMs = 5000;              // After startup or after the WhatsApp tab unloads.

// The hidden WhatsApp Web: no window, no sound, no popups, dialogs, downloads, permission
// prompts or navigation away from web.whatsapp.com. It reports its title and its closing.
class WhatsAppClient final : public CefClient, public CefRenderHandler, public CefLifeSpanHandler, public CefDisplayHandler,
                             public CefRequestHandler, public CefJSDialogHandler, public CefPermissionHandler,
                             public CefDownloadHandler, public CefDialogHandler {
 public:
  WhatsAppClient(std::weak_ptr<State> state, std::weak_ptr<WhatsAppUnread> owner) : state_(std::move(state)), owner_(std::move(owner)) {}
  CefRefPtr<CefRenderHandler> GetRenderHandler() override { return this; }
  CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return this; }
  CefRefPtr<CefDisplayHandler> GetDisplayHandler() override { return this; }
  CefRefPtr<CefRequestHandler> GetRequestHandler() override { return this; }
  CefRefPtr<CefJSDialogHandler> GetJSDialogHandler() override { return this; }
  CefRefPtr<CefPermissionHandler> GetPermissionHandler() override { return this; }
  CefRefPtr<CefDownloadHandler> GetDownloadHandler() override { return this; }
  CefRefPtr<CefDialogHandler> GetDialogHandler() override { return this; }
  void GetViewRect(CefRefPtr<CefBrowser>, CefRect& rect) override { rect = CefRect(0, 0, 900, 640); }
  void OnPaint(CefRefPtr<CefBrowser>, PaintElementType, const RectList&, const void*, int, int) override {}
  void OnAfterCreated(CefRefPtr<CefBrowser> browser) override {
    browser->GetHost()->SetAudioMuted(true);
    if (const auto s = state_.lock()) { s->browsers[browser->GetIdentifier()] = browser; s->Record("whatsapp_worker_created"); }
  }
  void OnBeforeClose(CefRefPtr<CefBrowser> browser) override {
    if (const auto owner = owner_.lock()) owner->OnClosed(browser->GetIdentifier());
    if (const auto s = state_.lock()) {
      s->browsers.erase(browser->GetIdentifier());
      s->Record("whatsapp_worker_closed");
      if (s->window_close) s->window_close->OnClosed(browser->GetIdentifier());
      if (!s->window && s->browsers.empty()) QuitIfLastWindow(s);
    }
  }
  void OnTitleChange(CefRefPtr<CefBrowser> browser, const CefString& title) override {
    if (const auto owner = owner_.lock()) owner->OnTitle(browser->GetIdentifier(), title.ToString());
  }
  bool OnBeforeBrowse(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame> frame, CefRefPtr<CefRequest> request, bool, bool) override {
    if (!frame->IsMain()) return false;
    const auto address = IntegrationUrl(request->GetURL().ToString());
    return !address || address->host != "web.whatsapp.com";
  }
  CefRefPtr<CefResourceRequestHandler> GetResourceRequestHandler(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>,
      CefRefPtr<CefRequest> request, bool, bool, const CefString&, bool&) override {
    return IntegrationFixtureResource(state_.lock(), request->GetURL().ToString());  // Probes only.
  }
  void OnRenderProcessTerminated(CefRefPtr<CefBrowser> browser, TerminationStatus, int, const CefString&) override {
    browser->GetHost()->CloseBrowser(true);
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
  std::weak_ptr<WhatsAppUnread> owner_;
  IMPLEMENT_REFCOUNTING(WhatsAppClient);
};
}  // namespace

void WhatsAppUnread::Update(std::shared_ptr<BrowsingProfile> profile, bool wanted, bool tab_loaded) {
  if (stopped_) return;
  const bool changed_profile = profile != profile_;
  if (changed_profile) { profile_ = std::move(profile); count_.reset(); }
  const bool was_wanted = wanted_, was_loaded = tab_loaded_;
  wanted_ = wanted && profile_ && profile_->context;
  tab_loaded_ = tab_loaded;
  // An open WhatsApp tab owns the session: never compete with it.
  if (browser_ && (!wanted_ || tab_loaded_ || changed_profile)) Finish(std::nullopt);
  if (!wanted_ || tab_loaded_) ++generation_;  // Cancels a scheduled check.
  else if (!was_wanted || was_loaded || changed_profile) {
    const auto state = state_.lock();
    Schedule(state && state->probe ? 1000 : kFirstMs);
  }
  if (was_wanted != wanted_ && changed_) changed_();
}
void WhatsAppUnread::Stop() {
  stopped_ = true; wanted_ = false; ++generation_;
  if (browser_) Finish(std::nullopt);
}
void WhatsAppUnread::Schedule(int delay_ms) {
  const int generation = ++generation_;
  CefPostDelayedTask(TID_UI, base::BindOnce([](std::weak_ptr<WhatsAppUnread> weak, int g) {
    if (const auto self = weak.lock()) self->Start(g);
  }, weak_from_this(), generation), delay_ms);
}
void WhatsAppUnread::Start(int generation) {
  if (generation != generation_ || !wanted_ || tab_loaded_ || stopped_ || !profile_ || !profile_->context) return;
  if (browser_) {  // The previous check is still closing.
    CefPostDelayedTask(TID_UI, base::BindOnce([](std::weak_ptr<WhatsAppUnread> weak, int g) {
      if (const auto self = weak.lock()) self->Start(g);
    }, weak_from_this(), generation), 1000);
    return;
  }
  const auto state = state_.lock();
  if (!state) return;
  CefWindowInfo window;
  window.SetAsWindowless(nullptr);
  window.runtime_style = CEF_RUNTIME_STYLE_ALLOY;
  CefBrowserSettings settings;
  settings.windowless_frame_rate = 1;
  seen_.reset();
  closing_ = false;
  deadline_ = Clock::now() + 30s;  // WhatsApp syncs before its title shows the count.
  settle_ = deadline_;
  browser_ = CefBrowserHost::CreateBrowserSync(window, new WhatsAppClient(state, weak_from_this()),
      "https://web.whatsapp.com/", settings, nullptr, profile_->context);
  if (!browser_) { Schedule(kIntervalMs); return; }
  CefPostDelayedTask(TID_UI, base::BindOnce([](std::weak_ptr<WhatsAppUnread> weak, int g) {
    if (const auto self = weak.lock()) self->Tick(g);
  }, weak_from_this(), generation), 500);
}
void WhatsAppUnread::OnTitle(int browser, const std::string& title) {
  if (!browser_ || browser_->GetIdentifier() != browser || closing_) return;
  const auto value = WhatsAppTitleCount(title);
  if (!value) return;
  seen_ = value;
  // A count can still grow while chats sync: take it once it has been stable for a moment.
  if (*value > 0) settle_ = std::min(deadline_, Clock::now() + 2500ms);
}
void WhatsAppUnread::Tick(int generation) {
  if (generation != generation_ || !browser_ || closing_) return;
  const auto now = Clock::now();
  // "WhatsApp" alone is also the title before syncing, so zero is only trusted at the deadline.
  if ((seen_ && *seen_ > 0 && now >= settle_) || now >= deadline_) { Finish(seen_); return; }
  CefPostDelayedTask(TID_UI, base::BindOnce([](std::weak_ptr<WhatsAppUnread> weak, int g) {
    if (const auto self = weak.lock()) self->Tick(g);
  }, weak_from_this(), generation), 500);
}
void WhatsAppUnread::Finish(std::optional<int> result) {
  if (browser_ && !closing_) { closing_ = true; browser_->GetHost()->CloseBrowser(true); }
  if (const auto state = state_.lock()) {
    auto details = CefDictionaryValue::Create();
    details->SetBool("read", result.has_value());
    if (result) details->SetInt("unread", *result);
    if (state->probe) state->Record("whatsapp_check", details);
    if (IntegrationLiveCheckEnabled(state)) {
      auto check = CefDictionaryValue::Create();
      check->SetString("status", result ? "ready" : "unsupported");
      check->SetList("items", CefListValue::Create());
      check->SetBool("composeAvailable", false);
      if (result) check->SetInt("unread", *result);
      RecordIntegrationPreviewCheck(state, "whatsapp", check, false);
    }
  }
  if (result) Set(result);  // A check that could not read keeps the last count.
  if (wanted_ && !tab_loaded_ && !stopped_) Schedule(kIntervalMs);
}
void WhatsAppUnread::OnClosed(int browser) {
  if (browser_ && browser_->GetIdentifier() == browser) { browser_ = nullptr; closing_ = false; }
}
void WhatsAppUnread::Set(std::optional<int> value) {
  if (value == count_) return;
  count_ = value;
  if (changed_) changed_();
}
}  // namespace red
