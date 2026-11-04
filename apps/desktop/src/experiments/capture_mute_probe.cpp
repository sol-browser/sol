#include "experiments/capture_mute_probe.h"
#include <functional>
#include "include/base/cef_callback.h"
#include "include/wrapper/cef_closure_task.h"
#include "windows/panel_popup.h"
#include "workspace/controller.h"
namespace red {
namespace {
constexpr double kAudible = 1e-3;
void Later(const std::shared_ptr<State>& state, int ms, std::function<void(const std::shared_ptr<State>&)> work) {
  CefPostDelayedTask(TID_UI, base::BindOnce([](std::weak_ptr<State> weak, std::function<void(const std::shared_ptr<State>&)> work) {
    if (auto state = weak.lock()) work(state);
  }, std::weak_ptr<State>(state), std::move(work)), ms);
}
void Mute(const std::shared_ptr<State>& state, bool muted) {
  std::string error;
  const auto id = state->workspace->Snapshot()->GetString("activeId").ToString();
  const int code = state->workspace->SetMuted(id, muted, error);
  auto data = CefDictionaryValue::Create(); data->SetBool("muted", muted); data->SetInt("code", code);
  state->Record("capture_mute_set", data);
}
}  // namespace
void OnCaptureMuteProbeLoad(const std::shared_ptr<State>& state, CefRefPtr<CefBrowser> page, const std::string& url) {
  if (!state->capture_mute_probe || state->capture_mute_phase != 0 || !url.ends_with("/media")) return;
  state->capture_mute_phase = 1;
  Later(state, 800, [page](const std::shared_ptr<State>& s) {
    auto evaluate = CefDictionaryValue::Create();
    evaluate->SetString("expression", "document.querySelector('audio').play()"); evaluate->SetBool("userGesture", true);
    page->GetHost()->ExecuteDevToolsMethod(0, "Runtime.evaluate", evaluate);
    if (!s->panel_popup) return;
    s->panel_popup->Toggle("site", CefRect(500, 8, 28, 28));
    s->panel_popup->ProbeClick("Open Capture Probe");
  });
  // Ends anyway if the capture never starts.
  Later(state, 20000, [](const std::shared_ptr<State>& s) { s->capture_mute_done = true; });
}
void ObserveCaptureMuteProbe(const std::shared_ptr<State>& state, const std::string& message) {
  if (!state->capture_mute_probe || !message.starts_with("RED_CAPTURE_LEVEL:")) return;
  const auto value = message.substr(18, 120);
  auto data = CefDictionaryValue::Create(); data->SetString("level", value); data->SetInt("phase", state->capture_mute_phase);
  state->Record("capture_level", data);
  // Phases: 1 unmuted until the capture hears the tone, 2 muted, 3 unmuted.
  if (state->capture_mute_phase != 1 || value.starts_with("error") || std::atof(value.c_str()) < kAudible) return;
  state->capture_mute_phase = 2;
  Mute(state, true);
  Later(state, 2500, [](const std::shared_ptr<State>& s) {
    s->capture_mute_phase = 3; Mute(s, false);
    Later(s, 2500, [](const std::shared_ptr<State>& s) { s->capture_mute_phase = 4; s->capture_mute_done = true; });
  });
}
}  // namespace red
