#include "experiments/audible_probe.h"
#include <functional>
#include "include/base/cef_callback.h"
#include "include/wrapper/cef_closure_task.h"
#include "workspace/controller.h"
namespace red {
namespace {
void Fail(const std::shared_ptr<State>& state, const char* step) {
  auto data = CefDictionaryValue::Create(); data->SetString("step", step);
  state->Record("audible_probe_failed", data); state->exit_code = 2;
}
void Later(const std::shared_ptr<State>& state, int ms, std::function<void(const std::shared_ptr<State>&)> work) {
  CefPostDelayedTask(TID_UI, base::BindOnce([](std::weak_ptr<State> weak, std::function<void(const std::shared_ptr<State>&)> work) {
    if (auto state = weak.lock()) work(state);
  }, std::weak_ptr<State>(state), std::move(work)), ms);
}
void Run(CefRefPtr<CefBrowser> page, const std::string& script) {
  auto evaluate = CefDictionaryValue::Create();
  evaluate->SetString("expression", script); evaluate->SetBool("userGesture", true);
  page->GetHost()->ExecuteDevToolsMethod(0, "Runtime.evaluate", evaluate);
}
bool ActiveAudible(const std::shared_ptr<State>& state) {
  const auto snapshot = state->workspace->Snapshot();
  const auto nodes = snapshot->GetList("nodes");
  for (size_t i = 0; i < nodes->GetSize(); ++i)
    if (nodes->GetDictionary(i)->GetString("id") == snapshot->GetString("activeId")) return nodes->GetDictionary(i)->GetBool("audible");
  return false;
}
// Each step: run |script|, then after |wait| expect the tab's audible flag.
struct Step { const char* name; const char* script; bool audible; };
const Step kSteps[] = {
  {"document_audio_playing", "document.querySelector('audio').play()", true},
  {"document_audio_paused", "document.querySelector('audio').pause()", false},
  {"detached_audio_playing", "window.__probeAudio = new Audio('/tone.wav'); __probeAudio.loop = true; __probeAudio.play()", true},
  {"detached_audio_muted", "__probeAudio.muted = true", false},
  {"detached_audio_unmuted", "__probeAudio.muted = false", true},
  {"detached_audio_stopped", "__probeAudio.pause()", false},
};
void RunStep(const std::shared_ptr<State>& state, CefRefPtr<CefBrowser> page, size_t index) {
  if (index == std::size(kSteps)) {
    // The detector must not leave a page-visible global behind.
    page->GetMainFrame()->ExecuteJavaScript("console.info('RED_AUDIBLE_GLOBALS:' + Object.getOwnPropertyNames(window)"
        ".filter(k => k === 'report' || k === 'audible' || /^__red/i.test(k)).join(','))", page->GetMainFrame()->GetURL(), 0);
    return;
  }
  Run(page, kSteps[index].script);
  // Chromium keeps a tab audible ~2 s after it falls silent.
  Later(state, 3500, [page, index](const std::shared_ptr<State>& s) {
    if (ActiveAudible(s) != kSteps[index].audible) { Fail(s, kSteps[index].name); return; }
    auto data = CefDictionaryValue::Create(); data->SetString("step", kSteps[index].name); s->Record("audible_probe_step", data);
    RunStep(s, page, index + 1);
  });
}
}  // namespace
void OnAudibleProbeLoad(const std::shared_ptr<State>& state, CefRefPtr<CefBrowser> page, const std::string& url) {
  if (!state->audible_probe || state->audible_phase != 0 || !url.ends_with("/media")) return;
  state->audible_phase = 1;
#if CEF_API_ADDED(CEF_EXPERIMENTAL)
  // Tab mute is applied after Chromium measures the level, so the tab stays
  // audible (as Chrome's muted-tab indicator) while nothing reaches speakers.
  page->GetHost()->SetAudioMuted(true);
#endif
  Later(state, 800, [page](const std::shared_ptr<State>& s) { RunStep(s, page, 0); });
}
void ObserveAudibleProbe(const std::shared_ptr<State>& state, const std::string& message) {
  if (!state->audible_probe || !message.starts_with("RED_AUDIBLE_GLOBALS:")) return;
  auto data = CefDictionaryValue::Create(); data->SetString("globals", message.substr(20, 200)); state->Record("audible_probe_globals", data);
  // Only the probe's own variable may appear.
  if (message != "RED_AUDIBLE_GLOBALS:") { Fail(state, "globals"); return; }
  state->audible_checks = true; state->Record("audible_checks_pass");
}
}  // namespace red
