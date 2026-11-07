#include "windows/embedded_content.h"
#include "windows/focus.h"
#include "include/cef_devtools_message_observer.h"
#include "include/cef_parser.h"
#include "include/base/cef_callback.h"
#include "include/wrapper/cef_closure_task.h"
#include <algorithm>

namespace red {
namespace {
class PresentationObserver final : public CefDevToolsMessageObserver {
 public:
  explicit PresentationObserver(std::weak_ptr<State> state) : state_(std::move(state)) {}
  void OnDevToolsEvent(CefRefPtr<CefBrowser> browser,const CefString& method,const void* data,size_t size) override {
    if (size>65536 || (method!="Page.frameNavigated" && method!="Page.lifecycleEvent")) return;
    CefPostTask(TID_UI,base::BindOnce([](std::weak_ptr<State> weak,CefRefPtr<CefBrowser> browser,std::string method,std::string json) {
      if (const auto state=weak.lock(); state && state->embedded) state->embedded->OnPresentationEvent(browser,method,json);
    },state_,browser,method.ToString(),std::string(static_cast<const char*>(data),size)));
  }
  void OnDevToolsMethodResult(CefRefPtr<CefBrowser> browser, int id, bool success, const void* data, size_t size) override {
    // Replies can be synchronous; match the returned request ID after the
    // caller has stored it. Never retain CEF's callback-scoped buffer.
    const bool bounded = size <= 24 * 1024 * 1024;
    std::string result;
    if (bounded && size) result.assign(static_cast<const char*>(data), size);
    CefPostTask(TID_UI, base::BindOnce([](std::weak_ptr<State> weak, CefRefPtr<CefBrowser> browser,
        int id, bool success, std::string result) {
      if (auto s = weak.lock(); s && s->embedded)
        s->embedded->OnPresentationResult(browser, id, success, result.data(), result.size());
    }, state_, browser, id, success && bounded, std::move(result)));
  }
 private:
  std::weak_ptr<State> state_;
  IMPLEMENT_REFCOUNTING(PresentationObserver);
};
}
void EmbeddedContent::ObservePresentation(CefRefPtr<CefBrowser> browser) {
  auto& page = presentations_[browser->GetIdentifier()];
  page.registration = browser->GetHost()->AddDevToolsMessageObserver(new PresentationObserver(state_));
}
void EmbeddedContent::ShowColdCover() {
  // As in Arc: nothing stands in for the page; the parked page alone renders.
  cold_cover_ = true;
  for (const auto& [id, browser] : browsers_)
    if (!(parked_ && id == presenting_id_)) ShowWindow(browser->GetHost()->GetWindowHandle(), SW_HIDE);
  UpdateCorners();
}
void EmbeddedContent::BlockPresentationInput(HWND handle) {
  if (handle && IsWindowEnabled(handle)) {
    EnableWindow(handle, FALSE);
    presentation_disabled_.insert(handle);
  }
}
void EmbeddedContent::ShowPendingColdTab() {
  const auto state = state_.lock();
  if (!state || !state->content || HasLinkPeek() || HasPendingColdTab()) return;
  const bool already_visible = cold_cover_;
  if (presenting_id_ != -1) FinishPresentation(presentation_generation_, false, false, true);
  presentation_backdrop_id_ = -1;
  ShowColdCover();
  FocusShell(state);
  if (!already_visible) state->Record("page_transition_gradient_shown");
}
void EmbeddedContent::CancelPendingColdTab() {
  if (HasPendingColdTab()) FinishPresentation(presentation_generation_, false, false);
}
void EmbeddedContent::BeginPresentation(CefRefPtr<CefBrowser> browser, bool cold) {
  const auto state = state_.lock();
  if (!state) return;
  presenting_id_ = browser->GetIdentifier();
  cold_presentation_ = cold;
  if (cold || cold_cover_) {
    const bool already_visible = cold_cover_;
    ShowColdCover();
    if (!already_visible) state->Record("page_transition_gradient_shown");
  }
  presentation_input_ = browser->GetHost()->GetWindowHandle();
  BlockPresentationInput(presentation_input_);
  FocusShell(state);
  if (!cold) {
    CefPostDelayedTask(TID_UI, base::BindOnce([](std::weak_ptr<State> weak, unsigned long long generation) {
      if (auto s = weak.lock(); s && s->embedded) s->embedded->CheckPresentationTimeout(generation);
    }, state_, presentation_generation_), 400);
    AwaitIncomingFrame(browser);
    return;
  }

  // Blink's firstPaint excludes the default background and includes a site-
  // painted background. Ignore about:blank and match the main document loader.
  auto& page = presentations_[presenting_id_];
  page.waiting_for_content = true;
  page.saw_loading = browser->IsLoading();
  auto host = browser->GetHost();
  host->ExecuteDevToolsMethod(0, "Page.enable", nullptr);
  page.frame_query = host->ExecuteDevToolsMethod(0, "Page.getFrameTree", nullptr);
  auto params = CefDictionaryValue::Create(); params->SetBool("enabled", true);
  page.watching_paint = host->ExecuteDevToolsMethod(0, "Page.setLifecycleEventsEnabled", params) != 0;
  state->Record("page_transition_waiting_content");
}
void EmbeddedContent::OnPresentationEvent(CefRefPtr<CefBrowser> browser, const std::string& method, const std::string& json) {
  OnLinkPeekEvent(browser, method, json);
  const auto found = presentations_.find(browser->GetIdentifier());
  if (browser->GetIdentifier() != presenting_id_ || found == presentations_.end() || !found->second.waiting_for_content) return;
  const auto value = CefParseJSON(json, JSON_PARSER_RFC);
  if (!value || value->GetType() != VTYPE_DICTIONARY) return;
  auto& page = found->second;
  const auto params = value->GetDictionary();
  if (method == "Page.frameNavigated") {
    const auto frame = params->GetDictionary("frame");
    if (frame && !frame->HasKey("parentId")) {
      const auto url = frame->GetString("url");
      page.main_frame = frame->GetString("id");
      page.loader = url.empty() || url == "about:blank" ? "" : frame->GetString("loaderId").ToString();
    }
  } else if (!page.main_frame.empty() && !page.loader.empty() &&
             params->GetString("frameId") == page.main_frame && params->GetString("loaderId") == page.loader) {
    const auto name = params->GetString("name");
    if (name == "firstPaint") PresentationContentReady(browser, "firstPaint", false);
    else if (name == "firstContentfulPaint") PresentationContentReady(browser, "firstContentfulPaint", false);
  }
}
void EmbeddedContent::OnPresentationLoadFinished(CefRefPtr<CefBrowser> browser) {
  const auto frame = browser->GetMainFrame();
  if (frame && !frame->GetURL().empty() && frame->GetURL() != "about:blank") PresentationContentReady(browser, "loadEnd", true);
}
void EmbeddedContent::OnPresentationLoading(CefRefPtr<CefBrowser> browser, bool loading) {
  const auto found = presentations_.find(browser->GetIdentifier());
  if (browser->GetIdentifier() != presenting_id_ || found == presentations_.end() || !found->second.waiting_for_content) return;
  if (loading) found->second.saw_loading = true;
  else if (found->second.saw_loading) PresentationContentReady(browser, "loadStopped", true);
}
void EmbeddedContent::PresentationContentReady(CefRefPtr<CefBrowser> browser, const char* source, bool load_finished) {
  const int id = browser->GetIdentifier();
  const auto found = presentations_.find(id);
  if (id != presenting_id_ || found == presentations_.end() || !found->second.waiting_for_content) return;
  auto& page = found->second;
  if (load_finished && page.load_ended) return;
  if (const auto state = state_.lock()) {
    auto details = CefDictionaryValue::Create();
    details->SetInt("browser_id", id);
    details->SetBool("load_finished", load_finished);
    details->SetString("source", source);
    details->SetDouble("ready_ms", static_cast<double>(GetTickCount64() - page.activated_ms));
    state->Record("page_transition_content_ready", details);
  }
  if (!load_finished) { ConfirmPresentationFrame(id); return; }
  // Load completion can precede paint. Keep listening while copying a surface
  // for empty documents, canceled loads, or a missed paint event.
  page.load_ended = true;
  AwaitIncomingFrame(browser);
}
void EmbeddedContent::AwaitIncomingFrame(CefRefPtr<CefBrowser> browser) {
  auto& page = presentations_.at(browser->GetIdentifier());
  if (cold_presentation_ && !page.waiting_for_content) return;
  RECT area{};
  if (!GetClientRect(browser->GetHost()->GetWindowHandle(), &area) || area.right < 1 || area.bottom < 1 ||
      static_cast<unsigned long long>(area.right) * area.bottom > 16 * 1024 * 1024) {
    if (cold_presentation_) RevealPresentationAfterFrame();
    return;
  }
  // In the pinned Chromium, clip changes the widget size until the callback.
  // Copy the surface at its normal size and discard the low-quality JPEG.
  auto params = CefDictionaryValue::Create(); params->SetString("format", "jpeg"); params->SetInt("quality", 0);
  params->SetBool("fromSurface", true); params->SetBool("optimizeForSpeed", true);
  ++page.frame_attempts;
  page.incoming_capture = browser->GetHost()->ExecuteDevToolsMethod(0, "Page.captureScreenshot", params);
  if (!page.incoming_capture) RetryIncomingFrame(browser);
}
void EmbeddedContent::RetryIncomingFrame(CefRefPtr<CefBrowser> browser) {
  if (const auto state = state_.lock()) state->Record("page_transition_frame_failed");
  if (presentations_.at(browser->GetIdentifier()).frame_attempts >= 2) {
    if (cold_presentation_) RevealPresentationAfterFrame();
    return;
  }
  CefPostDelayedTask(TID_UI, base::BindOnce([](std::weak_ptr<State> weak, CefRefPtr<CefBrowser> page, unsigned long long generation) {
    if (const auto state = weak.lock(); state && state->embedded && state->embedded->presentation_generation_ == generation && page->IsValid())
      state->embedded->AwaitIncomingFrame(page);
  }, state_, browser, presentation_generation_), 32);
}
void EmbeddedContent::ConfirmPresentationFrame(int id) {
  auto& page = presentations_.at(id);
  page.waiting_for_content = false;
  page.incoming_capture = 0;
  if (const auto state = state_.lock()) {
    auto details = CefDictionaryValue::Create(); details->SetInt("browser_id", id);
    details->SetDouble("painted_ms", static_cast<double>(GetTickCount64() - page.activated_ms));
    state->Record("page_transition_painted", details);
  }
  RevealPresentationAfterFrame();
}
void EmbeddedContent::RevealPresentationAfterFrame() {
  // Let the native window catch up with the confirmed compositor frame.
  CefPostDelayedTask(TID_UI, base::BindOnce([](std::weak_ptr<State> weak, unsigned long long generation) {
    if (auto s = weak.lock(); s && s->embedded) s->embedded->FinishPresentation(generation);
  }, state_, presentation_generation_), 16);
}
void EmbeddedContent::OnPresentationResult(CefRefPtr<CefBrowser> browser, int message, bool success, const void* data, size_t size) {
  if (OnLinkPeekCapture(browser,message,success,data,size)) return;
  if (OnLinkPeekReadiness(browser,message,success,data,size)) return;
  const int id = browser->GetIdentifier();
  if (const auto found = presentations_.find(id); id == presenting_id_ && found != presentations_.end() && message &&
      message == found->second.frame_query) {
    auto& page = found->second;
    page.frame_query = 0;
    if (!page.waiting_for_content) return;
    const auto value = success && size <= 65536 ? CefParseJSON(static_cast<const char*>(data), size, JSON_PARSER_RFC) : nullptr;
    const auto root = value && value->GetType() == VTYPE_DICTIONARY ? value->GetDictionary() : nullptr;
    const auto tree = root ? root->GetDictionary("frameTree") : nullptr;
    const auto frame = tree ? tree->GetDictionary("frame") : nullptr;
    if (frame && page.main_frame.empty()) {
      const auto url = frame->GetString("url");
      page.main_frame = frame->GetString("id");
      if (!url.empty() && url != "about:blank") page.loader = frame->GetString("loaderId");
    }
    return;
  }
  if (const auto incoming = presentations_.find(id); incoming != presentations_.end() && message && message == incoming->second.incoming_capture) {
    incoming->second.incoming_capture = 0;
    if (id == presenting_id_) {
      // The protocol also returns success with an empty data string if JPEG encoding fails.
      if (!success || size <= 16) { RetryIncomingFrame(browser); return; }
      ConfirmPresentationFrame(id);
    }
  }
}
void EmbeddedContent::CheckPresentationTimeout(unsigned long long generation) {
  if (generation != presentation_generation_) return;
  if (auto s = state_.lock()) s->Record("page_transition_timeout");
  FinishPresentation(generation);
}
void EmbeddedContent::FinishPresentation(unsigned long long generation, bool focus, bool commit, bool keep_gradient) {
  if (generation != presentation_generation_) return;
  ++presentation_generation_;
  auto s = state_.lock();
  auto timing = CefDictionaryValue::Create(); timing->SetInt("browser_id", presenting_id_);
  timing->SetBool("revealed", commit && presenting_id_ != -1 && s && s->content);
  if (const auto found = presentations_.find(presenting_id_); found != presentations_.end()) {
    auto& page = found->second;
    timing->SetDouble("total_ms", static_cast<double>(GetTickCount64() - found->second.activated_ms));
    if (page.watching_paint) {
      if (const auto target = browsers_.find(presenting_id_); target != browsers_.end() && target->second->IsValid()) {
        auto params = CefDictionaryValue::Create(); params->SetBool("enabled", false);
        target->second->GetHost()->ExecuteDevToolsMethod(0, "Page.setLifecycleEventsEnabled", params);
      }
    }
    page.incoming_capture = page.frame_attempts = page.frame_query = 0;
    page.waiting_for_content = page.watching_paint = page.saw_loading = page.load_ended = false;
    page.main_frame.clear(); page.loader.clear();
  }
  const bool parked = parked_; parked_ = false;
  if (commit && presenting_id_ != -1 && s && s->content) {
    // A page that painted parked outside the window moves into place now.
    const auto place = PagePhysicalBounds(s->content->GetIdentifier());
    const HWND sidebar = InlineSidebar();
    SetWindowPos(s->content->GetHost()->GetWindowHandle(), sidebar ? sidebar : HWND_TOP, place.x, place.y, place.width, place.height,
        (parked ? 0 : SWP_NOMOVE | SWP_NOSIZE) | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    for (const auto& [id, page] : browsers_)
      if (id != s->content->GetIdentifier()) ShowWindow(page->GetHost()->GetWindowHandle(), SW_HIDE);
  }
  for (const HWND handle : presentation_disabled_) if (IsWindow(handle)) EnableWindow(handle, TRUE);
  presentation_disabled_.clear();
  if (commit) split_leaving_.clear();
  presentation_backdrop_id_ = -1;
  cold_presentation_ = false;
  if (!keep_gradient) cold_cover_ = false;
  const auto input = presentation_input_; presentation_input_ = nullptr;
  presenting_id_ = -1;
  // Never steal focus from a popup, DevTools, permission dialog or another app.
  const auto active = ::GetFocus();
  if (focus && s && !s->ui_editing && s->window && s->content && s->content->GetHost()->GetWindowHandle() == input &&
      GetForegroundWindow() == s->window->GetWindowHandle() &&
      (!active || GetAncestor(active, GA_ROOT) == s->window->GetWindowHandle())) FocusBrowser(s->content);
  if (s) s->Record("page_transition_finished", timing);
}
}  // namespace red
