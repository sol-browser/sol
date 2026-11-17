#include "app/content_blocker_bridge.h"
#include "app/session.h"
#include "app/state.h"
#include "trust_policy.h"
#include "windows/window_close.h"
#include "include/base/cef_callback.h"
#include "include/cef_client.h"
#include "include/cef_devtools_message_observer.h"
#include "include/cef_parser.h"
#include "include/wrapper/cef_closure_task.h"

namespace red {
namespace {
constexpr int kIdleCloseMs = 60 * 1000;
constexpr int kCallTimeoutMs = 20 * 1000;
std::string PageUrl() { return std::string("chrome-extension://") + kContentBlockerExtensionId + "/no-dashboard.html"; }

// The hidden uBO page: no window, sound, popups, dialogs, downloads, permission
// prompts, or navigation away from that one page.
class BridgeClient final : public CefClient, public CefRenderHandler, public CefLifeSpanHandler, public CefLoadHandler,
                           public CefRequestHandler, public CefJSDialogHandler, public CefPermissionHandler,
                           public CefDownloadHandler, public CefDialogHandler {
 public:
  BridgeClient(std::weak_ptr<State> state, std::weak_ptr<ContentBlockerBridge> owner) : state_(std::move(state)), owner_(std::move(owner)) {}
  CefRefPtr<CefRenderHandler> GetRenderHandler() override { return this; }
  CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return this; }
  CefRefPtr<CefLoadHandler> GetLoadHandler() override { return this; }
  CefRefPtr<CefRequestHandler> GetRequestHandler() override { return this; }
  CefRefPtr<CefJSDialogHandler> GetJSDialogHandler() override { return this; }
  CefRefPtr<CefPermissionHandler> GetPermissionHandler() override { return this; }
  CefRefPtr<CefDownloadHandler> GetDownloadHandler() override { return this; }
  CefRefPtr<CefDialogHandler> GetDialogHandler() override { return this; }
  void GetViewRect(CefRefPtr<CefBrowser>, CefRect& rect) override { rect = CefRect(0, 0, 320, 240); }
  void OnPaint(CefRefPtr<CefBrowser>, PaintElementType, const RectList&, const void*, int, int) override {}
  void OnAfterCreated(CefRefPtr<CefBrowser> browser) override {
    // Not one of a window's browsers: its owner closes it (idle, Stop()).
    browser->GetHost()->SetAudioMuted(true);
    if (const auto owner = owner_.lock()) owner->OnCreated(browser);
  }
  bool OnBeforePopup(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, int, const CefString&, const CefString&,
      cef_window_open_disposition_t, bool, const CefPopupFeatures&, CefWindowInfo&, CefRefPtr<CefClient>&,
      CefBrowserSettings&, CefRefPtr<CefDictionaryValue>&, bool*) override { return true; }
  void OnBeforeClose(CefRefPtr<CefBrowser> browser) override {
    if (const auto owner = owner_.lock()) owner->OnClosed(browser->GetIdentifier());
  }
  void OnLoadEnd(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, int) override {
    if (frame->IsMain()) if (const auto owner = owner_.lock()) owner->OnLoaded(browser->GetIdentifier(), frame->GetURL() == PageUrl());
  }
  void OnLoadError(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, ErrorCode code, const CefString&, const CefString&) override {
    if (frame->IsMain() && code != ERR_ABORTED) if (const auto owner = owner_.lock()) owner->OnLoaded(browser->GetIdentifier(), false);
  }
  bool OnBeforeBrowse(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame> frame, CefRefPtr<CefRequest> request, bool, bool) override {
    return frame->IsMain() && request->GetURL().ToString() != PageUrl();
  }
  bool OnJSDialog(CefRefPtr<CefBrowser>, const CefString&, JSDialogType, const CefString&, const CefString&,
                  CefRefPtr<CefJSDialogCallback>, bool& suppress) override { suppress = true; return false; }
  bool OnBeforeUnloadDialog(CefRefPtr<CefBrowser>, const CefString&, bool, CefRefPtr<CefJSDialogCallback> callback) override {
    callback->Continue(true, ""); return true;
  }
  bool OnShowPermissionPrompt(CefRefPtr<CefBrowser>, uint64_t, const CefString&, uint32_t,
                              CefRefPtr<CefPermissionPromptCallback> callback) override {
    callback->Continue(CEF_PERMISSION_RESULT_DENY); return true;
  }
  bool CanDownload(CefRefPtr<CefBrowser>, const CefString&, const CefString&) override { return false; }
  bool OnBeforeDownload(CefRefPtr<CefBrowser>, CefRefPtr<CefDownloadItem>, const CefString&,
                        CefRefPtr<CefBeforeDownloadCallback>) override { return true; }
  bool OnFileDialog(CefRefPtr<CefBrowser>, FileDialogMode, const CefString&, const CefString&, const std::vector<CefString>&,
                    const std::vector<CefString>&, const std::vector<CefString>&, CefRefPtr<CefFileDialogCallback> callback) override {
    callback->Cancel(); return true;
  }
 private:
  std::weak_ptr<State> state_;
  std::weak_ptr<ContentBlockerBridge> owner_;
  IMPLEMENT_REFCOUNTING(BridgeClient);
};

class BridgeObserver final : public CefDevToolsMessageObserver {
 public:
  explicit BridgeObserver(std::weak_ptr<ContentBlockerBridge> owner) : owner_(std::move(owner)) {}
  void OnDevToolsMethodResult(CefRefPtr<CefBrowser>, int message_id, bool success, const void* result, size_t size) override {
    std::string json(static_cast<const char*>(result), size);
    CefPostTask(TID_UI, base::BindOnce([](std::weak_ptr<ContentBlockerBridge> owner, int id, bool ok, std::string json) {
      if (const auto self = owner.lock()) self->OnResult(id, ok, json);
    }, owner_, message_id, success, std::move(json)));
  }
 private:
  std::weak_ptr<ContentBlockerBridge> owner_;
  IMPLEMENT_REFCOUNTING(BridgeObserver);
};
}  // namespace

std::string ContentBlockerExpression(const std::string& channel_json, const std::string& message_json) {
  // "tabUrl" (Sol's own field) is resolved to uBO's tab ID first: the hidden
  // page has no current tab of its own.
  return "(async () => { if (typeof vAPI !== 'object' || !vAPI.messaging) throw new Error('uBlock Origin is not ready');"
         " const message = " + message_json + ";"
         " if (typeof message.tabUrl === 'string') { const tabs = await chrome.tabs.query({});"
         " const tab = tabs.find(t => t.url === message.tabUrl); delete message.tabUrl;"
         " if (!tab) return JSON.stringify(null); message.tabId = tab.id; }"
         " const reply = await vAPI.messaging.send(" + channel_json + ", message);"
         " return JSON.stringify(reply === undefined ? null : reply); })()";
}

void ContentBlockerBridge::Send(const std::string& channel, CefRefPtr<CefDictionaryValue> message, Reply done) {
  if (stopped_ || !context_) { done(nullptr, "The ad blocker is unavailable"); return; }
  if ((channel != "dashboard" && channel != "popupPanel") || !message || message->GetType("what") != VTYPE_STRING) {
    done(nullptr, "Invalid ad blocker command"); return;
  }
  auto channel_value = CefValue::Create(); channel_value->SetString(channel);
  auto message_value = CefValue::Create(); message_value->SetDictionary(message->Copy(false));
  queued_.push_back({ContentBlockerExpression(CefWriteJSON(channel_value, JSON_WRITER_DEFAULT).ToString(),
                                              CefWriteJSON(message_value, JSON_WRITER_DEFAULT).ToString()), std::move(done)});
  if (loaded_) Flush(); else Open();
}
void ContentBlockerBridge::Open() {
  if (opening_ || browser_ || stopped_) return;
  const auto state = state_.lock(); if (!state) { Fail("The ad blocker is unavailable"); return; }
  opening_ = true;
  CefWindowInfo window;
  window.SetAsWindowless(nullptr);
  window.runtime_style = CEF_RUNTIME_STYLE_ALLOY;
  CefBrowserSettings settings;
  settings.windowless_frame_rate = 1;
  if (!CefBrowserHost::CreateBrowserSync(window, new BridgeClient(state_, weak_from_this()), PageUrl(), settings, nullptr, context_)) {
    opening_ = false; Fail("The ad blocker could not be reached");
  }
}
void ContentBlockerBridge::OnCreated(CefRefPtr<CefBrowser> browser) {
  browser_ = browser; opening_ = false;
  observer_ = browser->GetHost()->AddDevToolsMessageObserver(new BridgeObserver(weak_from_this()));
}
void ContentBlockerBridge::OnLoaded(int browser, bool ok) {
  if (!browser_ || browser_->GetIdentifier() != browser) return;
  // A missing extension (blocker off, or not registered yet) fails to load.
  if (!ok) { Fail("The ad blocker is off or still starting"); Stop(); stopped_ = false; return; }
  loaded_ = true; Flush();
}
void ContentBlockerBridge::Flush() {
  if (!loaded_ || !browser_) return;
  for (auto& call : queued_) {
    auto params = CefDictionaryValue::Create();
    params->SetString("expression", call.expression);
    params->SetBool("awaitPromise", true); params->SetBool("returnByValue", true);
    const int id = browser_->GetHost()->ExecuteDevToolsMethod(0, "Runtime.evaluate", params);
    if (!id) { call.done(nullptr, "The ad blocker could not be reached"); continue; }
    waiting_[id] = std::move(call.done);
    CefPostDelayedTask(TID_UI, base::BindOnce([](std::weak_ptr<ContentBlockerBridge> weak, int id) {
      if (const auto self = weak.lock()) if (auto it = self->waiting_.find(id); it != self->waiting_.end()) {
        auto done = std::move(it->second); self->waiting_.erase(it); done(nullptr, "The ad blocker did not answer");
      }
    }, weak_from_this(), id), kCallTimeoutMs);
  }
  queued_.clear();
  ScheduleIdleClose();
}
void ContentBlockerBridge::OnResult(int message, bool ok, const std::string& json) {
  const auto it = waiting_.find(message); if (it == waiting_.end()) return;
  auto done = std::move(it->second); waiting_.erase(it);
  const auto parsed = ok ? CefParseJSON(json, JSON_PARSER_RFC) : nullptr;
  const auto outer = parsed && parsed->GetType() == VTYPE_DICTIONARY ? parsed->GetDictionary() : nullptr;
  if (!outer || outer->HasKey("exceptionDetails")) { done(nullptr, "The ad blocker reported an error"); return; }
  const auto result = outer->GetDictionary("result");
  if (!result || result->GetString("type") != "string") { done(nullptr, "Unexpected ad blocker reply"); return; }
  const auto reply = CefParseJSON(result->GetString("value"), JSON_PARSER_RFC);
  if (!reply) { done(nullptr, "Unexpected ad blocker reply"); return; }
  done(reply, "");
}
void ContentBlockerBridge::ScheduleIdleClose() {
  const int generation = ++idle_generation_;
  CefPostDelayedTask(TID_UI, base::BindOnce([](std::weak_ptr<ContentBlockerBridge> weak, int generation) {
    const auto self = weak.lock();
    if (!self || self->idle_generation_ != generation || !self->waiting_.empty() || !self->queued_.empty()) return;
    if (self->browser_) self->browser_->GetHost()->CloseBrowser(true);
  }, weak_from_this(), generation), kIdleCloseMs);
}
void ContentBlockerBridge::Fail(const std::string& error) {
  auto queued = std::move(queued_); queued_.clear();
  for (auto& call : queued) call.done(nullptr, error);
  auto waiting = std::move(waiting_); waiting_.clear();
  for (auto& [id, done] : waiting) done(nullptr, error);
}
void ContentBlockerBridge::OnClosed(int browser) {
  if (!browser_ || browser_->GetIdentifier() != browser) return;
  browser_ = nullptr; observer_ = nullptr; loaded_ = opening_ = false;
  Fail("The ad blocker page closed");
}
void ContentBlockerBridge::Stop() {
  stopped_ = true; ++idle_generation_;
  Fail("The ad blocker is unavailable");
  if (browser_) browser_->GetHost()->CloseBrowser(true);
}
}  // namespace red
