#include "experiments/history_probe.h"
#include "history/history_service.h"
#include "include/base/cef_callback.h"
#include "include/wrapper/cef_closure_task.h"
#include "navigation/shortcuts.h"
#include "trust_policy.h"
#include "windows/panel_popup.h"
namespace red {
namespace {
void Fail(const std::shared_ptr<State>& state, const char* step) {
  auto data = CefDictionaryValue::Create(); data->SetString("step", step);
  state->Record("history_probe_failed", data); state->exit_code = 2;
}
void Later(const std::shared_ptr<State>& state, int ms, std::function<void(const std::shared_ptr<State>&)> work) {
  CefPostDelayedTask(TID_UI, base::BindOnce([](std::weak_ptr<State> weak, std::function<void(const std::shared_ptr<State>&)> work) {
    if (auto state = weak.lock()) work(state);
  }, std::weak_ptr<State>(state), std::move(work)), ms);
}
// Checks stored visits (titles) after the worker has written them.
void ExpectTitles(const std::shared_ptr<State>& state, std::vector<std::string> present, std::vector<std::string> absent,
                  const char* step, std::function<void(const std::shared_ptr<State>&)> next) {
  state->history->Visits(0, 50, "", [weak = std::weak_ptr<State>(state), present, absent, step, next](std::vector<Visit> visits) {
    auto state = weak.lock(); if (!state) return;
    auto has = [&](const std::string& title) { for (const auto& v : visits) if (v.title == title) return true; return false; };
    for (const auto& title : present) if (!has(title)) { Fail(state, step); return; }
    for (const auto& title : absent) if (has(title)) { Fail(state, step); return; }
    auto data = CefDictionaryValue::Create(); data->SetString("step", step); data->SetInt("visits", static_cast<int>(visits.size()));
    state->Record("history_probe_step", data);
    next(state);
  });
}
void OpenHistoryAndClick(const std::shared_ptr<State>& state, const std::string& label) {
  // The real Ctrl+H path (also the logo menu's History item).
  if (!state->panel_popup->is_open()) ExecuteShortcut(state, Shortcut::History);
  state->panel_popup->ProbeClick(label);
}
}  // namespace
namespace {
// Fixed probe script for the history page: an untrusted command must be
// refused, then select both visits and delete them, then try to navigate away.
constexpr char kPageScript[] = R"JS((async () => {
  const say = m => console.info('RED_HISTORY_PAGE:' + m);
  const ask = r => new Promise(ok => window.cefQuery({ request: JSON.stringify(r), persistent: false,
    onSuccess: () => ok('ok'), onFailure: c => ok(String(c)) }));
  say('shell_command:' + await ask({ method: 'tree.createTab', title: 'x', url: 'https://example.com/', expectedRevision: 0 }));
  const find = label => [...document.querySelectorAll('[aria-label]')].find(e => e.getAttribute('aria-label') === label);
  for (let i = 0; i < 100 && !(find('Select Alpha page') && find('Select Beta page')); i++) await new Promise(r => setTimeout(r, 50));
  say('rows:' + document.querySelectorAll('.hp-row').length);
  find('Select Alpha page')?.click(); await new Promise(r => setTimeout(r, 50));
  find('Select Beta page')?.click(); await new Promise(r => setTimeout(r, 50));
  [...document.querySelectorAll('button')].find(b => b.getAttribute('aria-label') === 'Delete selected')?.click();
  for (let i = 0; i < 100 && document.querySelectorAll('.hp-row').length; i++) await new Promise(r => setTimeout(r, 50));
  say('deleted:' + document.querySelectorAll('.hp-row').length);
  location.href = 'https://example.com/';
  setTimeout(() => say('locked:' + location.href), 700);
})())JS";
}  // namespace
void OnHistoryProbeLoad(const std::shared_ptr<State>& state, CefRefPtr<CefBrowser> page, const std::string& url) {
  if (state->history_probe.empty() || !state->history) return;
  if (state->history_probe == "page") {
    if (url.ends_with("/alpha") && state->history_phase == 0) {
      state->history_phase = 1;
      Later(state, 300, [page](const std::shared_ptr<State>&) {
        page->GetMainFrame()->ExecuteJavaScript("location.href='/beta'", page->GetMainFrame()->GetURL(), 0);
      });
    } else if (url.ends_with("/beta") && state->history_phase == 1) {
      state->history_phase = 2;
      Later(state, 600, [](const std::shared_ptr<State>& s) { ExecuteShortcut(s, Shortcut::HistoryPage); });
    } else if (url == kHistoryUrl && state->history_phase == 2) {
      state->history_phase = 3;
      auto data = CefDictionaryValue::Create();
      data->SetBool("alloy", page->GetHost()->GetRuntimeStyle() == CEF_RUNTIME_STYLE_ALLOY);
      data->SetBool("ui_profile", state->ui_context && page->GetHost()->GetRequestContext()->IsSharingWith(state->ui_context));
      data->SetBool("web_profile", page->GetHost()->GetRequestContext()->IsSharingWith(CefRequestContext::GetGlobalContext()));
      data->SetBool("registered", state->internal_pages.contains(page->GetIdentifier()));
      state->Record("history_page_opened", data);
      page->GetMainFrame()->ExecuteJavaScript(kPageScript, page->GetMainFrame()->GetURL(), 0);
    }
    return;
  }
  if (state->history_probe == "record") {
    if (url.ends_with("/alpha") && state->history_phase == 0) {
      state->history_phase = 1;
      // A download that must survive the restart, then the next page.
      Later(state, 300, [page](const std::shared_ptr<State>&) {
        page->GetMainFrame()->ExecuteJavaScript("const a=document.createElement('a');a.href='/notes.txt';a.download='';document.body.append(a);a.click();"
            "setTimeout(()=>location.href='/beta',800);", page->GetMainFrame()->GetURL(), 0);
      });
    } else if (url.ends_with("/beta") && state->history_phase == 1) {
      state->history_phase = 2;
      Later(state, 600, [](const std::shared_ptr<State>& s) {
        ExpectTitles(s, {"Alpha page", "Beta page"}, {}, "recorded", [](const std::shared_ptr<State>& s) {
          OpenHistoryAndClick(s, "Remove Alpha page from history");
          Later(s, 1200, [](const std::shared_ptr<State>& s) {
            ExpectTitles(s, {"Beta page"}, {"Alpha page"}, "removed_through_popover", [](const std::shared_ptr<State>& s) {
              s->history_checks = true; s->Record("history_checks_pass");
            });
          });
        });
      });
    }
  } else if (state->history_probe == "restart" && state->history_phase == 0) {
    state->history_phase = 1;
    Later(state, 800, [](const std::shared_ptr<State>& s) {
      ExpectTitles(s, {"Beta page"}, {"Alpha page"}, "persisted", [](const std::shared_ptr<State>& s) {
        // One opening, as a person would: "Clear…" then the range.
        OpenHistoryAndClick(s, "Clear…");
        s->panel_popup->ProbeClick("Clear history: All time");
        Later(s, 1600, [](const std::shared_ptr<State>& s) {
          ExpectTitles(s, {}, {"Beta page", "Alpha page"}, "cleared_through_popover", [](const std::shared_ptr<State>& s) {
            s->history->Visits(0, 5, "", [weak = std::weak_ptr<State>(s)](std::vector<Visit> visits) {
              auto s = weak.lock(); if (!s) return;
              if (!visits.empty()) { Fail(s, "cleared_empty"); return; }
              s->history_checks = true; s->Record("history_checks_pass");
            });
          });
        });
      });
    });
  }
}
void ObserveHistoryProbe(const std::shared_ptr<State>& state, const std::string& message) {
  if (state->history_probe != "page" || !message.starts_with("RED_HISTORY_PAGE:")) return;
  auto data = CefDictionaryValue::Create(); data->SetString("message", message.substr(0, 200));
  state->Record("history_page_probe", data);
  if (message == "RED_HISTORY_PAGE:deleted:0") {
    ExpectTitles(state, {}, {"Alpha page", "Beta page"}, "deleted_through_page", [](const std::shared_ptr<State>&) {});
  } else if (message == "RED_HISTORY_PAGE:locked:" + std::string(kHistoryUrl)) {
    state->history_checks = true; state->Record("history_checks_pass");
  }
}
}  // namespace red
