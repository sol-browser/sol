#include "experiments/ua_brand_probe.h"
#include "include/base/cef_callback.h"
#include "include/wrapper/cef_closure_task.h"
namespace red {
namespace {
// Reads the Web Store's install button label (text only, no page data).
constexpr char kReadButton[] =
    "console.info('RED_UA_BRAND:' + (() => { const b = [...document.querySelectorAll('button')].map(x => x.innerText.trim())"
    ".filter(t => /Chrome|Sol|Arc|Brave|Opera|Edge/.test(t)); return JSON.stringify({ buttons: b,"
    " brands: (navigator.userAgentData?.brands || []).map(x => x.brand) }); })())";
void Evaluate(CefRefPtr<CefBrowser> page, const char* expression) {
  auto params = CefDictionaryValue::Create(); params->SetString("expression", expression); params->SetBool("returnByValue", true);
  page->GetHost()->ExecuteDevToolsMethod(0, "Runtime.evaluate", params);
}
}  // namespace
void OnUaBrandProbeLoad(const std::shared_ptr<State>& state, CefRefPtr<CefBrowser> page, const std::string& url) {
  if (state->ua_brand_probe.empty() || !url.starts_with("https://chromewebstore.google.com/")) return;
  if (state->ua_brand_phase == 0) {
    state->ua_brand_phase = 1;
    // As a branded build would report it: Chromium, the product, a GREASE brand.
    auto brand = [](const char* name, const char* version) {
      auto b = CefDictionaryValue::Create(); b->SetString("brand", name); b->SetString("version", version); return b;
    };
    auto brands = CefListValue::Create();
    brands->SetDictionary(0, brand("Chromium", "154")); brands->SetDictionary(1, brand(state->ua_brand_probe.c_str(), "154"));
    brands->SetDictionary(2, brand("Not_A Brand", "24"));
    auto full = CefListValue::Create();
    full->SetDictionary(0, brand("Chromium", "154.0.8037.98")); full->SetDictionary(1, brand(state->ua_brand_probe.c_str(), "154.0.8037.98"));
    full->SetDictionary(2, brand("Not_A Brand", "24.0.0.0"));
    auto metadata = CefDictionaryValue::Create();
    metadata->SetList("brands", brands); metadata->SetList("fullVersionList", full);
    metadata->SetString("platform", "Windows"); metadata->SetString("platformVersion", "19.0.0");
    metadata->SetString("architecture", "x86"); metadata->SetString("model", ""); metadata->SetBool("mobile", false);
    auto params = CefDictionaryValue::Create();
    params->SetString("userAgent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/154.0.0.0 Safari/537.36");
    params->SetDictionary("userAgentMetadata", metadata);
    if (state->ua_brand_probe != "none") page->GetHost()->ExecuteDevToolsMethod(0, "Emulation.setUserAgentOverride", params);
    CefPostDelayedTask(TID_UI, base::BindOnce([](CefRefPtr<CefBrowser> page) { page->ReloadIgnoreCache(); }, page), 300);
    return;
  }
  if (state->ua_brand_phase != 1) return;
  state->ua_brand_phase = 2;
  // The store renders its button after scripts run.
  CefPostDelayedTask(TID_UI, base::BindOnce([](CefRefPtr<CefBrowser> page) { Evaluate(page, kReadButton); }, page), 5000);
}
void ObserveUaBrandProbe(const std::shared_ptr<State>& state, const std::string& message) {
  if (state->ua_brand_probe.empty() || state->ua_brand_phase != 2 || !message.starts_with("RED_UA_BRAND:")) return;
  auto data = CefDictionaryValue::Create(); data->SetString("result", message.substr(13, 400));
  state->Record("ua_brand_result", data);
  state->ua_brand_phase = 3; state->ua_brand_done = true;
}
}  // namespace red
