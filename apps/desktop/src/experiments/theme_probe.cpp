#include "experiments/theme_probe.h"
#include <functional>
#include "include/cef_frame.h"
#include "include/base/cef_callback.h"
#include "include/wrapper/cef_closure_task.h"
#include "windows/panel_popup.h"
#include "windows/popup_presentation.h"
#include "workspace/controller.h"
namespace red {
namespace {
struct Step { const char* name; Theme theme; };
const Step kSteps[] = {
  {"default", {}},
  {"green_light", {145, 120, ThemeMode::Light, 0}},
  {"green_dark", {145, 120, ThemeMode::Dark, 0}},
  {"coral_dark_grain", {30, 140, ThemeMode::Dark, 60}},
  {"graphite_light_grain", {307, 0, ThemeMode::Light, 40}},
};
void Later(const std::shared_ptr<State>& state, int ms, std::function<void(const std::shared_ptr<State>&)> work) {
  CefPostDelayedTask(TID_UI, base::BindOnce([](std::weak_ptr<State> weak, std::function<void(const std::shared_ptr<State>&)> work) {
    if (auto state = weak.lock()) work(state);
  }, std::weak_ptr<State>(state), std::move(work)), ms);
}
// Controller/renderer acceptance without foreground input: safe to run while
// the user works in their daily browser. The interactive editor has a separate
// focus-gated probe; never weaken that guard just to collect preview timings.
void Preview(const std::shared_ptr<State>& state,int index) {
  if(index==90) {
    const auto shell=state->browsers.find(state->shell_id);
    if(shell!=state->browsers.end())shell->second->GetMainFrame()->ExecuteJavaScript(R"JS(
      requestAnimationFrame(()=>requestAnimationFrame(()=>{
        const root=document.documentElement,hue=((parseFloat(root.style.getPropertyValue('--theme-hue'))%360)+360)%360;
        const surface=document.querySelector('.workspace'),style=surface&&getComputedStyle(surface),spatial=!!style?.backgroundImage.includes('cross-fade');
        const materialAlpha=parseFloat(getComputedStyle(root).getPropertyValue('--theme-material-alpha'))||1;
        console.info('RED_THEME_PREVIEW:'+JSON.stringify({passed:Math.abs(hue-356)<.01&&root.dataset.theme==='dark'&&root.style.getPropertyValue('--theme-palette-gradient').includes('linear-gradient'),hue,dark:root.dataset.theme,spatial,materialAlpha,backdrop:root.dataset.backdrop||'none',surfaceOpacity:style?.opacity}));
      }));
    )JS",shell->second->GetMainFrame()->GetURL(),0);
    Later(state,4000,[](const std::shared_ptr<State>& s){Preview(s,91);});return;
  }
  Theme theme{index<90?index*4:356,42,ThemeMode::Dark,31,ThemePalette{1,3,580,68,0,0,76}};
  // A seeded v2 profile exercises the new image tint on the actual CEF surface;
  // legacy performance fixtures keep their original palette and expectations.
  if(state->theme.palette&&state->theme.palette->version==2){theme.palette=state->theme.palette;theme.palette->circles[0].hue=theme.hue;}
  state->workspace->SetTheme(theme,index==91,[weak=std::weak_ptr<State>(state),index](int code,const std::string& error,CefRefPtr<CefDictionaryValue> snapshot){
    auto s=weak.lock();if(!s)return;
    if(code){auto data=CefDictionaryValue::Create();data->SetString("error",error);s->Record("theme_preview_failed",data);s->theme_done=true;return;}
    if(index<90){Later(s,16,[index](const std::shared_ptr<State>& next){Preview(next,index+1);});return;}
    auto data=CefDictionaryValue::Create();data->SetDictionary("theme",snapshot->GetDictionary("theme")->Copy(false));
    data->SetInt("steps",90);s->Record("theme_preview_saved",data);
  },state->workspace->ThemeSnapshot()->GetString("activeSpace").ToString());
}
void Run(const std::shared_ptr<State>& state, size_t index) {
  if (index == std::size(kSteps)) {
    if(state->palette_probe&&!PopupOwnerIsForeground(state->window)) {
      Later(state,250,[index](const std::shared_ptr<State>& s){Run(s,index);});return;
    }
    if(state->palette_probe&&state->window)state->window->Activate();
    state->panel_popup->Toggle("theme", CefRect(14, 3, 24, 24));
    Later(state, 1800, [](const std::shared_ptr<State>& s) {
      s->Record("theme_probe_step", [] { auto d = CefDictionaryValue::Create(); d->SetString("step", "popover"); return d; }());
      Later(s, 1500, [](const std::shared_ptr<State>& s) { s->theme_done = true; });
    });
    return;
  }
  const auto& step = kSteps[index];
  // Saved like a release in the popover; the last step persists for restart checks.
  state->workspace->SetTheme(step.theme, true, [weak = std::weak_ptr<State>(state), index](int code, const std::string&, CefRefPtr<CefDictionaryValue> snapshot) {
    auto state = weak.lock(); if (!state) return;
    auto data = CefDictionaryValue::Create(); data->SetString("step", kSteps[index].name); data->SetInt("code", code);
    data->SetBool("dark", state->theme_dark);
    const auto bg = Themed({231, 222, 237}, state->theme, state->theme_dark, true);
    data->SetInt("bg_r", bg.r); data->SetInt("bg_g", bg.g); data->SetInt("bg_b", bg.b);
    if (snapshot && snapshot->GetType("theme") == VTYPE_DICTIONARY) data->SetDictionary("snapshot_theme", snapshot->GetDictionary("theme")->Copy(false));
    // Give every window time to restyle before the screenshot.
    Later(state, 1200, [data, index](const std::shared_ptr<State>& s) { s->Record("theme_probe_step", data); Later(s, 900, [index](const std::shared_ptr<State>& s) { Run(s, index + 1); }); });
  });
}
}  // namespace
void OnThemeProbeLoad(const std::shared_ptr<State>& state, const std::string& url) {
  if (!state->theme_probe || state->theme_phase != 0 || !url.ends_with("/theme")) return;
  if(state->palette_probe&&state->window)state->window->SetTitle("Sol — Palette test");
  state->theme_phase = 1;
  if(state->theme_preview_probe){
    auto data=CefDictionaryValue::Create();data->SetDictionary("theme",state->workspace->ThemeSnapshot()->GetDictionary("theme")->Copy(false));
    state->Record("theme_preview_startup",data);
    Later(state,500,[](const std::shared_ptr<State>& s){Preview(s,0);});return;
  }
  if(state->palette_probe&&state->workspace) {
    const auto snapshot=state->workspace->Snapshot();auto data=CefDictionaryValue::Create();
    if(snapshot->GetType("theme")==VTYPE_DICTIONARY)data->SetDictionary("theme",snapshot->GetDictionary("theme")->Copy(false));
    state->Record("palette_startup",data);
  }
  Later(state, 1000, [](const std::shared_ptr<State>& s) { Run(s, 0); });
}
}  // namespace red
