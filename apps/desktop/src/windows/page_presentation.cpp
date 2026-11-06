#include "windows/embedded_content.h"
#include "windows/focus.h"
#include "include/cef_devtools_message_observer.h"
#include "include/cef_parser.h"
#include "include/base/cef_callback.h"
#include "include/wrapper/cef_closure_task.h"
#include <vector>

namespace red {
namespace {
class PresentationObserver final : public CefDevToolsMessageObserver {
 public:
  explicit PresentationObserver(std::weak_ptr<State> state) : state_(std::move(state)) {}
  void OnDevToolsEvent(CefRefPtr<CefBrowser> browser,const CefString& method,const void* data,size_t size) override {
    if (size>65536 || (method!="Page.frameNavigated" && method!="Page.lifecycleEvent")) return;
    CefPostTask(TID_UI,base::BindOnce([](std::weak_ptr<State> weak,CefRefPtr<CefBrowser> browser,std::string method,std::string json) {
      if (const auto state=weak.lock(); state && state->embedded) state->embedded->OnLinkPeekEvent(browser,method,json);
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
  // Presentation needs only an outgoing screenshot. No Page lifecycle domain,
  // incoming screenshot, FCP or load-end readiness gate.
}
void EmbeddedContent::CaptureOutgoing(CefRefPtr<CefBrowser> browser) {
  RECT area{};
  if (!GetClientRect(browser->GetHost()->GetWindowHandle(), &area) || area.right < 1 || area.bottom < 1 ||
      static_cast<unsigned long long>(area.right) * area.bottom > 16 * 1024 * 1024) {
    FinishPresentation(presentation_generation_); return;
  }
  auto& page = presentations_[browser->GetIdentifier()];
  page.outgoing_generation = presentation_generation_;
  page.capture_started_ms = GetTickCount64();
  auto params = CefDictionaryValue::Create(); params->SetString("format", "png");
  params->SetBool("captureBeyondViewport", false); params->SetBool("optimizeForSpeed", true);
  page.outgoing_capture = browser->GetHost()->ExecuteDevToolsMethod(0, "Page.captureScreenshot", params);
  if (!page.outgoing_capture) FinishPresentation(presentation_generation_);
}
void EmbeddedContent::AwaitIncomingFrame(CefRefPtr<CefBrowser> browser) {
  auto params = CefDictionaryValue::Create(); params->SetString("format", "png"); params->SetBool("optimizeForSpeed", true);
  auto clip = CefDictionaryValue::Create(); clip->SetDouble("x", 0); clip->SetDouble("y", 0);
  clip->SetDouble("width", 1); clip->SetDouble("height", 1); clip->SetDouble("scale", 1); params->SetDictionary("clip", clip);
  auto& page = presentations_[browser->GetIdentifier()];
  page.incoming_capture = browser->GetHost()->ExecuteDevToolsMethod(0, "Page.captureScreenshot", params);
  if (!page.incoming_capture) FinishPresentation(presentation_generation_);
}
void EmbeddedContent::SettlePresentation() {
  const auto generation = ++presentation_generation_;
  CefPostDelayedTask(TID_UI, base::BindOnce([](std::weak_ptr<State> weak, unsigned long long generation) {
    if (auto s = weak.lock(); s && s->embedded) s->embedded->FinishPresentation(generation);
  }, state_, generation), PageCorners::kPageTransitionDurationMs + 32);
}
void EmbeddedContent::OnPresentationResult(CefRefPtr<CefBrowser> browser, int message, bool success, const void* data, size_t size) {
  if (OnLinkPeekCapture(browser,message,success,data,size)) return;
  if (OnLinkPeekReadiness(browser,message,success,data,size)) return;
  const int id = browser->GetIdentifier();
  // The incoming page has produced a frame: the outgoing picture goes now.
  if (const auto incoming = presentations_.find(id); incoming != presentations_.end() && message && message == incoming->second.incoming_capture) {
    incoming->second.incoming_capture = 0;
    if (id == presenting_id_) {
      if (auto s = state_.lock()) {
        auto details = CefDictionaryValue::Create(); details->SetInt("browser_id", id);
        details->SetDouble("painted_ms", static_cast<double>(GetTickCount64() - incoming->second.activated_ms));
        s->Record("page_transition_painted", details);
      }
      // The screenshot returns once the frame is drawn, which can be a vsync
      // before it is on screen: hide the outgoing page a frame later.
      CefPostDelayedTask(TID_UI, base::BindOnce([](std::weak_ptr<State> weak, unsigned long long generation) {
        if (auto s = weak.lock(); s && s->embedded) s->embedded->FinishPresentation(generation);
      }, state_, presentation_generation_), 16);
    }
    return;
  }
  const auto found = presentations_.find(id);
  if (found == presentations_.end() || !message || message != found->second.outgoing_capture) return;
  found->second.outgoing_capture = 0;
  if (presentation_backdrop_id_ != id || found->second.outgoing_generation != presentation_generation_) return;
  const auto generation = presentation_generation_;
  if (!success || size > 24 * 1024 * 1024) { FinishPresentation(generation); return; }
  auto parsed = CefParseJSON(static_cast<const char*>(data), size, JSON_PARSER_RFC);
  if (!parsed || parsed->GetType() != VTYPE_DICTIONARY) { FinishPresentation(generation); return; }
  auto png = CefBase64Decode(parsed->GetDictionary()->GetString("data"));
  if (!png || !png->GetSize() || png->GetSize() > 16 * 1024 * 1024) { FinishPresentation(generation); return; }
  std::vector<char> bytes(png->GetSize()); png->GetData(bytes.data(), bytes.size(), 0);
  if (!corners_.HoldOutgoingPage(bytes.data(), bytes.size())) { FinishPresentation(generation); return; }
  auto s = state_.lock();
  if (!s || !s->content || s->content->GetIdentifier() != presenting_id_) { FinishPresentation(generation, false); return; }
  // Replace the old live HWND with its identical opaque picture. The live target
  // is now directly underneath that picture and progressively paints as normal.
  presentation_fading_ = true;
  SetWindowPos(s->content->GetHost()->GetWindowHandle(), HWND_TOP, 0, 0, 0, 0,
      SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
  ShowWindow(browser->GetHost()->GetWindowHandle(), SW_HIDE);
  // (Outgoing-picture path, no longer entered by tab switches.)
  AwaitIncomingFrame(s->content);
  auto details = CefDictionaryValue::Create(); details->SetInt("browser_id", presenting_id_);
  details->SetInt("outgoing_browser_id", id);
  details->SetDouble("feedback_ms", static_cast<double>(GetTickCount64() - presentations_[presenting_id_].activated_ms));
  details->SetDouble("capture_ms", static_cast<double>(GetTickCount64() - found->second.capture_started_ms));
  details->SetBool("target_loading", s->content->IsLoading());
  s->Record("page_transition_outgoing_held", details);
}
void EmbeddedContent::CheckPresentationTimeout(unsigned long long generation) {
  if (generation != presentation_generation_) return;
  // Bound only the outgoing capture. Slow network/renderer readiness never
  // extends this deadline or prevents the target from becoming live.
  if (auto s = state_.lock()) s->Record("page_transition_timeout");
  FinishPresentation(generation);
}
void EmbeddedContent::FinishPresentation(unsigned long long generation, bool focus, bool commit) {
  if (generation != presentation_generation_) return;
  ++presentation_generation_;
  auto s = state_.lock();
  auto timing = CefDictionaryValue::Create(); timing->SetInt("browser_id", presenting_id_);
  if (const auto found = presentations_.find(presenting_id_); found != presentations_.end())
    timing->SetDouble("total_ms", static_cast<double>(GetTickCount64() - found->second.activated_ms));
  // Commit the live target and remove any remaining outgoing picture.
  // The target has been loading throughout; it never waits for a screenshot.
  if (commit) split_leaving_.clear();
  const bool parked = parked_; parked_ = false;
  if (commit && presenting_id_ != -1 && s && s->content) {
    // A page that painted parked outside the window moves into place now.
    const auto place = PagePhysicalBounds(s->content->GetIdentifier());
    SetWindowPos(s->content->GetHost()->GetWindowHandle(), HWND_TOP, place.x, place.y, place.width, place.height,
        (parked ? 0 : SWP_NOMOVE | SWP_NOSIZE) | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    for (const auto& [id, page] : browsers_)
      if (id != s->content->GetIdentifier()) ShowWindow(page->GetHost()->GetWindowHandle(), SW_HIDE);
  }
  const auto retained = browsers_.find(presentation_backdrop_id_);
  if (retained != browsers_.end()) EnableWindow(retained->second->GetHost()->GetWindowHandle(), TRUE);
  presentation_backdrop_id_ = -1;
  presentation_fading_ = false;
  corners_.EndPageTransition();
  const auto input = presentation_input_; presentation_input_ = nullptr;
  presenting_id_ = -1;
  if (IsWindow(input)) EnableWindow(input, TRUE);
  // Never steal focus from a popup, DevTools, permission dialog or another app.
  const auto active = ::GetFocus();
  if (focus && s && !s->ui_editing && s->window && s->content && s->content->GetHost()->GetWindowHandle() == input &&
      GetForegroundWindow() == s->window->GetWindowHandle() &&
      (!active || GetAncestor(active, GA_ROOT) == s->window->GetWindowHandle())) FocusBrowser(s->content);
  if (s) s->Record("page_transition_finished", timing);
}
}  // namespace red
