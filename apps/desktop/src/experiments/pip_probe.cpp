#include "experiments/pip_probe.h"
#include <windows.h>
#include "include/base/cef_callback.h"
#include "include/wrapper/cef_closure_task.h"
#include "workspace/controller.h"
#include "app/preferences.h"
#include "site/site_settings.h"
#include "app/session.h"
#include "navigation/shortcuts.h"
#include "windows/auto_pip.h"
namespace red {
namespace {
constexpr char kRequest[] =
    "(()=>{const v=document.querySelector('video');const label='%s';"
    "if(!v){console.info('RED_PIP:'+JSON.stringify({label,error:'no video'}));return;}"
    "v.requestPictureInPicture().then(()=>console.info('RED_PIP:'+JSON.stringify({label,entered:true,hidden:document.hidden})))"
    ".catch(e=>console.info('RED_PIP:'+JSON.stringify({label,error:String(e&&e.message||e),hidden:document.hidden,"
    "activation:navigator.userActivation.isActive})));})()";
constexpr char kExit[] = "document.pictureInPictureElement&&document.exitPictureInPicture()";

void Evaluate(CefRefPtr<CefBrowser> page, const std::string& script, bool gesture) {
  auto evaluate = CefDictionaryValue::Create();
  evaluate->SetString("expression", script); evaluate->SetBool("userGesture", gesture);
  page->GetHost()->ExecuteDevToolsMethod(0, "Runtime.evaluate", evaluate);
}
void Mouse(CefRefPtr<CefBrowser> page, const char* type, double x, double y) {
  auto params = CefDictionaryValue::Create(); params->SetString("type", type); params->SetDouble("x", x); params->SetDouble("y", y);
  if (std::string(type) != "mouseMoved") { params->SetString("button", "left"); params->SetInt("clickCount", 1); }
  page->GetHost()->ExecuteDevToolsMethod(0, "Input.dispatchMouseEvent", params);
}
std::string Script(const char* label) {
  std::string s(kRequest); s.replace(s.find("%s"), 2, label); return s;
}
// Visible top-level windows of this process except the root (titles).
void RecordWindows(const std::shared_ptr<State>& state, const char* phase) {
  struct Search { DWORD pid; HWND root; CefRefPtr<CefListValue> list; } search{GetCurrentProcessId(),
      state->window ? state->window->GetWindowHandle() : nullptr, CefListValue::Create()};
  EnumWindows([](HWND window, LPARAM param) -> BOOL {
    auto* s = reinterpret_cast<Search*>(param); DWORD pid = 0; GetWindowThreadProcessId(window, &pid);
    if (pid != s->pid || window == s->root || !IsWindowVisible(window)) return TRUE;
    wchar_t title[200] = {}; GetWindowTextW(window, title, 200);
    RECT box; GetWindowRect(window, &box);
    auto row = CefDictionaryValue::Create(); row->SetString("title", CefString(title));
    row->SetInt("w", box.right - box.left); row->SetInt("h", box.bottom - box.top);
    s->list->SetDictionary(s->list->GetSize(), row);
    return TRUE;
  }, reinterpret_cast<LPARAM>(&search));
  auto data = CefDictionaryValue::Create(); data->SetString("phase", phase); data->SetList("windows", search.list);
  state->Record("pip_windows", data);
}
using Step = void (*)(const std::shared_ptr<State>&);
void Later(const std::shared_ptr<State>& state, int delay, Step step) {
  CefPostDelayedTask(TID_UI, base::BindOnce([](std::weak_ptr<State> weak, Step step) {
    if (auto state = weak.lock()) step(state);
  }, std::weak_ptr<State>(state), step), delay);
}
}  // namespace

// Automatic PiP (--probe-pip on /autopip): an audible video starts with a
// gesture (the tab is muted), another tab opens (the video must pop out), then
// the first tab returns (Red's PiP must close).
void AutoPip(const std::shared_ptr<State>& state, CefRefPtr<CefBrowser> page) {
  state->pip_page = page; page->GetHost()->SetAudioMuted(true);
  Later(state, 1500, [](const std::shared_ptr<State>& s) { Evaluate(s->pip_page, "start()", true); });
  // Off by default: leaving the tab must not pop the video out.
  Later(state, 3500, [](const std::shared_ptr<State>& s) {
    auto data = CefDictionaryValue::Create(); data->SetBool("audible", s->audible_frames.contains(s->pip_page->GetIdentifier()));
    s->Record("auto_pip_audible", data);
    const auto url = s->pip_page->GetMainFrame()->GetURL().ToString();
    s->workspace->OpenLink(url.substr(0, url.rfind('/')) + "/other", true);
  });
  Later(state, 5300, [](const std::shared_ptr<State>& s) { RecordWindows(s, "away_default"); s->workspace->CycleTab(true); });
  // The site's own switch turns it on (as the site controls do).
  Later(state, 6500, [](const std::shared_ptr<State>& s) {
    auto next = s->session->preferences->Get(); next.auto_pip_sites[SiteOrigin(s->pip_page)] = true;
    s->session->preferences->Save(next, [](const std::string&) {});
  });
  Later(state, 7000, [](const std::shared_ptr<State>& s) { s->workspace->CycleTab(false); });
  Later(state, 8800, [](const std::shared_ptr<State>& s) { RecordWindows(s, "away"); s->workspace->CycleTab(true); });
  // Built-in Alt+P on the visible page: pops the video out, then back in.
  Later(state, 10800, [](const std::shared_ptr<State>& s) { RecordWindows(s, "back"); ExecuteShortcut(s, Shortcut::PictureInPicture); });
  Later(state, 12300, [](const std::shared_ptr<State>& s) { RecordWindows(s, "shortcut_on"); ExecuteShortcut(s, Shortcut::PictureInPicture); });
  // The button over the video (isolated world): hover the 640x360 video at
  // the page origin, then click where the button appears (right edge, middle).
  Later(state, 13800, [](const std::shared_ptr<State>& s) { RecordWindows(s, "shortcut_off"); Mouse(s->pip_page, "mouseMoved", 320, 180); });
  Later(state, 14400, [](const std::shared_ptr<State>& s) { Mouse(s->pip_page, "mousePressed", 612, 180); Mouse(s->pip_page, "mouseReleased", 612, 180); });
  Later(state, 15900, [](const std::shared_ptr<State>& s) { RecordWindows(s, "button_on"); Evaluate(s->pip_page, kExit, false); });
  Later(state, 16400, [](const std::shared_ptr<State>& s) { s->pip_done = true; s->pip_page = nullptr; });
}
// Sidebar player (--probe-pip on /media): sound with Media Session metadata
// plays, another tab opens; the snapshot must describe it; pause, seek and
// dismiss go through the same media.control command the sidebar sends.
void MediaStep(const std::shared_ptr<State>& s, const char* step) {
  const auto snapshot = s->workspace->Snapshot(); const auto list = snapshot ? snapshot->GetList("media") : nullptr;
  const auto media = list && list->GetSize() ? list->GetDictionary(0) : nullptr;
  auto data = media ? media->Copy(false) : CefDictionaryValue::Create();
  // The artwork itself is a data URL; the evidence only needs to know it arrived.
  data->SetBool("artwork", media && media->GetString("artwork").ToString().starts_with("data:image/png;base64,"));
  data->SetInt("players", list ? static_cast<int>(list->GetSize()) : 0);
  data->SetString("step", step); data->SetBool("present", media != nullptr); s->Record("media_probe_step", data);
}
void MediaControl(const std::shared_ptr<State>& s, const char* action, double value = -1) {
  const auto snapshot = s->workspace->Snapshot(); const auto list = snapshot ? snapshot->GetList("media") : nullptr;
  const auto media = list && list->GetSize() ? list->GetDictionary(0) : nullptr;
  if (!media) return;
  auto request = CefDictionaryValue::Create(); request->SetString("method", "media.control");
  request->SetString("id", media->GetString("id")); request->SetString("action", action);
  if (value >= 0) request->SetDouble("value", value);
  s->workspace->Handle(request, [](int, const std::string&, CefRefPtr<CefDictionaryValue>) {});
}
void Media(const std::shared_ptr<State>& state, CefRefPtr<CefBrowser> page) {
  state->pip_page = page;  // Not muted: the fixture's tone is inaudible (19 kHz).
  Later(state, 1500, [](const std::shared_ptr<State>& s) { Evaluate(s->pip_page, "start()", true); });
  // A second playing tab (/media?b starts itself on load), then another tab:
  // two players, the newest first.
  Later(state, 2500, [](const std::shared_ptr<State>& s) {
    const auto url = s->pip_page->GetMainFrame()->GetURL().ToString();
    s->workspace->OpenLink(url.substr(0, url.rfind('/')) + "/media?b", true);
  });
  Later(state, 5500, [](const std::shared_ptr<State>& s) {
    const auto url = s->pip_page->GetMainFrame()->GetURL().ToString();
    s->workspace->OpenLink(url.substr(0, url.rfind('/')) + "/other", true);
  });
  // Previous/next and artwork come from the page's Media Session (engine patch 09); "next" runs
  // the page's own nexttrack handler, which retitles the track.
  Later(state, 8000, [](const std::shared_ptr<State>& s) { MediaStep(s, "away"); MediaControl(s, "next"); });
  Later(state, 9300, [](const std::shared_ptr<State>& s) { MediaStep(s, "nexted"); MediaControl(s, "toggle"); });
  Later(state, 10000, [](const std::shared_ptr<State>& s) { MediaStep(s, "paused"); MediaControl(s, "seek", 3); });
  Later(state, 12000, [](const std::shared_ptr<State>& s) { MediaStep(s, "seeked"); MediaControl(s, "dismiss"); });
  // One dismiss closes a player that is still playing (it stays audible for a
  // moment, which must not bring it back).
  Later(state, 13500, [](const std::shared_ptr<State>& s) { MediaStep(s, "dismissed"); MediaControl(s, "dismiss"); });
  Later(state, 15500, [](const std::shared_ptr<State>& s) { MediaStep(s, "dismissed_playing"); });
  Later(state, 16000, [](const std::shared_ptr<State>& s) { s->pip_done = true; s->pip_page = nullptr; });
}
void OnPipProbeLoad(const std::shared_ptr<State>& state, CefRefPtr<CefBrowser> page, const std::string& url) {
  // Real-site check of the button over videos (whether its host element exists).
  if (state->pip_probe && state->pip_phase == 0 && url.find("youtube.com/watch") != std::string::npos) {
    state->pip_phase = 1; state->pip_page = page;
    Later(state, 7000, [](const std::shared_ptr<State>& s) {
      Evaluate(s->pip_page, "console.info('RED_PIPCHECK:'+JSON.stringify({host:!!document.querySelector('red-pip-button'),videos:document.querySelectorAll('video').length}))", false);
    });
    Later(state, 8000, [](const std::shared_ptr<State>& s) { s->pip_done = true; s->pip_page = nullptr; });
    return;
  }
  if (state->pip_probe && state->pip_phase == 0 && url.ends_with("/media")) { state->pip_phase = 1; Media(state, page); return; }
  if (state->pip_probe && url.ends_with("/media?b")) {
    CefPostDelayedTask(TID_UI, base::BindOnce([](CefRefPtr<CefBrowser> page) { Evaluate(page, "start('B song')", true); }, page), 800);
    return;
  }
  if (state->pip_probe && state->pip_phase == 0 && url.ends_with("/autopip")) { state->pip_phase = 1; AutoPip(state, page); return; }
  if (!state->pip_probe || state->pip_phase != 0 || !url.ends_with("/pip")) return;
  state->pip_phase = 1; state->pip_page = page;
  // 1. Visible page, host-initiated with a DevTools user gesture.
  Later(state, 2000, [](const std::shared_ptr<State>& s) { Evaluate(s->pip_page, Script("visible"), true); });
  Later(state, 3500, [](const std::shared_ptr<State>& s) { RecordWindows(s, "after_visible"); Evaluate(s->pip_page, kExit, false); });
  // 2. Hide the page by opening another tab, then ask again.
  Later(state, 5000, [](const std::shared_ptr<State>& s) {
    const auto url = s->pip_page->GetMainFrame()->GetURL().ToString();
    s->workspace->OpenLink(url.substr(0, url.rfind('/')) + "/other", true);
  });
  Later(state, 7500, [](const std::shared_ptr<State>& s) { Evaluate(s->pip_page, Script("hidden"), true); });
  Later(state, 9000, [](const std::shared_ptr<State>& s) { RecordWindows(s, "after_hidden"); Evaluate(s->pip_page, kExit, false); });
  Later(state, 10500, [](const std::shared_ptr<State>& s) { s->pip_done = true; s->pip_page = nullptr; });
}
void ObservePipProbe(const std::shared_ptr<State>& state, const std::string& message) {
  if (state->pip_probe && message == "RED_AUTO_PIP:entered") { state->Record("auto_pip_entered"); return; }
  if (state->pip_probe && message.starts_with("RED_PIPCHECK:")) {
    auto data = CefDictionaryValue::Create(); data->SetString("result", message.substr(13, 200)); state->Record("pip_check", data); return;
  }
  if (!state->pip_probe || !message.starts_with("RED_PIP:")) return;
  auto data = CefDictionaryValue::Create(); data->SetString("result", message.substr(8, 600));
  state->Record("pip_result", data);
}
}  // namespace red
