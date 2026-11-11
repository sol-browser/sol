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
// Where an archived (or restored) tab is: the shared archive, the snapshot, its Space.
void ArchiveStep(const std::shared_ptr<State>& state, const char* name, const std::string& id) {
  const auto snapshot = state->workspace->Snapshot();
  auto data = CefDictionaryValue::Create(); data->SetString("step", name);
  data->SetString("active_space", snapshot->GetString("activeSpace"));
  data->SetBool("active", snapshot->GetString("activeId") == id);
  const auto archive = state->workspace->ArchiveSnapshot();
  data->SetInt("archive_rows", static_cast<int>(archive->GetSize()));
  for (size_t i = 0; i < archive->GetSize(); ++i) if (const auto row = archive->GetDictionary(i); row->GetString("id") == id) {
    data->SetBool("archived", true); data->SetString("archived_space", row->GetString("space"));
  }
  const auto nodes = snapshot->GetList("nodes");
  for (size_t i = 0; i < nodes->GetSize(); ++i) if (const auto node = nodes->GetDictionary(i); node->GetString("id") == id) {
    data->SetBool("in_snapshot", true); data->SetString("space", node->GetString("space")); data->SetBool("live", node->GetBool("live"));
  }
  state->Record("spaces_probe_archive", data);
}
void SharedArchive(const std::shared_ptr<State>& state) {
  const auto closed = state->workspace->Snapshot()->GetString("activeId").ToString();
  Command(state, "tree.close", [closed](auto c) { c->SetString("id", closed); },
    [closed](const std::shared_ptr<State>& s, int, CefRefPtr<CefDictionaryValue>) {
      Later(s, 900, [closed](const std::shared_ptr<State>& s) {
        ArchiveStep(s, "closed_at_home", closed);
        Command(s, "space.create", [](auto c) { c->SetString("title", "Else"); c->SetString("icon", ""); },
          [closed](const std::shared_ptr<State>& s, int, CefRefPtr<CefDictionaryValue>) {
            Later(s, 900, [closed](const std::shared_ptr<State>& s) {
              ArchiveStep(s, "listed_elsewhere", closed);
              if (s->panel_popup) s->panel_popup->Toggle("archive", CefRect(14, 400, 28, 28));
              Later(s, 1500, [closed](const std::shared_ptr<State>& s) {
                // The real Archive panel row. A background run can lose the panel
                // to the foreground policy: then restore by command and say so.
                const auto panel = s->panel_popup && s->panel_popup->is_open() ? s->panel_popup->browser() : nullptr;
                auto via = CefDictionaryValue::Create(); via->SetBool("panel", panel != nullptr); s->Record("spaces_probe_archive_restore", via);
                if (panel) panel->GetMainFrame()->ExecuteJavaScript("document.querySelector('.archive-row')?.click()", panel->GetMainFrame()->GetURL(), 0);
                else Command(s, "tree.restore", [closed](auto c) { c->SetString("id", closed); },
                             [](const std::shared_ptr<State>&, int, CefRefPtr<CefDictionaryValue>) {});
                Later(s, 1300, [closed](const std::shared_ptr<State>& s) {
                  ArchiveStep(s, "restored_elsewhere", closed);
                  if (s->panel_popup) s->panel_popup->Hide(false);
                  const auto other = s->workspace->Snapshot()->GetString("activeSpace").ToString();
                  Command(s, "space.delete", [other](auto c) { c->SetString("id", other); },
                    [](const std::shared_ptr<State>& s, int, CefRefPtr<CefDictionaryValue>) {
                      Later(s, 700, [](const std::shared_ptr<State>& s) { s->spaces_done = true; });
                    });
                });
              });
            });
          });
      });
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
          // The panel's first document can take a moment to present.
          Later(s, 2500, [](const std::shared_ptr<State>& s) {
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
                  // + opens the new-space panel: name the space, then Create space.
                  Later(s, 2900, [](const std::shared_ptr<State>& s) {
                    if (auto panel = s->panel_popup ? s->panel_popup->browser() : nullptr)
                      panel->GetMainFrame()->ExecuteJavaScript(
                          "(() => { const input = document.querySelector('input[placeholder=\"Name your space\"]'); if (!input) return;"
                          " Object.getOwnPropertyDescriptor(HTMLInputElement.prototype, 'value').set.call(input, 'Brief');"
                          " input.dispatchEvent(new Event('input', { bubbles: true }));"
                          " setTimeout(() => document.querySelector('.orbit-done:not(:disabled)')?.click(), 300); })()",
                          panel->GetMainFrame()->GetURL(), 0);
                  });
                  Later(s, 4400, [](const std::shared_ptr<State>& s) {
                    Step(s, "brief_empty");
                    // Deleted while the floating sidebar is still on screen: no outgoing page,
                    // so the page must show on its own (it stayed hidden behind a failed restack).
                    const auto brief = s->workspace->Snapshot()->GetString("activeSpace").ToString();
                    Command(s, "space.delete", [brief](auto c) { c->SetString("id", brief); },
                      [](const std::shared_ptr<State>& s, int, CefRefPtr<CefDictionaryValue>) {
                        if (s->panel_popup) s->panel_popup->Hide(false);
                      });
                  });
                  Later(s, 6200, [](const std::shared_ptr<State>& s) { Step(s, "brief_deleted_open"); s->sidebar_peek_probe = false; });
                  Later(s, 7600, [](const std::shared_ptr<State>& s) {
                    Step(s, "brief_deleted");
                    s->workspace->SetSidebarVisible(true);
                    // One archive for every Space: close a tab here, restore it from another Space.
                    s->spaces_phase = 4; s->workspace->OpenLink(s->spaces_base + "/closed", true);
                  });
                });
              });
            });
          });
        });
      });
    });
  } else if (state->spaces_phase == 4 && url.ends_with("/closed")) {
    state->spaces_phase = 5;
    Later(state, 700, [](const std::shared_ptr<State>& s) { SharedArchive(s); });
  }
}
}  // namespace red
