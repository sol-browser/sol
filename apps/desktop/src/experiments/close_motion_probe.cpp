#include "experiments/close_motion_probe.h"
#include "include/base/cef_callback.h"
#include "include/cef_parser.h"
#include "include/wrapper/cef_closure_task.h"
#include "workspace/controller.h"
namespace red {
namespace {
// Fixed recorder: one line per frame whose sidebar picture changed, for 1.6 s
// after the real close button of the row titled "b" is clicked.
constexpr char kRecorder[] = R"JS((()=>{
const row=[...document.querySelectorAll('.tree-row')].find(r=>r.querySelector('.node-title')?.textContent==='b');
const button=row?.querySelector('.close-page');
if(!button){console.info('RED_MOTION:'+JSON.stringify({error:'no close button'}));return;}
const t0=performance.now();let last='';
const frame=()=>{
  const rows=[...document.querySelectorAll('.sidebar-scroll .tree-row')].map(r=>{
    const b=r.getBoundingClientRect(),s=getComputedStyle(r);
    return [r.querySelector('.node-title')?.textContent,r.dataset.active==='true'?1:0,Math.round(b.top),
      Math.round(+s.opacity*100),s.transform==='none'?'':s.transform.replace(/matrix\(|\)/g,'').split(',').map(v=>Math.round(+v*10)/10).join(',')];});
  const ghosts=[...document.querySelectorAll('.sidebar-motion-ghost')].map(g=>Math.round(+getComputedStyle(g).opacity*100));
  const picture=JSON.stringify({rows,ghosts});
  if(picture!==last){last=picture;console.info('RED_MOTION:'+JSON.stringify({t:Math.round(performance.now()-t0),rows,ghosts}));}
  if(performance.now()-t0<1600)requestAnimationFrame(frame);else console.info('RED_MOTION:'+JSON.stringify({done:true}));
};
requestAnimationFrame(frame);button.click();
})())JS";
}  // namespace

void OnCloseMotionProbeLoad(const std::shared_ptr<State>& state, const std::string& url) {
  if (!state->close_motion_probe || !url.starts_with("http") || !state->workspace) return;
  const auto base = url.substr(0, url.rfind('/'));
  if (state->close_motion_phase == 0 && url.ends_with("/a")) {
    state->close_motion_phase = 1; state->workspace->OpenLink(base + "/b", true);
  } else if (state->close_motion_phase == 1 && url.ends_with("/b")) {
    state->close_motion_phase = 2; state->workspace->OpenLink(base + "/c", true);
  } else if (state->close_motion_phase == 2 && url.ends_with("/c")) {
    state->close_motion_phase = 3;
    // Back to /b, then close it with the real button after the UI settles.
    CefPostDelayedTask(TID_UI, base::BindOnce([](std::weak_ptr<State> weak) {
      auto state = weak.lock(); if (!state || !state->workspace) return;
      const auto snapshot = state->workspace->Snapshot(); const auto nodes = snapshot->GetList("nodes");
      for (size_t i = 0; i < nodes->GetSize(); ++i) {
        const auto node = nodes->GetDictionary(i);
        if (!node->GetString("url").ToString().ends_with("/b")) continue;
        auto command = CefDictionaryValue::Create(); command->SetString("method", "tree.activate");
        command->SetString("id", node->GetString("id")); command->SetDouble("expectedRevision", snapshot->GetDouble("treeRevision"));
        state->workspace->Handle(command, [](int, const std::string&, CefRefPtr<CefDictionaryValue>) {});
      }
    }, std::weak_ptr<State>(state)), 1000);
    CefPostDelayedTask(TID_UI, base::BindOnce([](std::weak_ptr<State> weak) {
      auto state = weak.lock(); if (!state) return;
      const auto shell = state->browsers.find(state->shell_id);
      if (shell != state->browsers.end()) shell->second->GetMainFrame()->ExecuteJavaScript(kRecorder, shell->second->GetMainFrame()->GetURL(), 0);
    }, std::weak_ptr<State>(state)), 2500);
  }
}
void ObserveCloseMotionProbe(const std::shared_ptr<State>& state, const std::string& message) {
  if (!state->close_motion_probe || !message.starts_with("RED_MOTION:")) return;
  auto data = CefDictionaryValue::Create(); data->SetString("frame", message.substr(11, 2000));
  state->Record("motion_frame", data);
  if (message.find("\"done\"") != std::string::npos || message.find("\"error\"") != std::string::npos) state->close_motion_done = true;
}
}  // namespace red
