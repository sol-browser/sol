#include "experiments/spaces_probe.h"
#include <functional>
#include "include/base/cef_callback.h"
#include "include/wrapper/cef_closure_task.h"
#include "windows/panel_popup.h"
#include "windows/sidebar_peek.h"
#include "workspace/controller.h"
namespace red {
namespace {
void Later(const std::shared_ptr<State>& state, int ms, std::function<void(const std::shared_ptr<State>&)> work) {
  CefPostDelayedTask(TID_UI, base::BindOnce([](std::weak_ptr<State> weak, std::function<void(const std::shared_ptr<State>&)> work) {
    if (auto state = weak.lock()) work(state);
  }, std::weak_ptr<State>(state), std::move(work)), ms);
}
// What the shell shows now, as evidence for the smoke script.
void Step(const std::shared_ptr<State>& state, const char* name) {
  const auto snapshot = state->workspace->Snapshot();
  auto data = CefDictionaryValue::Create(); data->SetString("step", name);
  data->SetString("active_space", snapshot->GetString("activeSpace"));
  data->SetString("active_id", snapshot->GetString("activeId"));
  data->SetInt("spaces", static_cast<int>(snapshot->GetList("spaces")->GetSize()));
  data->SetString("url", snapshot->GetString("url"));
  const auto nodes = snapshot->GetList("nodes");
  int live = 0;
  for (size_t i = 0; i < nodes->GetSize(); ++i) if (nodes->GetDictionary(i)->GetBool("live")) ++live;
  data->SetInt("live", live); data->SetInt("nodes", static_cast<int>(nodes->GetSize()));
  if (snapshot->GetType("theme") == VTYPE_DICTIONARY) data->SetInt("hue", snapshot->GetDictionary("theme")->GetInt("hue"));
  // The active page is actually on screen, not only selected.
  data->SetBool("page_visible", state->content && IsWindowVisible(state->content->GetHost()->GetWindowHandle()));
  state->Record("spaces_probe_step", data);
}
void Command(const std::shared_ptr<State>& state, const char* method, std::function<void(CefRefPtr<CefDictionaryValue>)> fill,
             std::function<void(const std::shared_ptr<State>&, int, CefRefPtr<CefDictionaryValue>)> next) {
  auto command = CefDictionaryValue::Create(); command->SetString("method", method);
  command->SetDouble("expectedRevision", state->workspace->Snapshot()->GetDouble("treeRevision"));
  fill(command);
  state->workspace->Handle(command, [weak = std::weak_ptr<State>(state), next](int code, const std::string&, CefRefPtr<CefDictionaryValue> snapshot) {
    if (auto state = weak.lock()) next(state, code, snapshot);
  });
}
}  // namespace
void OnSpacesProbeLoad(const std::shared_ptr<State>& state, const std::string& url) {
  if (!state->spaces_probe || !url.starts_with("http")) return;
  const auto base = url.substr(0, url.rfind('/'));
  if (state->spaces_phase == 0 && url.ends_with("/home")) {
    state->spaces_phase = 1; state->spaces_base = base;
    Later(state, 900, [](const std::shared_ptr<State>& s) {
      Step(s, "home");
      Command(s, "space.create", [](auto c) { c->SetString("title", "Work"); c->SetString("icon", "💼"); },
        [](const std::shared_ptr<State>& s, int code, CefRefPtr<CefDictionaryValue>) {
          auto d = CefDictionaryValue::Create(); d->SetInt("code", code); s->Record("spaces_probe_created", d);
          Later(s, 900, [](const std::shared_ptr<State>& s) {
            Step(s, "empty_work");
            s->spaces_phase = 2; s->workspace->OpenLink(s->spaces_base + "/work", true);
          });
        });
    });
  } else if (state->spaces_phase == 2 && url.ends_with("/work")) {
    state->spaces_phase = 3;
    Later(state, 900, [](const std::shared_ptr<State>& s) {
      Step(s, "work_tab");
      s->workspace->SwitchSpaceAt(0);
      Later(s, 1300, [](const std::shared_ptr<State>& s) {
        Step(s, "back_home");
        s->workspace->SwitchSpaceAt(1);
        Later(s, 1300, [](const std::shared_ptr<State>& s) {
          Step(s, "work_again");
          if (s->panel_popup) s->panel_popup->Toggle("space", CefRect(14, 90, 120, 26));
          Later(s, 1500, [](const std::shared_ptr<State>& s) {
            Step(s, "editor");
            Later(s, 900, [](const std::shared_ptr<State>& s) {
            if (s->panel_popup) s->panel_popup->Hide(false);
            const auto work = s->workspace->Snapshot()->GetString("activeSpace").ToString();
            Command(s, "space.delete", [work](auto c) { c->SetString("id", work); },
              [](const std::shared_ptr<State>& s, int code, CefRefPtr<CefDictionaryValue>) {
                auto d = CefDictionaryValue::Create(); d->SetInt("code", code); s->Record("spaces_probe_deleted", d);
                Later(s, 1500, [](const std::shared_ptr<State>& s) {
                  Step(s, "deleted");
                  // Collapsed sidebar: a new space deleted right away returns to the page.
                  // Driven through the floating sidebar and the real space editor, as a person does.
                  s->workspace->SetSidebarVisible(false);
                  // Held open as under a person's pointer, until after the delete.
                  Later(s, 700, [](const std::shared_ptr<State>& s) { s->sidebar_peek_probe = true; if (s->sidebar_peek) s->sidebar_peek->Show(); });
                  Later(s, 1700, [](const std::shared_ptr<State>& s) {
                    if (auto peek = s->sidebar_peek ? s->sidebar_peek->browser() : nullptr)
                      peek->GetMainFrame()->ExecuteJavaScript("document.querySelector('.space-add')?.click()", peek->GetMainFrame()->GetURL(), 0);
                  });
                  Later(s, 3000, [](const std::shared_ptr<State>& s) {
                    Step(s, "brief_empty");
                    // Deleted while the floating sidebar is still on screen: no outgoing page,
                    // so the page must show on its own (it stayed hidden behind a failed restack).
                    const auto brief = s->workspace->Snapshot()->GetString("activeSpace").ToString();
                    Command(s, "space.delete", [brief](auto c) { c->SetString("id", brief); },
                      [](const std::shared_ptr<State>& s, int, CefRefPtr<CefDictionaryValue>) {
                        if (s->panel_popup) s->panel_popup->Hide(false);
                      });
                  });
                  Later(s, 4800, [](const std::shared_ptr<State>& s) { Step(s, "brief_deleted_open"); s->sidebar_peek_probe = false; });
                  Later(s, 6200, [](const std::shared_ptr<State>& s) {
                    Step(s, "brief_deleted");
                    s->workspace->SetSidebarVisible(true); s->spaces_done = true;
                  });
                });
              });
            });
          });
        });
      });
    });
  }
}
}  // namespace red
