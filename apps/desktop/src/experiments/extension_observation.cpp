#include "experiments/extension_observation.h"
#include "windows/embedded_content.h"
#include "workspace/controller.h"

namespace red {
void SignalWorkspaceProbeReady(const std::shared_ptr<State>& state) {
  if (!state->workspace_probe || !state->content) return;
  const auto frame = state->content->GetMainFrame();
  // A fixed diagnostic signal for the owned fixture, never an IPC-supplied script.
  frame->ExecuteJavaScript(
      "document.documentElement.dataset.redWorkspaceProbeReady='true';"
      "document.dispatchEvent(new Event('red-workspace-probe-ready'));", frame->GetURL(), 0);
}
void ObserveExtensionProbe(const std::shared_ptr<State>& state,
                           CefRefPtr<CefBrowser> source, const std::string& message) {
  if (!state->probe || source->GetIdentifier() == state->shell_id) return;
  constexpr std::string_view prefix = "RED_EXTENSION_OBSERVATION:";
  if (!message.starts_with(prefix)) return;
  const auto phase = message.substr(prefix.size());
  if (phase != "before" && phase != "after_update" && phase != "after_restore" &&
      phase != "after_popup") return;
  auto details = CefDictionaryValue::Create();
  details->SetString("phase", phase);
  details->SetInt("source_browser_id", source->GetIdentifier());
  details->SetString("source_url", source->GetMainFrame()->GetURL());
  details->SetInt("content_browser_id", state->content ? state->content->GetIdentifier() : -1);
  details->SetBool("workspace_ready", state->workspace_checks);
  if (state->workspace) details->SetDictionary("snapshot", state->workspace->Snapshot());
  if (state->embedded) details->SetBool("single_visible_active", state->embedded->HasSingleVisibleActive());
  auto views = CefListValue::Create();
  size_t index = 0;
  for (const auto& [id, browser] : state->browsers) {
    if (id == state->shell_id) continue;
    auto row = CefDictionaryValue::Create(); row->SetInt("browser_id", id);
    row->SetString("url", browser->GetMainFrame()->GetURL());
    row->SetBool("visible", IsWindowVisible(browser->GetHost()->GetWindowHandle()) != FALSE);
    views->SetDictionary(index++, row);
  }
  details->SetList("views", views);
  state->Record("extension_observation", details);
}
}  // namespace red
