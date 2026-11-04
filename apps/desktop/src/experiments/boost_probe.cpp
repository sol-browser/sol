#include "experiments/boost_probe.h"
#include "include/base/cef_callback.h"
#include "include/wrapper/cef_closure_task.h"
#include "app/session.h"
#include "site/boost_store.h"
#include "windows/boosts.h"
#include "windows/boost_window.h"
#include <filesystem>
namespace red {
namespace {
// A constructed stylesheet needs no <style> element, so no style-src decision applies to it.
constexpr char kBoostScript[] =
    "(() => { let error = null; try { const sheet = new CSSStyleSheet(); sheet.replaceSync('h1 { color: rgb(255, 0, 0) !important }');"
    " document.adoptedStyleSheets = [...document.adoptedStyleSheets, sheet]; } catch (e) { error = String(e); }"
    " addEventListener('DOMContentLoaded', () => { const h = document.querySelector('h1');"
    " console.info('RED_BOOST:' + JSON.stringify({ path: location.pathname, top: window === top,"
    " color: h ? getComputedStyle(h).color : null, error })); }); })();";
std::string Origin(const std::string& url) {
  const auto scheme = url.find("://");
  const auto slash = scheme == std::string::npos ? std::string::npos : url.find('/', scheme + 3);
  return slash == std::string::npos ? url : url.substr(0, slash);
}
void Navigate(CefRefPtr<CefBrowser> page, const std::string& url, int delay_ms) {
  CefPostDelayedTask(TID_UI, base::BindOnce([](CefRefPtr<CefBrowser> page, std::string url) {
    page->GetMainFrame()->LoadURL(url);
  }, page, url), delay_ms);
}
// Records the outcome of the registration call (success flag, short reply).
class Observer final : public CefDevToolsMessageObserver {
 public:
  explicit Observer(std::weak_ptr<State> state) : state_(std::move(state)) {}
  void OnDevToolsMethodResult(CefRefPtr<CefBrowser>, int, bool success, const void* data, size_t size) override {
    if (auto state = state_.lock()) {
      auto details = CefDictionaryValue::Create(); details->SetBool("success", success);
      details->SetString("reply", std::string(static_cast<const char*>(data), std::min<size_t>(size, 200)));
      state->Record("boost_cdp_result", details);
    }
  }
 private:
  std::weak_ptr<State> state_;
  IMPLEMENT_REFCOUNTING(Observer);
};
void Read(CefRefPtr<CefBrowser> page, const char* step, int delay_ms) {
  const std::string expression = std::string("console.info('RED_BOOST:' + JSON.stringify({ path: '") + step +
      "', top: true, color: getComputedStyle(document.querySelector('h1')).color }))";
  CefPostDelayedTask(TID_UI, base::BindOnce([](CefRefPtr<CefBrowser> page, std::string expression) {
    auto params = CefDictionaryValue::Create(); params->SetString("expression", expression);
    page->GetHost()->ExecuteDevToolsMethod(0, "Runtime.evaluate", params);
  }, page, expression), delay_ms);
}
void SetBoost(const std::shared_ptr<State>& state, bool enabled) {
  std::string error;
  state->session->boosts->Set("localhost", Boost{enabled, "h1 { color: rgb(0, 128, 0) !important }"}, error);
  auto data = CefDictionaryValue::Create(); data->SetString("error", SaveBoosts(*state->session));
  state->Record("boost_saved", data);
  RefreshBoosts(*state->session);
}
void Click(CefRefPtr<CefBrowser> page, int x, int y) {
  for (const char* type : {"mouseMoved", "mousePressed", "mouseReleased"}) {
    auto params = CefDictionaryValue::Create(); params->SetString("type", type); params->SetDouble("x", x); params->SetDouble("y", y);
    if (std::string(type) != "mouseMoved") { params->SetString("button", "left"); params->SetInt("clickCount", 1); }
    page->GetHost()->ExecuteDevToolsMethod(0, "Input.dispatchMouseEvent", params);
  }
}
void PickLoad(const std::shared_ptr<State>& state, CefRefPtr<CefBrowser> page) {
  if (state->boost_phase) return;
  state->boost_phase = 1;
  ShowBoostEditor(state, page);
  const bool started = StartBoostPick(*state->session, page->GetIdentifier(), "zap", [weak = std::weak_ptr<State>(state), page](CefRefPtr<CefDictionaryValue> pick) {
    const auto state = weak.lock(); if (!state || state->boost_done) return;
    const auto selector = pick->GetString("selector").ToString();
    state->boost_seen["selector"] = selector;
    // What the editor does with a pick (BoostWindow::Picked), then a look at the page.
    std::string error; red::Boost boost; boost.zaps = {selector};
    state->boost_seen["saved"] = std::to_string(state->session->boosts->Set("localhost", boost, error));
    RefreshBoosts(*state->session);
    StopBoostPick(*state->session, page->GetIdentifier());
    CefPostDelayedTask(TID_UI, base::BindOnce([](CefRefPtr<CefBrowser> page) {
      auto params = CefDictionaryValue::Create();
      params->SetString("expression", "console.info('RED_BOOST:' + (document.getElementById('ad').style.removeProperty('display'), JSON.stringify({ path: 'picked', display: getComputedStyle(document.getElementById('ad')).display, h1: getComputedStyle(document.querySelector('h1')).display })))");
      page->GetHost()->ExecuteDevToolsMethod(0, "Runtime.evaluate", params);
    }, page), 600);
  });
  state->boost_seen["started"] = started ? "true" : "false";
  // #ad is fixed at 100,100 (200x50) in the probe page.
  CefPostDelayedTask(TID_UI, base::BindOnce([](CefRefPtr<CefBrowser> page) { Click(page, 150, 120); }, page), 1500);
}
void StoreLoad(const std::shared_ptr<State>& state, CefRefPtr<CefBrowser> page, const std::string& url) {
  if (!state->session || !state->session->boosts) return;
  const auto origin = Origin(url);
  switch (state->boost_phase) {
    case 0: state->boost_phase = 1; Read(page, "before", 300);
      CefPostDelayedTask(TID_UI, base::BindOnce([](std::shared_ptr<State> state, CefRefPtr<CefBrowser> page) {
        SetBoost(state, true); Read(page, "live", 400);
        Navigate(page, state->boost_origin + "/second", 900);
      }, state, page), 800);
      state->boost_origin = origin; break;
    case 1: state->boost_phase = 2; Read(page, "navigated", 400);
      // A different site (a subdomain of the Boost's host): a new renderer process.
      CefPostDelayedTask(TID_UI, base::BindOnce([](std::shared_ptr<State> state, CefRefPtr<CefBrowser> page) {
        auto origin = state->boost_origin; origin.replace(origin.find("localhost"), 9, "foo.localhost");
        Navigate(page, origin + "/cross", 0);
      }, state, page), 900); break;
    case 2: state->boost_phase = 3; Read(page, "cross", 400);
      CefPostDelayedTask(TID_UI, base::BindOnce([](std::shared_ptr<State> state, CefRefPtr<CefBrowser> page) {
        SetBoost(state, false); Read(page, "disabled", 400);
      }, state, page), 900); break;
    default: break;
  }
}
}  // namespace
void OnBoostProbeLoad(const std::shared_ptr<State>& state, CefRefPtr<CefBrowser> page, const std::string& url) {
  if (!state->boost_probe || state->boost_done || !url.starts_with("http://")) return;
  if (state->boost_pick) { PickLoad(state, page); return; }
  if (state->boost_store) { StoreLoad(state, page, url); return; }
  const auto origin = Origin(url);
  switch (state->boost_phase) {
    case 0: {
      state->boost_observer = page->GetHost()->AddDevToolsMessageObserver(new Observer(state));
      page->GetHost()->ExecuteDevToolsMethod(0, "Page.enable", nullptr);
      auto params = CefDictionaryValue::Create(); params->SetString("source", kBoostScript); params->SetString("worldName", "red-boost");
      page->GetHost()->ExecuteDevToolsMethod(0, "Page.addScriptToEvaluateOnNewDocument", params);
      state->boost_phase = 1; Navigate(page, url, 200);  // The same address again, now with the script registered.
      break;
    }
    case 1: state->boost_phase = 2; Navigate(page, origin + "/csp", 400); break;
    case 2: state->boost_phase = 3; Navigate(page, origin + "/second", 400); break;
    case 3: {
      state->boost_phase = 4;
      CefPostDelayedTask(TID_UI, base::BindOnce([](std::shared_ptr<State> state) {
        auto data = CefDictionaryValue::Create();
        for (const auto& [key, value] : state->boost_seen) data->SetString(key, value);
        state->Record("boost_result", data);
        state->boost_done = true;
      }, state), 500);
      break;
    }
    default: break;
  }
}
void ObserveBoostProbe(const std::shared_ptr<State>& state, const std::string& message) {
  if (!state->boost_probe || state->boost_done || !message.starts_with("RED_BOOST:")) return;
  if (state->boost_pick && message.find("\"path\":\"picked\"") != std::string::npos) {
    state->boost_seen["picked"] = message.substr(10, 300);
    auto data = CefDictionaryValue::Create();
    for (const auto& [key, value] : state->boost_seen) data->SetString(key, value);
    data->SetBool("editor", state->boost_id.load() > 0);
    state->Record("boost_result", data); state->boost_done = true; return;
  }
  if (state->boost_store && message.find("\"path\":\"disabled\"") != std::string::npos) {
    state->boost_seen["disabled:top"] = message.substr(10, 300);
    auto data = CefDictionaryValue::Create();
    for (const auto& [key, value] : state->boost_seen) data->SetString(key, value);
    data->SetBool("file", std::filesystem::exists(state->session->boosts_path));
    state->Record("boost_result", data); state->boost_done = true; return;
  }
  const auto body = message.substr(10, 300);
  const auto at = body.find("\"path\":\"");
  if (at == std::string::npos) return;
  const auto path = body.substr(at + 8);
  const auto name = path.substr(0, path.find('"')) + (body.find("\"top\":true") != std::string::npos ? ":top" : ":sub");
  state->boost_seen[name] = body;
}
}  // namespace red
