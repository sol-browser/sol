#include "experiments/sidebar_perf_probe.h"
#include <windows.h>
#include "include/base/cef_callback.h"
#include "include/cef_trace.h"
#include "include/wrapper/cef_closure_task.h"
namespace red {
namespace {
// Fixed shell call (trusted shell only; only the fixture base URL varies). The
// scenarios live in the UI bundle (apps/browser-ui/src/perf-probe.ts), which
// installs this entry point in probe mode only.
constexpr char kScript[] = R"JS(window.solPerfProbe?window.solPerfProbe(%BASE%,%COUNT%):console.info('RED_PERF:{"error":"perf probe not installed"}'))JS";

void CloseAfterResult(const std::weak_ptr<State>& state) {
  CefPostDelayedTask(TID_UI, base::BindOnce([](std::weak_ptr<State> weak) {
    if (auto s = weak.lock(); s && s->window) s->window->Close();
  }, state), 500);
}

class TraceFinished final : public CefEndTracingCallback {
 public:
  explicit TraceFinished(std::weak_ptr<State> state) : state_(std::move(state)) {}

  void OnEndTracingComplete(const CefString& trace_file) override {
    if (auto state = state_.lock()) {
      state->Record(trace_file.empty() ? "sidebar_perf_trace_failed" : "sidebar_perf_trace_saved");
      CloseAfterResult(state_);
    }
  }

 private:
  std::weak_ptr<State> state_;
  IMPLEMENT_REFCOUNTING(TraceFinished);
};
}  // namespace
void OnSidebarPerfProbeLoad(const std::shared_ptr<State>& state, const std::string& url) {
  if (!state->sidebar_perf_probe || state->sidebar_perf_started || !url.starts_with("http")) return;
  state->sidebar_perf_started = true;
  wchar_t trace[2]{};
  if (GetEnvironmentVariableW(L"SOL_SIDEBAR_PERF_TRACE", trace, 2) == 1 && trace[0] == L'1' &&
      state->evidence_directory.is_absolute()) {
    state->sidebar_perf_tracing = CefBeginTracing(
        "toplevel,devtools.timeline,blink.user_timing,cef.client,"
        "disabled-by-default-devtools.timeline.invalidationTracking", nullptr);
    if (!state->sidebar_perf_tracing) state->Record("sidebar_perf_trace_failed");
  }
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
  if (!state->sidebar_perf_probe || state->sidebar_perf_done || !message.starts_with("RED_PERF:")) return;
  auto data = CefDictionaryValue::Create(); data->SetString("result", message.substr(9, 60000));
  state->Record("sidebar_perf", data); state->sidebar_perf_done = true;
  if (state->sidebar_perf_tracing) {
    const auto path = state->evidence_directory / "chromium-trace.json";
    if (CefEndTracing(path.wstring(), new TraceFinished(state))) return;
    state->Record("sidebar_perf_trace_failed");
  }
  // The scenarios take a variable time: close as soon as they report rather
  // than at the probe's fixed deadline (no probe_result is recorded then).
  CloseAfterResult(state);
}
}  // namespace red
