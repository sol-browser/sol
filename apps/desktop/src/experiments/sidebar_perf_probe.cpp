#include "experiments/sidebar_perf_probe.h"
#include "include/base/cef_callback.h"
#include "include/wrapper/cef_closure_task.h"
namespace red {
namespace {
// Fixed shell call (trusted shell only; only the fixture base URL varies). The
// scenarios live in the UI bundle (apps/browser-ui/src/perf-probe.ts), which
// installs this entry point in probe mode only.
constexpr char kScript[] = R"JS(window.solPerfProbe?window.solPerfProbe(%BASE%,%COUNT%):console.info('RED_PERF:{"error":"perf probe not installed"}'))JS";
}  // namespace
void OnSidebarPerfProbeLoad(const std::shared_ptr<State>& state, const std::string& url) {
  if (!state->sidebar_perf_probe || state->sidebar_perf_started || !url.starts_with("http")) return;
  state->sidebar_perf_started = true;
  CefPostDelayedTask(TID_UI, base::BindOnce([](std::weak_ptr<State> weak, std::string base) {
    auto state = weak.lock(); if (!state) return;
    const auto shell = state->browsers.find(state->shell_id); if (shell == state->browsers.end()) return;
    std::string script(kScript);
    script.replace(script.find("%BASE%"), 6, "'" + base + "'");
    script.replace(script.find("%COUNT%"), 7, std::to_string(state->sidebar_perf_tabs));
    shell->second->GetMainFrame()->ExecuteJavaScript(script, shell->second->GetMainFrame()->GetURL(), 0);
  }, std::weak_ptr<State>(state), url.substr(0, url.find('?'))), 2500);
}
void ObserveSidebarPerfProbe(const std::shared_ptr<State>& state, const std::string& message) {
  if (!state->sidebar_perf_probe || !message.starts_with("RED_PERF:")) return;
  auto data = CefDictionaryValue::Create(); data->SetString("result", message.substr(9, 60000));
  state->Record("sidebar_perf", data); state->sidebar_perf_done = true;
  // The scenarios take a variable time: close as soon as they report rather
  // than at the probe's fixed deadline (no probe_result is recorded then).
  CefPostDelayedTask(TID_UI, base::BindOnce([](std::weak_ptr<State> weak) {
    if (auto s = weak.lock(); s && s->window) s->window->Close();
  }, std::weak_ptr<State>(state)), 500);
}
}  // namespace red
