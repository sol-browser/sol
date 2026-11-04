#include "experiments/fullscreen_probe.h"
#include "windows/embedded_content.h"
#include "windows/window_corners.h"
#include "include/base/cef_callback.h"
#include "include/cef_parser.h"
#include "include/wrapper/cef_closure_task.h"
namespace red {
namespace {
void Evaluate(CefRefPtr<CefBrowser> page, const std::string& script, bool gesture) {
  auto evaluate = CefDictionaryValue::Create();
  evaluate->SetString("expression", script); evaluate->SetBool("userGesture", gesture);
  page->GetHost()->ExecuteDevToolsMethod(0, "Runtime.evaluate", evaluate);
}
void PressEscape(CefRefPtr<CefBrowser> page) {
  for (const auto* type : {"rawKeyDown", "keyUp"}) {
    auto key = CefDictionaryValue::Create(); key->SetString("type", type);
    key->SetString("key", "Escape"); key->SetString("code", "Escape");
    key->SetInt("windowsVirtualKeyCode", 27); key->SetInt("nativeVirtualKeyCode", 27);
    page->GetHost()->ExecuteDevToolsMethod(0, "Input.dispatchKeyEvent", key);
  }
}
// Reports what the page sees: fullscreen element and viewport vs. screen size.
constexpr char kReport[] =
    "requestAnimationFrame(()=>requestAnimationFrame(()=>console.info('RED_FULLSCREEN:'+JSON.stringify({"
    "phase:'%s',element:!!document.fullscreenElement,inner:[innerWidth,innerHeight],screen:[screen.width,screen.height]}))))";
std::string Report(const char* phase) {
  std::string script(kReport);
  script.replace(script.find("%s"), 2, phase);
  return script;
}
}  // namespace
void StartFullscreenProbe(const std::shared_ptr<State>& state, CefRefPtr<CefBrowser> page) {
  if (!state->fullscreen_probe || state->fullscreen_phase != 0) return;
  state->fullscreen_phase = 1;
  CefPostDelayedTask(TID_UI, base::BindOnce([](std::weak_ptr<State> weak, CefRefPtr<CefBrowser> page) {
    auto state = weak.lock();
    if (!state) return;
    state->Record("fullscreen_probe_request");
    Evaluate(page, "document.documentElement.requestFullscreen().catch(e=>console.info('RED_FULLSCREEN_ERROR:'+e))", true);
  }, std::weak_ptr<State>(state), page), 1500);
}
void OnFullscreenProbeChange(const std::shared_ptr<State>& state, CefRefPtr<CefBrowser> page, bool fullscreen) {
  if (!state->fullscreen_probe) return;
  // Let Chrome's own transition and our window resize settle before measuring.
  CefPostDelayedTask(TID_UI, base::BindOnce([](std::weak_ptr<State> weak, CefRefPtr<CefBrowser> page, bool fullscreen) {
    auto state = weak.lock();
    if (!state || !state->window || !state->embedded) return;
    auto data = CefDictionaryValue::Create();
    const bool window_full = state->window->IsFullscreen();
    data->SetBool("fullscreen", fullscreen); data->SetBool("window_fullscreen", window_full);
    data->SetBool("root_corners_ok", WindowCornersMatch(state->window));
    if (fullscreen) {
      const bool ok = window_full && state->embedded->FullscreenGeometryOk() && state->embedded->ActiveOwnsInputArea();
      data->SetBool("notification_visible", state->embedded->NotificationVisible());
      data->SetBool("geometry_ok", ok); state->Record("fullscreen_entered", data);
      if (!ok) { state->exit_code = 2; return; }
      state->fullscreen_phase = 2;
      Evaluate(page, Report("entered"), false);
    } else {
      const bool ok = !window_full && state->embedded->ActiveMatchesBounds() && state->embedded->ActiveOwnsInputArea() &&
          !state->embedded->FullscreenGeometryOk();
      data->SetBool("notification_hidden", !state->embedded->NotificationVisible());
      data->SetBool("geometry_ok", ok); state->Record("fullscreen_exited", data);
      if (!ok || state->fullscreen_phase != 3) { state->exit_code = 2; return; }
      state->fullscreen_phase = 4;
      Evaluate(page, Report("exited"), false);
    }
  }, std::weak_ptr<State>(state), page, fullscreen), 800);
}
void ObserveFullscreenProbe(const std::shared_ptr<State>& state, CefRefPtr<CefBrowser> page, const std::string& message) {
  if (!state->fullscreen_probe || !message.starts_with("RED_FULLSCREEN")) return;
  auto data = CefDictionaryValue::Create(); data->SetString("message", message.substr(0, 300));
  state->Record("fullscreen_page", data);
  const auto json = CefParseJSON(message.substr(message.find(':') + 1), JSON_PARSER_RFC);
  if (!json || json->GetType() != VTYPE_DICTIONARY) { state->exit_code = 2; return; }
  const auto report = json->GetDictionary();
  const auto inner = report->GetList("inner"), screen = report->GetList("screen");
  const bool covers = inner->GetInt(0) == screen->GetInt(0) && inner->GetInt(1) == screen->GetInt(1);
  if (report->GetString("phase") == "entered" && state->fullscreen_phase == 2) {
    if (!report->GetBool("element") || !covers) { state->exit_code = 2; return; }
    state->fullscreen_phase = 3;
    PressEscape(page);  // Chrome's own exit action, as a user would press it.
  } else if (report->GetString("phase") == "exited" && state->fullscreen_phase == 4) {
    if (report->GetBool("element") || covers) { state->exit_code = 2; return; }
    state->fullscreen_checks = true;
    state->Record("fullscreen_checks_pass");
  }
}
}  // namespace red
