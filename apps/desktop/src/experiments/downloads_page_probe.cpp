#include "experiments/downloads_page_probe.h"
#include <functional>
#include "history/history_service.h"
#include "include/base/cef_callback.h"
#include "include/wrapper/cef_closure_task.h"
#include "navigation/shortcuts.h"
#include "trust_policy.h"
namespace red {
namespace {
void Later(const std::shared_ptr<State>& state, int ms, std::function<void(const std::shared_ptr<State>&)> work) {
  CefPostDelayedTask(TID_UI, base::BindOnce([](std::weak_ptr<State> weak, std::function<void(const std::shared_ptr<State>&)> work) {
    if (auto state = weak.lock()) work(state);
  }, std::weak_ptr<State>(state), std::move(work)), ms);
}
constexpr char kPageScript[] = R"JS((async () => {
  const say = m => console.info('RED_DOWNLOADS_PAGE:' + m);
  const ask = r => new Promise(ok => window.cefQuery({ request: JSON.stringify(r), persistent: false,
    onSuccess: () => ok('ok'), onFailure: c => ok(String(c)) }));
  say('shell_command:' + await ask({ method: 'tree.createTab', title: 'x', url: 'https://example.com/', expectedRevision: 0 }));
  say('history_command:' + await ask({ method: 'history.clear', range: 'all' }));
  const find = label => [...document.querySelectorAll('[aria-label]')].find(e => e.getAttribute('aria-label') === label);
  for (let i = 0; i < 100 && !find('Remove notes.txt from list'); i++) await new Promise(r => setTimeout(r, 50));
  say('rows:' + document.querySelectorAll('.dp-row').length);
  find('Remove notes.txt from list')?.click();
  for (let i = 0; i < 100 && document.querySelectorAll('.dp-row').length; i++) await new Promise(r => setTimeout(r, 50));
  say('removed:' + document.querySelectorAll('.dp-row').length);
  location.href = 'https://example.com/';
  setTimeout(() => say('locked:' + location.href), 700);
})())JS";
}  // namespace
void OnDownloadsPageProbeLoad(const std::shared_ptr<State>& state, CefRefPtr<CefBrowser> page, const std::string& url) {
  if (!state->downloads_page_probe) return;
  if (url.ends_with("/alpha") && state->downloads_page_phase == 0) {
    state->downloads_page_phase = 1;
    Later(state, 300, [page](const std::shared_ptr<State>&) {
      page->GetMainFrame()->ExecuteJavaScript("const a=document.createElement('a');a.href='/notes.txt';a.download='';document.body.append(a);a.click();",
                                              page->GetMainFrame()->GetURL(), 0);
    });
    Later(state, 2500, [](const std::shared_ptr<State>& s) { ExecuteShortcut(s, Shortcut::Downloads); });
  } else if (url == kDownloadsUrl && state->downloads_page_phase == 1) {
    state->downloads_page_phase = 2;
    auto data = CefDictionaryValue::Create();
    data->SetBool("chrome_style", page->GetHost()->GetRuntimeStyle() == CEF_RUNTIME_STYLE_CHROME);
    data->SetBool("ui_profile", state->ui_context && page->GetHost()->GetRequestContext()->IsSharingWith(state->ui_context));
    data->SetBool("registered", state->internal_pages.contains(page->GetIdentifier()));
    state->Record("downloads_page_opened", data);
    page->GetMainFrame()->ExecuteJavaScript(kPageScript, page->GetMainFrame()->GetURL(), 0);
  }
}
void ObserveDownloadsPageProbe(const std::shared_ptr<State>& state, const std::string& message) {
  if (!state->downloads_page_probe || !message.starts_with("RED_DOWNLOADS_PAGE:")) return;
  auto data = CefDictionaryValue::Create(); data->SetString("message", message.substr(0, 200));
  state->Record("downloads_page_probe", data);
  if (message == "RED_DOWNLOADS_PAGE:removed:0" && state->history) {
    state->history->DownloadsBefore(0, 10, [weak = std::weak_ptr<State>(state)](std::vector<DownloadRecord> records) {
      auto s = weak.lock(); if (!s) return;
      auto d = CefDictionaryValue::Create(); d->SetInt("saved", static_cast<int>(records.size())); s->Record("downloads_page_store", d);
    });
  } else if (message == "RED_DOWNLOADS_PAGE:locked:" + std::string(kDownloadsUrl)) {
    state->downloads_page_checks = true; state->Record("downloads_page_checks_pass");
  }
}
}  // namespace red
