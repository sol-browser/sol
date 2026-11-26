#include "experiments/web_audio_probe.h"
#include <functional>
#include "include/base/cef_callback.h"
#include "include/cef_parser.h"
#include "include/wrapper/cef_closure_task.h"
#include "web_url.h"
#include "workspace/controller.h"
namespace red {
namespace {
constexpr char kResult[] = "SOL_WEB_AUDIO:";
constexpr char kLive[] = "SOL_WEB_AUDIO_LIVE:";
void Fail(const std::shared_ptr<State>& state, const char* step) {
  auto data = CefDictionaryValue::Create(); data->SetString("step", step);
  state->Record("web_audio_probe_failed", data); state->exit_code = 2;
  state->web_audio_page = nullptr;  // No browser reference may outlive CefShutdown.
}
void Later(const std::shared_ptr<State>& state, int ms, std::function<void(const std::shared_ptr<State>&)> work) {
  CefPostDelayedTask(TID_UI, base::BindOnce([](std::weak_ptr<State> weak, std::function<void(const std::shared_ptr<State>&)> work) {
    if (auto state = weak.lock()) work(state);
  }, std::weak_ptr<State>(state), std::move(work)), ms);
}
void Run(CefRefPtr<CefBrowser> page, const std::string& script) {
  // userGesture: the same user activation a real click or key press gives.
  auto evaluate = CefDictionaryValue::Create();
  evaluate->SetString("expression", script); evaluate->SetBool("userGesture", true);
  page->GetHost()->ExecuteDevToolsMethod(0, "Runtime.evaluate", evaluate);
}
bool ActiveAudible(const std::shared_ptr<State>& state) {
  if (!state->workspace) return false;
  const auto snapshot = state->workspace->Snapshot();
  const auto nodes = snapshot->GetList("nodes");
  for (size_t i = 0; nodes && i < nodes->GetSize(); ++i)
    if (nodes->GetDictionary(i)->GetString("id") == snapshot->GetString("activeId")) return nodes->GetDictionary(i)->GetBool("audible");
  return false;
}
// Ticks every ~90 ms keep Chromium's audible state on (it holds ~2 s after
// silence); a tab that fell silent after the first tick is off by 3.5 s.
void SampleAudible(const std::shared_ptr<State>& state, const char* phase) {
#if CEF_API_ADDED(CEF_EXPERIMENTAL)
  for (const int ms : {1500, 2500, 3500}) Later(state, ms, [ms, phase](const std::shared_ptr<State>& s) {
    auto data = CefDictionaryValue::Create(); data->SetInt("ms", ms); data->SetString("phase", phase);
    data->SetBool("audible", ActiveAudible(s)); s->Record("web_audio_audible_sample", data);
    if (!ActiveAudible(s) && std::string(phase) != "drag") s->web_audio_silent = true;
  });
#endif
}
// Live mode (--probe-web-audio=<css selector of a spinner>): counts the page's
// own AudioBufferSourceNodes (created / started / ended) and AudioContext states
// without changing what they play, then reports and resets on each `report()`.
std::string LiveCounters() {
  return "(()=>{if(window.__solWebAudio)return;const r=window.__solWebAudio={created:0,started:0,ended:0,states:[],contexts:0};"
         "const P=AudioContext.prototype,C=P.createBufferSource;P.createBufferSource=function(){const s=C.call(this);r.created++;"
         "const st=s.start;s.start=function(...a){r.started++;return st.apply(this,a)};s.addEventListener('ended',()=>r.ended++);return s};"
         "const AC=window.AudioContext;window.AudioContext=class extends AC{constructor(...a){super(...a);r.contexts++;r.ctx=this;"
         "r.states.push(this.state);this.addEventListener('statechange',()=>r.states.push(this.state))}};})()";
}
std::string LiveReport(const char* phase, const std::string& selector) {
  // The spinner is scrolled into view, so its rect is where the drag lands.
  return std::string("(()=>{const r=window.__solWebAudio,e=document.querySelector('") + selector + "');"
         "e&&e.scrollIntoView({block:'center',behavior:'instant'});const b=e&&e.getBoundingClientRect();"
         "console.log('" + kLive + "'+JSON.stringify({phase:'" + phase + "',created:r.created,started:r.started,ended:r.ended,contexts:r.contexts,"
         "states:r.states,state:r.ctx&&r.ctx.state,rect:b?[b.left,b.top,b.width,b.height]:null}));"
         "r.created=r.started=r.ended=0;r.states=[];})()";
}
void SendDrag(const std::shared_ptr<State>& state, CefRefPtr<CefBrowser> page, int x, int y) {
  // A mouse drag through the page's own input path: press at the spinner's
  // centre, move right 12 px every 25 ms, release while still moving.
  CefMouseEvent event; event.x = x; event.y = y;
  page->GetHost()->SendMouseMoveEvent(event, false);
  event.modifiers = EVENTFLAG_LEFT_MOUSE_BUTTON;
  page->GetHost()->SendMouseClickEvent(event, MBT_LEFT, false, 1);
  for (int i = 1; i <= 40; ++i) Later(state, 25 * i, [page, x, y, i](const std::shared_ptr<State>&) {
    CefMouseEvent move; move.x = x + 12 * i; move.y = y; move.modifiers = EVENTFLAG_LEFT_MOUSE_BUTTON;
    page->GetHost()->SendMouseMoveEvent(move, false);
    if (i == 40) { move.modifiers = 0; page->GetHost()->SendMouseClickEvent(move, MBT_LEFT, true, 1); }
  });
}
}  // namespace
void OnWebAudioProbeLoad(const std::shared_ptr<State>& state, CefRefPtr<CefBrowser> page, const std::string& url) {
  if (!state->web_audio_probe || state->web_audio_phase != 0) return;
  const bool live = !state->web_audio_selector.empty();
  // /web-audio-speakers: the same fixture through the real output device (a
  // muted tab renders into a fake sink, so the device's clock is not tested).
  const bool speakers = url.ends_with("/web-audio-speakers");
  if (live ? !HasWebScheme(url) : !url.ends_with("/web-audio") && !speakers) return;
  state->web_audio_phase = 1;
#if CEF_API_ADDED(CEF_EXPERIMENTAL)
  // Tab mute is applied after Chromium measures the level: nothing reaches the
  // speakers, and the tab's audible state still follows the page's output.
  if (!speakers) page->GetHost()->SetAudioMuted(true);
#endif
  if (!live) {
    Later(state, 800, [page](const std::shared_ptr<State>& s) {
      Run(page, "spin(4000)"); s->Record("web_audio_spin_started"); SampleAudible(s, "fixture");
    });
    return;
  }
  // The site's keyboard spin (Enter on the spinner), then report.
  Later(state, 2500, [page](const std::shared_ptr<State>& s) {
    const auto& selector = s->web_audio_selector;
    Run(page, LiveCounters());
    Run(page, "document.querySelector('" + selector + "').dispatchEvent(new KeyboardEvent('keydown',{key:'Enter',bubbles:true,cancelable:true}))");
    s->Record("web_audio_spin_started"); SampleAudible(s, "key");
    Later(s, 4500, [page](const std::shared_ptr<State>& s) { Run(page, LiveReport("key", s->web_audio_selector)); });
  });
  state->web_audio_page = page;
}
void ObserveWebAudioProbe(const std::shared_ptr<State>& state, const std::string& message) {
  if (!state->web_audio_probe) return;
  if (message.starts_with(kLive)) {
    const auto parsed = CefParseJSON(message.substr(std::size(kLive) - 1), JSON_PARSER_RFC);
    const auto result = parsed && parsed->GetType() == VTYPE_DICTIONARY ? parsed->GetDictionary() : nullptr;
    if (!result) { Fail(state, "live_result"); return; }
    state->Record("web_audio_live", result->Copy(false));
    const auto phase = result->GetString("phase").ToString();
    const int started = result->GetInt("started"), ended = result->GetInt("ended");
    if (started < 8) { Fail(state, phase == "key" ? "key_too_few_ticks" : "drag_too_few_ticks"); return; }
    if (ended < started - 1) { Fail(state, phase == "key" ? "key_ticks_not_ended" : "drag_ticks_not_ended"); return; }
    if (phase == "key") {
      const auto rect = result->GetList("rect"); const auto page = state->web_audio_page;
      if (!rect || rect->GetSize() != 4 || !page) { Fail(state, "spinner_rect"); return; }
      const int x = static_cast<int>(rect->GetDouble(0) + rect->GetDouble(2) / 2), y = static_cast<int>(rect->GetDouble(1) + rect->GetDouble(3) / 2);
      SendDrag(state, page, x, y); state->Record("web_audio_drag_started"); SampleAudible(state, "drag");
      Later(state, 5000, [page](const std::shared_ptr<State>& s) { Run(page, LiveReport("drag", s->web_audio_selector)); });
      return;
    }
    if (state->web_audio_silent) { Fail(state, "tab_fell_silent"); return; }
    state->web_audio_page = nullptr;
    state->web_audio_checks = true; state->Record("web_audio_checks_pass");
    return;
  }
  if (!message.starts_with(kResult) || state->web_audio_phase != 1) return;
  state->web_audio_phase = 2;
  const auto parsed = CefParseJSON(message.substr(std::size(kResult) - 1), JSON_PARSER_RFC);
  const auto result = parsed && parsed->GetType() == VTYPE_DICTIONARY ? parsed->GetDictionary() : nullptr;
  if (!result) { Fail(state, "result"); return; }
  state->Record("web_audio_result", result->Copy(false));
  const int scheduled = result->GetInt("scheduled"), ended = result->GetInt("ended"), bursts = result->GetInt("bursts");
  if (result->GetString("finalState") != "running") { Fail(state, "context_not_running"); return; }
  // 4 s at one tick per ~90 ms: well over 25 when every frame can tick.
  if (scheduled < 25) { Fail(state, "too_few_ticks"); return; }
  if (ended != scheduled) { Fail(state, "ticks_not_ended"); return; }
  if (bursts < scheduled - 1) { Fail(state, "ticks_not_rendered"); return; }
  if (state->web_audio_silent) { Fail(state, "tab_fell_silent"); return; }
  state->web_audio_checks = true; state->Record("web_audio_checks_pass");
}
}  // namespace red
