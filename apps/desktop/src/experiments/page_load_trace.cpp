#include "experiments/page_load_trace.h"
#include "experiments/platform_codecs_probe.h"
#include "experiments/devtools_probe.h"
#include "include/cef_devtools_message_observer.h"
#include "include/cef_parser.h"
#include "include/base/cef_callback.h"
#include "include/wrapper/cef_closure_task.h"
#include <cmath>
#include <map>

namespace red {
namespace {
// No URLs, resource names, document contents or cookies are collected.
constexpr char kTiming[] = R"JS((() => {
 const n=performance.getEntriesByType('navigation')[0]; if(!n)return {};
 const p=performance.getEntriesByType('paint').find(x=>x.name==='first-contentful-paint');
 return {red_timing_marker:193871,now_ms:performance.now(),dns_ms:n.domainLookupEnd-n.domainLookupStart,
 connect_ms:n.connectEnd-n.connectStart,tls_ms:n.secureConnectionStart?n.connectEnd-n.secureConnectionStart:0,
 ttfb_ms:n.responseStart,response_end_ms:n.responseEnd,dcl_ms:n.domContentLoadedEventStart,
 load_ms:n.loadEventStart,fcp_ms:p?p.startTime:0,transfer_bytes:n.transferSize,
 encoded_bytes:n.encodedBodySize,decoded_bytes:n.decodedBodySize,redirect_count:n.redirectCount,
 worker_ms:n.workerStart,resource_count:performance.getEntriesByType('resource').length};
})())JS";

class Observer final : public CefDevToolsMessageObserver {
 public:
  explicit Observer(std::weak_ptr<State> state) : state_(std::move(state)) {}
  void OnDevToolsEvent(CefRefPtr<CefBrowser> browser, const CefString& method, const void* data, size_t size) override {
    if (size > 65536 || (method != "Page.frameNavigated" && method != "Page.lifecycleEvent")) return;
    auto parsed = CefParseJSON(static_cast<const char*>(data), size, JSON_PARSER_RFC);
    if (!parsed || parsed->GetType() != VTYPE_DICTIONARY) return;
    const auto params = parsed->GetDictionary();
    if (method == "Page.frameNavigated") {
      const auto frame = params->GetDictionary("frame");
      if (frame && !frame->HasKey("parentId")) { frame_ = frame->GetString("id"); loader_ = frame->GetString("loaderId"); }
      return;
    }
    if (frame_.empty() || params->GetString("frameId") != frame_ || params->GetString("loaderId") != loader_) return;
    const auto name = params->GetString("name").ToString();
    const int phase = name == "firstContentfulPaint" ? 1 : name == "DOMContentLoaded" ? 2 : name == "load" ? 3 : 0;
    if (!phase) return;
    auto state = state_.lock(); if (!state) return;
    if (state->tab_trace_rows >= 4096) return;
    auto metrics = CefDictionaryValue::Create(); metrics->SetInt("browser_id", browser->GetIdentifier()); metrics->SetInt("phase", phase);
    state->Record("page_load_lifecycle", metrics);
    SamplePageLoad(state, browser);
  }
  void OnDevToolsMethodResult(CefRefPtr<CefBrowser> browser, int id, bool success, const void* data, size_t size) override {
    if (!success || size > 8192) return;
    // A private result marker avoids ID races when CEF replies synchronously.
    CefPostTask(TID_UI, base::BindOnce([](CefRefPtr<Observer> self, int browser_id, int id, std::string json) {
      auto state = self->state_.lock(); if (!state) return;
      const auto parsed = CefParseJSON(json, JSON_PARSER_RFC);
      if (!parsed || parsed->GetType() != VTYPE_DICTIONARY) return;
      const auto result = parsed->GetDictionary()->GetDictionary("result");
      const auto value = result ? result->GetDictionary("value") : nullptr;
      if (!value || value->GetInt("red_timing_marker") != 193871) return;
      auto metrics = CefDictionaryValue::Create(); metrics->SetInt("browser_id", browser_id);
      for (const auto* key : {"now_ms", "dns_ms", "connect_ms", "tls_ms", "ttfb_ms", "response_end_ms", "dcl_ms", "load_ms", "fcp_ms", "transfer_bytes", "encoded_bytes", "decoded_bytes", "redirect_count", "worker_ms", "resource_count"}) {
        const auto type = value->GetType(key); if (type != VTYPE_INT && type != VTYPE_DOUBLE) continue;
        const double number = type == VTYPE_INT ? static_cast<double>(value->GetInt(key)) : value->GetDouble(key);
        if (std::isfinite(number) && number >= 0) metrics->SetDouble(key, number);
      }
      state->Record("page_load_timing", metrics);
    }, CefRefPtr<Observer>(this), browser->GetIdentifier(), id, std::string(static_cast<const char*>(data), size)));
  }
 private:
  std::weak_ptr<State> state_;
  std::string frame_, loader_;
  IMPLEMENT_REFCOUNTING(Observer);
};
}
void SamplePageLoad(const std::shared_ptr<State>& state, CefRefPtr<CefBrowser> browser) {
  if (!state->tab_trace.is_open() || state->tab_trace_rows >= 4096 || !browser->IsValid()) return;
  auto params = CefDictionaryValue::Create(); params->SetString("expression", kTiming); params->SetBool("returnByValue", true);
  browser->GetHost()->ExecuteDevToolsMethod(0, "Runtime.evaluate", params);
}
CefRefPtr<CefRegistration> TracePageLoad(const std::shared_ptr<State>& state, CefRefPtr<CefBrowser> browser) {
  if (state->devtools_regression) return TraceDevToolsProbe(state, browser);
  if (state->platform_codecs_probe) return TracePlatformCodecs(state, browser);
  if (!state->tab_trace.is_open()) return nullptr;
  auto registration = browser->GetHost()->AddDevToolsMessageObserver(new Observer(state));
  browser->GetHost()->ExecuteDevToolsMethod(0, "Page.enable", nullptr);
  auto params = CefDictionaryValue::Create(); params->SetBool("enabled", true);
  browser->GetHost()->ExecuteDevToolsMethod(0, "Page.setLifecycleEventsEnabled", params);
  auto metrics = CefDictionaryValue::Create(); metrics->SetInt("browser_id", browser->GetIdentifier());
  state->Record("page_load_created", metrics);
  return registration;
}
}
