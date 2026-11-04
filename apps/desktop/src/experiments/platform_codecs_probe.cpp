#include "experiments/platform_codecs_probe.h"
#include "include/cef_devtools_message_observer.h"
#include "include/cef_parser.h"
#include "include/base/cef_callback.h"
#include "include/wrapper/cef_closure_task.h"

namespace red {
namespace {
class Observer final : public CefDevToolsMessageObserver {
 public:
  explicit Observer(std::weak_ptr<State> state) : state_(std::move(state)) {}
  void OnDevToolsEvent(CefRefPtr<CefBrowser> browser, const CefString& method,
                       const void* data, size_t size) override {
    if (!method.ToString().starts_with("Media.") || size > 65536 || rows_ >= 256) return;
    const auto parsed = CefParseJSON(static_cast<const char*>(data), size, JSON_PARSER_RFC);
    const auto state = state_.lock();
    if (!state || !parsed || parsed->GetType() != VTYPE_DICTIONARY) return;
    ++rows_;
    auto details = CefDictionaryValue::Create();
    details->SetInt("browser_id", browser->GetIdentifier());
    details->SetString("method", method);
    details->SetDictionary("params", parsed->GetDictionary());
    state->Record("platform_codecs_media", details);
  }
 private:
  std::weak_ptr<State> state_;
  int rows_ = 0;
  IMPLEMENT_REFCOUNTING(Observer);
};
}
CefRefPtr<CefRegistration> TracePlatformCodecs(const std::shared_ptr<State>& state, CefRefPtr<CefBrowser> page) {
  auto registration = page->GetHost()->AddDevToolsMessageObserver(new Observer(state));
  page->GetHost()->ExecuteDevToolsMethod(0, "Media.enable", nullptr);
  return registration;
}
void StartPlatformCodecs(const std::shared_ptr<State>& state, CefRefPtr<CefBrowser> page) {
  // Activate this owned fixture so asynchronous MSE/audio startup follows the
  // ordinary autoplay policy. These are page-local CDP events, not OS input.
  for (const char* type : {"mousePressed", "mouseReleased"}) {
    auto click = CefDictionaryValue::Create();
    click->SetString("type", type); click->SetDouble("x", 60); click->SetDouble("y", 60);
    click->SetString("button", "left"); click->SetInt("clickCount", 1);
    page->GetHost()->ExecuteDevToolsMethod(0, "Input.dispatchMouseEvent", click);
  }
  auto params = CefDictionaryValue::Create();
  params->SetString("expression", "window.startCodecProbe()");
  params->SetBool("userGesture", true);
  page->GetHost()->ExecuteDevToolsMethod(0, "Runtime.evaluate", params);
  CefPostDelayedTask(TID_UI, base::BindOnce([](std::weak_ptr<State> weak) {
    if (const auto state = weak.lock()) state->link_headers_done = true;
  }, std::weak_ptr<State>(state)), 20000);
}
}
