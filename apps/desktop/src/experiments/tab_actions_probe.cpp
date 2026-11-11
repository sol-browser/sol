#include "experiments/tab_actions_probe.h"
#include <functional>
#include "include/base/cef_callback.h"
#include "include/wrapper/cef_closure_task.h"
#include "navigation/shortcuts.h"
#include "workspace/controller.h"
namespace red {
namespace {
void Fail(const std::shared_ptr<State>& state, const char* step) {
  auto data = CefDictionaryValue::Create(); data->SetString("step", step);
  state->Record("tab_actions_failed", data); state->exit_code = 2;
}
void Step(const std::shared_ptr<State>& state, const char* step) {
  auto data = CefDictionaryValue::Create(); data->SetString("step", step); state->Record("tab_actions_step", data);
}
void Later(const std::shared_ptr<State>& state, int ms, std::function<void(const std::shared_ptr<State>&)> work) {
  CefPostDelayedTask(TID_UI, base::BindOnce([](std::weak_ptr<State> weak, std::function<void(const std::shared_ptr<State>&)> work) {
    if (auto state = weak.lock()) work(state);
  }, std::weak_ptr<State>(state), std::move(work)), ms);
}
CefRefPtr<CefDictionaryValue> Node(const std::shared_ptr<State>& state, const std::string& id) {
  // Keep the snapshot alive: CEF list/dictionary children are references into it.
  const auto snapshot = state->workspace->Snapshot();
  const auto nodes = snapshot->GetList("nodes");
  for (size_t i = 0; i < nodes->GetSize(); ++i) if (nodes->GetDictionary(i)->GetString("id") == id) return nodes->GetDictionary(i)->Copy(false);
  return nullptr;
}
std::string ActiveId(const std::shared_ptr<State>& state) { return state->workspace->Snapshot()->GetString("activeId"); }
}  // namespace
void OnTabActionsProbeLoad(const std::shared_ptr<State>& state, const std::string& url) {
  if (!state->tab_actions_probe || state->tab_actions_phase != 0 || !url.ends_with("/alpha")) return;
  state->tab_actions_phase = 1;
  Later(state, 600, [](const std::shared_ptr<State>& s) {
    const auto original = ActiveId(s);
    std::string error;
    if (s->workspace->Duplicate(original, error)) { Fail(s, "duplicate"); return; }
    Later(s, 1500, [original](const std::shared_ptr<State>& s) {
      const auto copy = ActiveId(s); const auto node = Node(s, copy);
      if (copy == original || !node || !node->GetString("currentUrl").ToString().ends_with("/alpha") || !node->GetBool("live")) {
        auto data = CefDictionaryValue::Create(); data->SetBool("same", copy == original); data->SetBool("node", node != nullptr);
        if (node) { data->SetString("url", node->GetString("currentUrl")); data->SetBool("live", node->GetBool("live")); }
        s->Record("tab_actions_detail", data); Fail(s, "duplicate_opened"); return;
      }
      Step(s, "duplicated");
      std::string error;
      if (s->workspace->SetMuted(copy, true, error) || !Node(s, copy)->GetBool("muted") || !s->content || !s->content->GetHost()->IsAudioMuted()) { Fail(s, "mute"); return; }
      if (s->workspace->SetMuted(copy, false, error) || Node(s, copy)->GetBool("muted")) { Fail(s, "unmute"); return; }
      Step(s, "muted_and_unmuted");
      ExecuteShortcut(s, Shortcut::CloseTab);
      Later(s, 1200, [copy](const std::shared_ptr<State>& s) {
        // Archived tabs are outside the snapshot (one archive for every Space).
        if (Node(s, copy) || !s->workspace->IsArchived(copy)) { Fail(s, "closed_archived"); return; }
        Step(s, "closed");
        ExecuteShortcut(s, Shortcut::ReopenClosed);
        Later(s, 1500, [copy](const std::shared_ptr<State>& s) {
          const auto reopened = Node(s, copy);
          if (!reopened || s->workspace->IsArchived(copy) || ActiveId(s) != copy) { Fail(s, "reopened"); return; }
          Step(s, "reopened");
          s->tab_actions_checks = true; s->Record("tab_actions_checks_pass");
        });
      });
    });
  });
}
}  // namespace red
