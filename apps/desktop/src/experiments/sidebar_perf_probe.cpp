#include "experiments/sidebar_perf_probe.h"
#include "include/base/cef_callback.h"
#include "include/wrapper/cef_closure_task.h"
namespace red {
namespace {
// Fixed shell script (trusted shell only; only the fixture base URL varies).
constexpr char kScript[] = R"JS((async(base,count)=>{
const q=r=>new Promise((ok,no)=>window.cefQuery({request:JSON.stringify(r),persistent:false,onSuccess:x=>ok(JSON.parse(x)),onFailure:(c,m)=>no(c+': '+m)}));
const sleep=ms=>new Promise(r=>setTimeout(r,ms));
for(let i=0;i<count;i++){const s=await q({method:'browser.getSnapshot'});await q({method:'tree.createTab',parentId:'',title:'Tab '+i,url:base+'?t='+i,expectedRevision:s.treeRevision});}
const measure=async label=>{const gaps=[];let last=performance.now();let done=false;
  const tick=t=>{gaps.push(t-last);last=t;if(!done)requestAnimationFrame(tick);};requestAnimationFrame(tick);
  await q({method:'workspace.setSidebarVisible',visible:true});await sleep(400);done=true;await sleep(50);
  const sorted=[...gaps].sort((a,b)=>b-a);
  return {label,frames:gaps.length,max:Math.round(sorted[0]),over20:gaps.filter(g=>g>20).length,over34:gaps.filter(g=>g>34).length,top:sorted.slice(0,5).map(Math.round)};};
const longtasks=[];new PerformanceObserver(l=>{for(const e of l.getEntries())longtasks.push(Math.round(e.duration));}).observe({type:'longtask',buffered:false});
const frames=[];new PerformanceObserver(l=>{for(const e of l.getEntries())frames.push({d:Math.round(e.duration),block:Math.round(e.blockingDuration),
  render:Math.round(e.renderStart?e.startTime+e.duration-e.renderStart:0),style:Math.round(e.styleAndLayoutStart?e.startTime+e.duration-e.styleAndLayoutStart:0),
  scripts:(e.scripts||[]).map(x=>({d:Math.round(x.duration),inv:(x.invoker||'').slice(0,60),fn:(x.sourceFunctionName||'').slice(0,40)})).filter(x=>x.d>=3)});}).observe({type:'long-animation-frame',buffered:false});
const variants={baseline:''};
const results={};const style=document.createElement('style');document.head.append(style);
for(const [name,css] of Object.entries(variants)){style.textContent=css;await q({method:'workspace.setSidebarVisible',visible:false});await sleep(700);
  const r=await measure(name);results[name]=r.top.slice(0,2);}
const rows=document.querySelectorAll('.tree-row').length;
console.info('RED_PERF:'+JSON.stringify({rows,results,longtasks,frames:frames.slice(-12)}));
})(%BASE%,%COUNT%))JS";
}  // namespace
void OnSidebarPerfProbeLoad(const std::shared_ptr<State>& state, const std::string& url) {
  if (!state->sidebar_perf_probe || state->sidebar_perf_started || !url.starts_with("http")) return;
  state->sidebar_perf_started = true;
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
  if (!state->sidebar_perf_probe || !message.starts_with("RED_PERF:")) return;
  auto data = CefDictionaryValue::Create(); data->SetString("result", message.substr(9, 8000));
  state->Record("sidebar_perf", data); state->sidebar_perf_done = true;
}
}  // namespace red
