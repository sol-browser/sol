#include "experiments/menu_probe.h"
#include "include/base/cef_callback.h"
#include "include/wrapper/cef_closure_task.h"
#include "windows/main_menu.h"
#include "windows/menu_popup.h"
#include "include/views/cef_display.h"
namespace red {
namespace {
// Finds the row and reports its centre; native input then moves to it (and clicks), as a person would.
std::string Row(const char* label, const char* action) {
  return std::string("(() => { const row = [...document.querySelectorAll('.menu-row')].find(e => e.textContent.startsWith('") + label + "'));"
         " const r = row ? row.getBoundingClientRect() : { left: -1, top: -1, width: 0, height: 0 };"
         " window.cefQuery({ request: JSON.stringify({ method: 'menu.probe:" + action + ":' + Math.round(r.left + r.width / 2) + ':' + Math.round(r.top + r.height / 2) }),"
         " persistent: false, onSuccess() {}, onFailure() {} }); })()";
}
// Geometry acceptance uses the real frontend's click handler for warm
// levels, independent of CDP's cached hover state in a moved native widget.
std::string ActivateRow(const char* label) {
  return std::string("[...document.querySelectorAll('.menu-row')].find(e=>e.textContent.startsWith('")+label+"'))?.click()";
}
void Later(const std::shared_ptr<State>& state, int delay_ms, void (*step)(const std::shared_ptr<State>&)) {
  CefPostDelayedTask(TID_UI, base::BindOnce([](std::weak_ptr<State> weak, void (*step)(const std::shared_ptr<State>&)) {
    if (const auto state = weak.lock(); state && state->menu_popup) step(state);
  }, std::weak_ptr<State>(state), step), delay_ms);
}
}  // namespace
void OnMenuProbeLoad(const std::shared_ptr<State>& state, const std::string& url) {
  if (!state->menu_probe || state->menu_probe_started || !url.starts_with("http")) return;
  state->menu_probe_started = true;
  if(state->menu_edges) {
    for(int i=0;i<4;++i) CefPostDelayedTask(TID_UI,base::BindOnce([](std::weak_ptr<State> weak,int corner) {
      const auto s=weak.lock();if(!s || !s->window)return;
      if(!s->menu_popup)s->menu_popup=std::make_shared<MenuPopup>(s);
      const auto work=s->window->GetDisplay()->GetWorkArea();
      MenuPopup::Builder menu;
      menu.Add("First","copy",[] {});
      auto& sub=menu.Submenu("Branch","folder");
      sub.Add("Child","copy",[] {});
      sub.Submenu("Nested","folder").Add("Pick","copy",[] {});
      menu.Add("Last","copy",[] {});
      s->menu_popup->Open(menu,CefPoint(work.x+(corner%2 ? work.width-1 : 1),work.y+(corner/2 ? work.height-1 : 1)),"edge:"+std::to_string(corner));
      Later(s,1300,[](const std::shared_ptr<State>& s) {s->menu_popup->ProbeRun(0,ActivateRow("Branch"));});
      Later(s,2200,[](const std::shared_ptr<State>& s) {s->menu_popup->ProbeRun(1,ActivateRow("Nested"));});
      Later(s,3000,[](const std::shared_ptr<State>& s) {s->menu_popup->ProbeRun(2,ActivateRow("Pick"));});
    },std::weak_ptr<State>(state),i),1200+i*4000);
    Later(state,17500,[](const std::shared_ptr<State>& s) {
      auto d=CefDictionaryValue::Create();d->SetBool("closed",!s->menu_popup->is_open());s->Record("menu_probe_result",d);s->menu_done=true;
    });
    return;
  }
  if (state->menu_page) {
    // A right click on the page (CDP input, trusted); then "Reload" in Red's menu must reach Chrome.
    CefPostDelayedTask(TID_UI, base::BindOnce([](std::weak_ptr<State> weak) {
      const auto state = weak.lock(); if (!state || !state->content) return;
      if (!state->menu_popup) state->menu_popup = std::make_shared<MenuPopup>(state);
      state->menu_loads = state->content_loads;
      for (const char* type : {"mousePressed", "mouseReleased"}) {
        auto input = CefDictionaryValue::Create(); input->SetString("type", type); input->SetDouble("x", 200); input->SetDouble("y", 200);
        input->SetString("button", "right"); input->SetInt("clickCount", 1);
        state->content->GetHost()->ExecuteDevToolsMethod(0, "Input.dispatchMouseEvent", input);
      }
      Later(state, 1500, [](const std::shared_ptr<State>& s) { s->menu_popup->ProbeRun(0, Row("Reload", "click")); });
      Later(state, 4000, [](const std::shared_ptr<State>& s) {
        auto data = CefDictionaryValue::Create(); data->SetBool("closed", !s->menu_popup->is_open());
        data->SetInt("loads_before", s->menu_loads); data->SetInt("loads_after", s->content_loads);
        s->Record("menu_probe_result", data); s->menu_done = true;
      });
    }, std::weak_ptr<State>(state)), 1200);
    return;
  }
  CefPostDelayedTask(TID_UI, base::BindOnce([](std::weak_ptr<State> weak) {
    const auto state = weak.lock(); if (!state) return;
    ShowMainMenu(state, kMainMenuAnchor);
    Later(state, 1500, [](const std::shared_ptr<State>& s) { s->menu_popup->ProbeRun(0, Row("Tabs", "move")); });
    Later(state, 3000, [](const std::shared_ptr<State>& s) { s->menu_popup->ProbeRun(1, Row("Next Tab", "click")); });
    Later(state, 4500, [](const std::shared_ptr<State>& s) {
      auto data = CefDictionaryValue::Create(); data->SetBool("closed", !s->menu_popup->is_open());
      s->Record("menu_probe_result", data); s->menu_done = true;
    });
  }, std::weak_ptr<State>(state)), 1200);
}
}  // namespace red
