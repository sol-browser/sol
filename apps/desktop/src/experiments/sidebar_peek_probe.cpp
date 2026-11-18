#include "experiments/sidebar_peek_probe.h"
#include "experiments/shell_capture.h"
#include "windows/embedded_content.h"
#include "windows/panel_popup.h"
#include "windows/command_actions.h"
#include <windows.h>
#include <dwmapi.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <chrono>
#include <thread>
#include "include/base/cef_callback.h"
#include "include/cef_command_line.h"
#include "include/cef_devtools_message_observer.h"
#include "include/cef_parser.h"
#include "include/views/cef_display.h"
#include "include/wrapper/cef_closure_task.h"
#include "windows/sidebar_menu.h"
#include "windows/sidebar_peek.h"
#include "windows/workspace_layout.h"
#include "workspace/controller.h"
#include "integrations/x_unread.h"
#include "app/browsing_profiles.h"
#include "history/history_service.h"
#include "windows/focus.h"
namespace red {
namespace {
constexpr char kScript[] = R"JS((async()=>{
const q=r=>new Promise((ok,no)=>window.cefQuery({request:JSON.stringify(r),persistent:false,onSuccess:ok,onFailure:c=>no(c)}));
let refused=0;try{await q({method:'navigation.load',url:'https://example.com/'});}catch(c){refused=c;}
await new Promise(r=>requestAnimationFrame(()=>requestAnimationFrame(r)));
const update=document.querySelector('[data-action="restart-to-update"]');
const box=update?.getBoundingClientRect();
const hit=box&&document.elementFromPoint(box.x+box.width/2,box.y+box.height/2);
console.info('RED_PEEK:'+JSON.stringify({updateVisible:!!update&&!!box&&box.width>0&&box.height>0,updateClickable:!!update&&!!hit&&update.contains(hit),updateVersion:update?.title.includes('0.1.999'),mediaVisible:document.querySelector('.media-stack')?.textContent?.includes('Synthetic media')===true}));
// Click in the same render as the synthetic snapshot. A subsequent real
// snapshot can remove the synthetic notice before the next task.
update?.click();
for(let i=0;i<40&&!document.querySelector('.peek-error')?.textContent?.includes('409');++i)await new Promise(r=>setTimeout(r,10));
let invalid=0;try{await q({method:'browser.restartToUpdate',extra:true});}catch(c){invalid=c;}
console.info('RED_PEEK:'+JSON.stringify({restartRouted:document.querySelector('.peek-error')?.textContent?.includes('409')===true,invalidUpdate:invalid}));

const rows=[...document.querySelectorAll('.tree-row .node-title')].map(n=>n.textContent);
const row=[...document.querySelectorAll('.tree-row')].find(r=>r.querySelector('.node-title')?.textContent==='a');
row?.querySelector('.node-name')?.click();
console.info('RED_PEEK:'+JSON.stringify({rows,refused,width:innerWidth,right:document.querySelector('.sidebar')?.getBoundingClientRect().right}));
// The row menu must open from the floating sidebar (the docked one is hidden).
const menu=await q({method:'workspace.contextMenu',id:'',x:20,y:60}).then(()=>'shown',c=>c);
console.info('RED_PEEK:'+JSON.stringify({menu}));
})())JS";
using Step = void (*)(const std::shared_ptr<State>&);
using Hover = std::function<void(CefRefPtr<CefBrowser>, bool)>;
void Later(const std::shared_ptr<State>& state, int delay, Step step) {
  CefPostDelayedTask(TID_UI, base::BindOnce([](std::weak_ptr<State> weak, Step step) {
    if (auto state = weak.lock()) step(state);
  }, std::weak_ptr<State>(state), step), delay);
}
void ChoosePinEditor(const std::shared_ptr<State>& state, int attempts) {
  if (state->sidebar_menu && state->sidebar_menu->ProbeSelect("editPinned")) {
    state->window->Activate();
    SetForegroundWindow(state->window->GetWindowHandle());
  } else if (attempts > 0) {
    CefPostDelayedTask(TID_UI, base::BindOnce([](std::weak_ptr<State> weak, int attempts) {
      if (const auto state = weak.lock(); state && state->window) ChoosePinEditor(state, attempts);
    }, std::weak_ptr<State>(state), attempts - 1), 10);
  }
}
void PinEditorMouse(CefRefPtr<CefBrowser> browser, const char* type, int x, int y, bool down) {
  if (!browser || !browser->IsValid()) return;
  const auto params = CefDictionaryValue::Create();
  params->SetString("type", type);
  params->SetInt("x", x); params->SetInt("y", y);
  params->SetString("button", "left"); params->SetInt("buttons", down ? 1 : 0);
  params->SetInt("clickCount", 1);
  browser->GetHost()->ExecuteDevToolsMethod(0, "Input.dispatchMouseEvent", params);
}
void ActivateProbeWindow(const std::shared_ptr<State>& s) {
  s->window->Activate();
  const auto root = s->window->GetWindowHandle();
  if (!SetForegroundWindow(root) && !PopupOwnerIsForeground(s->window)) {
    INPUT keys[2]{};
    keys[0].type = keys[1].type = INPUT_KEYBOARD;
    keys[0].ki.wVk = keys[1].ki.wVk = VK_MENU;
    keys[1].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(2, keys, sizeof(INPUT));
    SetForegroundWindow(root);
  }
}
void CheckPinEditor(const std::shared_ptr<State>& s, bool floating) {
  const auto host = s->browsers.find(floating ? s->peek_id : s->shell_id);
  if (host == s->browsers.end()) return;
  ActivateProbeWindow(s);
  const auto root = s->window->GetWindowHandle();
  RECT client{};
  GetClientRect(root, &client);
  POINT page_sample{client.right - 48, client.bottom - 48};
  ClientToScreen(root, &page_sample);
  HDC desktop = GetDC(nullptr);
  const COLORREF undimmed = GetPixel(desktop, page_sample.x, page_sample.y);
  ReleaseDC(nullptr, desktop);
  const auto frame = host->second->GetMainFrame();
  frame->ExecuteJavaScript(R"JS((()=>{
    const row = [...document.querySelectorAll('.tree-row')].find(r=>r.querySelector('.node-title')?.textContent==='Pinned editor fixture');
    row?.dispatchEvent(new MouseEvent('contextmenu',{bubbles:true,cancelable:true}));
  })())JS", frame->GetURL(), 0);
  ChoosePinEditor(s, 20);
  CefPostDelayedTask(TID_UI, base::BindOnce([](std::weak_ptr<State> weak, bool floating, POINT page_sample, COLORREF undimmed) {
    const auto state = weak.lock();
    if (!state || !state->panel_popup) return;
    const auto panel = state->panel_popup;
    const auto root = state->window->GetWindowHandle();
    const auto owner = state->window->GetClientAreaBoundsInScreen();
    const auto window = panel->window();
    const auto bounds = window ? window->GetBounds() : CefRect();
    const auto data = CefDictionaryValue::Create();
    data->SetBool("floating", floating);
    data->SetBool("presented", panel->is_presented() && window && window->IsVisible());
    data->SetBool("centered", std::abs(2 * bounds.x + bounds.width - 2 * owner.x - owner.width) <= 2 &&
        std::abs(2 * bounds.y + bounds.height - 2 * owner.y - owner.height) <= 2);
    data->SetBool("owner_disabled", !IsWindowEnabled(root));
    const HWND dialog = window ? window->GetWindowHandle() : nullptr;
    data->SetBool("owned", dialog && GetWindow(dialog, GW_OWNER) == root);
    RECT editor{};
    GetWindowRect(dialog, &editor);
    const POINT editor_center{(editor.left + editor.right) / 2, (editor.top + editor.bottom) / 2};
    data->SetBool("editor_hit_test", dialog && GetAncestor(WindowFromPoint(editor_center), GA_ROOT) == dialog);
    HDC desktop = GetDC(nullptr);
    const COLORREF dimmed = GetPixel(desktop, page_sample.x, page_sample.y);
    ReleaseDC(nullptr, desktop);
    const auto intensity = [](COLORREF c) { return GetRValue(c) + GetGValue(c) + GetBValue(c); };
    data->SetInt("page_before", intensity(undimmed));
    data->SetInt("page_dimmed", intensity(dimmed));
    data->SetBool("backdrop", state->embedded && state->embedded->modal_shade_visible() &&
        undimmed != CLR_INVALID && dimmed != CLR_INVALID && intensity(undimmed) > 100 &&
        std::abs(intensity(dimmed) - .6 * intensity(undimmed)) <= 15);
    data->SetBool("foreground", PopupOwnerIsForeground(state->window));
    const auto monitor = MonitorFromWindow(root, MONITOR_DEFAULTTONEAREST);
    MONITORINFO monitor_info{}; monitor_info.cbSize = sizeof(monitor_info);
    data->SetBool("secondary", GetMonitorInfoW(monitor, &monitor_info) && !(monitor_info.dwFlags & MONITORINFOF_PRIMARY));
    state->Record("pin_editor_window", data);
    if (!panel->is_presented() || !panel->browser()) return;
    const auto frame = panel->browser()->GetMainFrame();
    if (!floating) {
      auto capture = CaptureShell(panel->browser(), state->evidence_directory / "pinned-editor.png");
      CefPostDelayedTask(TID_UI, base::BindOnce([](CefRefPtr<CefRegistration>) {}, capture), 500);
    }
    const std::string script = std::string("(async(save)=>{") + R"JS(
      const modal=document.querySelector('[role=dialog][aria-modal=true]');
      const [title,url]=modal?.querySelectorAll('input')??[];
      const original=url?.value;
      const initialTitle=title?.value;
      const prefilled=initialTitle==='Pinned editor fixture'&&original?.endsWith('/pinned-editor');
      const focused=document.activeElement===title;
      const button=modal?.querySelector(save?'button[type=submit]':'button[type=button]');
      let pressed=false;
      button?.addEventListener('pointerdown',event=>{
        setTimeout(()=>{
          const style=getComputedStyle(button);
          const reduced=matchMedia('(prefers-reduced-motion: reduce)').matches;
          const scale=parseFloat(style.scale)||1;
          pressed=event.isTrusted&&button.matches(':active')&&style.filter!=='none'&&
              (reduced?scale===1&&style.transitionDuration==='0s':scale<.98);
        },75);
      },{once:true});
      await new Promise(r=>setTimeout(r,205));
      const set=(input,value)=>{
        Object.getOwnPropertyDescriptor(HTMLInputElement.prototype,'value').set.call(input,value);
        input.dispatchEvent(new Event('input',{bubbles:true}));
      };
      let rejected=false;
      if(title&&url){
        set(title,save?'Edited in modal':'Cancelled draft');
        if(save){
          const submit=modal.querySelector('[type=submit]');
          set(url,'javascript:alert(1)');submit.click();
          for(let i=0;i<30&&!modal.querySelector('[role=alert]');++i)await new Promise(r=>setTimeout(r,20));
          rejected=modal.querySelector('[role=alert]')?.textContent==='Invalid address';
          set(url,new URL('/pinned-saved',original).href);
          submit.click();
        }else{
          set(url,new URL('/pinned-cancelled',original).href);
          await new Promise(r=>setTimeout(r,100));
          title.dispatchEvent(new KeyboardEvent('keydown',{key:'Escape',bubbles:true,cancelable:true}));
        }
      }
      console.info('RED_PIN_EDITOR:'+JSON.stringify({save,prefilled,focused,pressed,rejected,initialTitle,original}));
    })()JS" + (floating ? "true)" : "false)");
    frame->ExecuteJavaScript(script, frame->GetURL(), 0);
    // Hold the footer button down, then release outside so the existing
    // cancellation and save checks can still operate on this opening.
    const auto viewport = window->GetClientAreaBoundsInScreen();
    CefPostDelayedTask(TID_UI, base::BindOnce(&PinEditorMouse, panel->browser(), "mousePressed",
        viewport.width - (floating ? 46 : 112), viewport.height - 35, true), 30);
    CefPostDelayedTask(TID_UI, base::BindOnce(&PinEditorMouse, panel->browser(), "mouseReleased", 4, 4, false), 165);
  }, std::weak_ptr<State>(s), floating, page_sample, undimmed), 650);
  CefPostDelayedTask(TID_UI, base::BindOnce([](std::weak_ptr<State> weak, bool floating) {
    if (const auto state = weak.lock(); state && state->window && state->panel_popup) {
      auto data = CefDictionaryValue::Create();
      data->SetBool("floating", floating);
      data->SetBool("closed", !state->panel_popup->is_open());
      data->SetBool("owner_enabled", IsWindowEnabled(state->window->GetWindowHandle()));
      data->SetBool("backdrop_removed", state->embedded && !state->embedded->modal_shade_visible());
      state->Record("pin_editor_closed", data);
      if (state->panel_popup->editing_pin()) state->panel_popup->Hide(true);
    }
  }, std::weak_ptr<State>(s), floating), 1250);
}
void CheckXBadge(CefRefPtr<CefBrowser> browser, bool floating) {
  if (!browser) return;
  const auto frame = browser->GetMainFrame();
  const std::string script = std::string("(async(floating)=>{") + R"JS(
    const q = request => new Promise((resolve, reject) => window.cefQuery({request:JSON.stringify(request),
      persistent:false, onSuccess:raw=>resolve(JSON.parse(raw)), onFailure:(code,message)=>reject({code,message})}));
    const read = () => q({method:'browser.getSnapshot'});
    const report = data => console.info('RED_PEEK:'+JSON.stringify({xBadge:floating?'floating':'docked', ...data}));
    const newTab = document.querySelector('.new-tab');
    const sidebar = document.querySelector('#browser-sidebar');
    let narrowOverflow;
    if (newTab && sidebar) {
      const narrow = newTab.cloneNode(true);
      narrow.style.cssText = 'position:absolute;visibility:hidden;width:150px';
      const label = narrow.querySelector('span');
      label.textContent += ' ' + label.textContent;
      sidebar.append(narrow);
      narrowOverflow = narrow.scrollWidth > narrow.clientWidth + 1;
      narrow.remove();
    }
    console.info('RED_PEEK:'+JSON.stringify({localeHost:floating?'floating':'docked',
      locale:document.documentElement.lang,newTab:newTab?.querySelector('span')?.textContent,
      sidebarLabel:sidebar?.getAttribute('aria-label'),overflow:newTab && newTab.scrollWidth > newTab.clientWidth + 1,narrowOverflow}));
    const wait = async predicate => {
      for (let i=0;i<40;i++) {
        if (await predicate()) return true;
        await new Promise(resolve=>setTimeout(resolve,60));
      }
      return false;
    };
    try {
      const first = await read();
      const pin = first.nodes.find(n=>n.title==='X fixture');
      const id = pin.id;
      const row = document.querySelector(`[data-node-id="${id}"]`);
      const badge = () => row?.querySelector('[data-provider="x"]');
      const setHidden = hidden => q({method:'tab.setXBadgeHidden',id,hidden});
      const setIntegration = enabled => q({method:'tab.setIntegration',id,provider:'twitter',enabled});
      if (floating) {
        const whatsapp=first.nodes.find(n=>n.title==='WhatsApp fixture');
        const duplicate=first.nodes.find(n=>n.title==='WhatsApp duplicate fixture');
        const deep=first.nodes.find(n=>n.title==='WhatsApp deep-link fixture');
        const whatsAppBadge=whatsapp&&document.querySelector(`[data-node-id="${whatsapp.id}"] [data-provider="whatsapp"]`);
        // The dot comes from the hidden check while the pin itself stays unloaded.
        report({whatsAppFloating:!!whatsAppBadge&&!whatsapp.live&&first.activeId!==whatsapp.id,
          otherPinsCold:duplicate?.live===false&&deep?.live===false});
        const startsHidden = first.nodes.find(n=>n.id===id)?.xBadgeHidden===true && !badge();
        await setHidden(false);
        const globallyOff = first.integrations?.twitter===false && !badge();
        const restoredGlobal = (await setIntegration(true)).integrations?.twitter===true;
        const visible = await wait(badge);
        report({startsHidden,globallyOff,restoredGlobal,visible,cleared:visible && await wait(()=>!badge())});
      } else {
        const visible = await wait(badge);
        const style = visible ? getComputedStyle(badge()) : null;
        const corner = style?.position==='absolute' && style.bottom==='7px' && style.right==='7px' && style.width==='6px';
        // X's count arrives without loading the X pin (integrations/x_unread.h).
        const startedInBackground = !pin.live && first.activeId!==id && visible;
        const disabledPin = first.nodes.find(n=>n.title==='X disabled fixture');
        const duplicate = first.nodes.find(n=>n.title==='X duplicate fixture');
        const deep = first.nodes.find(n=>n.title==='X deep-link fixture');
        const otherXCold = disabledPin?.xBadgeHidden===true && disabledPin.live===false && duplicate?.live===false && deep?.live===false;
        row?.querySelector('.node-name')?.click();
        const activated = await wait(async()=> {
          const state = await read();
          return state.activeId===id && state.url===pin.url && state.nodes.length===first.nodes.length;
        });
        await q({method:'tree.activate',id:first.activeId,expectedRevision:(await read()).treeRevision});
        await setHidden(true);
        const hidden = await wait(()=>!badge());
        await setHidden(false);
        const globalDisabled = (await setIntegration(false)).integrations?.twitter===false && await wait(()=>!badge());
        await setHidden(true);
        const paused = ['notion','linear','confluence'];
        const legacyPreferencesLoaded = first.searchEngine==='bing' && first.integrations?.figma===true &&
          paused.every(provider=>!(provider in first.integrations));
        const results = await Promise.all(paused.map(provider=>q({method:'tab.setIntegration',id,provider,enabled:true}).then(()=>0,error=>error.code)));
        report({visible,corner,startedInBackground,otherXCold,activated,hidden,globalDisabled,legacyPreferencesLoaded,
          pausedCommandsRejected:results.every(code=>code===400)});
      }
    } catch (error) { report({error:String(error?.message || error)}); }
  })()JS" + (floating ? "true)" : "false)");
  frame->ExecuteJavaScript(script, frame->GetURL(), 0);
}
void CheckWhatsAppPin(const std::shared_ptr<State>& state) {
  const auto shell = state->browsers.find(state->shell_id);
  if (shell == state->browsers.end()) return;
  const auto snapshot = state->workspace->Snapshot();
  const auto nodes = snapshot->GetList("nodes");
  for (size_t i = 0; i < nodes->GetSize(); ++i) {
    const auto node = nodes->GetDictionary(i);
    if (node->GetString("title") != "WhatsApp fixture") continue;
    const auto id = node->GetString("id").ToString();
    auto result = CefDictionaryValue::Create();
    result->SetBool("protected", state->workspace->ActiveNode() != id && state->workspace->AutoArchiveProtected(id));
    state->Record("whatsapp_startup_pin", result);
  }
  const auto frame = shell->second->GetMainFrame();
  frame->ExecuteJavaScript(R"JS((async()=>{
    const q=request=>new Promise((resolve,reject)=>window.cefQuery({request:JSON.stringify(request),persistent:false,
      onSuccess:raw=>resolve(JSON.parse(raw)),onFailure:(code,message)=>reject(message)}));
    const read=()=>q({method:'browser.getSnapshot'});
    const wait=async predicate=>{
      for(let i=0;i<24;i++) {
        if(await predicate(await read()))return true;
        await new Promise(resolve=>setTimeout(resolve,50));
      }
      return false;
    };
    const report=data=>console.info('RED_PEEK:'+JSON.stringify(data));
    try {
      const before=await read();
      const pin=before.nodes.find(n=>n.title==='WhatsApp fixture');
      const row=document.querySelector(`[data-node-id="${pin.id}"]`);
      const badge=()=>row?.querySelector('[data-provider="whatsapp"]');
      const docked=!pin.live && !!badge() && before.activeId!==pin.id;
      row?.querySelector('.node-name')?.click();
      const activated=await wait(s=>s.activeId===pin.id && s.url===pin.url && s.nodes.length===before.nodes.length);
      await q({method:'tree.activate',id:before.activeId,expectedRevision:(await read()).treeRevision});
      await q({method:'tree.close',id:pin.id,expectedRevision:(await read()).treeRevision});
      // Unloaded again: the last hidden check's count keeps the dot until the next check.
      const unloaded=await wait(s=>s.nodes.some(n=>n.id===pin.id && n.url===pin.url && !n.live) && !!badge());
      report({whatsAppDocked:docked,activated,unloaded});
    } catch (error) { report({whatsAppError:String(error)}); }
  })())JS", frame->GetURL(), 0);
}
class PinnedHintHover final : public CefDevToolsMessageObserver {
 public:
  int query = 0;
  void OnDevToolsMethodResult(CefRefPtr<CefBrowser> browser, int id, bool success, const void* bytes, size_t size) override {
    if (id != query || !success) return;
    const auto json = CefParseJSON(std::string(static_cast<const char*>(bytes), size), JSON_PARSER_RFC);
    const auto result = json && json->GetType() == VTYPE_DICTIONARY ? json->GetDictionary()->GetDictionary("result") : nullptr;
    const auto point = result ? result->GetDictionary("value") : nullptr;
    if (!point) return;
    const auto mouse = CefDictionaryValue::Create();
    mouse->SetString("type", "mouseMoved");
    mouse->SetInt("x", point->GetInt("x")); mouse->SetInt("y", point->GetInt("y"));
    browser->GetHost()->ExecuteDevToolsMethod(0, "Input.dispatchMouseEvent", mouse);
  }
 private:
  IMPLEMENT_REFCOUNTING(PinnedHintHover);
};
void CheckPinnedHint(const std::shared_ptr<State>& s, bool floating) {
  const auto host = s->browsers.find(floating ? s->peek_id : s->shell_id);
  if (host == s->browsers.end()) return;
  const auto snapshot = s->workspace->Snapshot();
  const auto nodes = snapshot->GetList("nodes");
  for (size_t i = 0; nodes && i < nodes->GetSize(); ++i) {
    const auto node = nodes->GetDictionary(i);
    const auto home = node->GetString("url").ToString();
    if (!home.ends_with("/pinned-saved")) continue;
    const auto id = node->GetString("id").ToString();
    auto command = CefDictionaryValue::Create();
    command->SetString("method", "tree.activate"); command->SetString("id", id);
    command->SetDouble("expectedRevision", snapshot->GetDouble("treeRevision"));
    s->workspace->Handle(command, [weak = std::weak_ptr<State>(s), host = host->second, id, home, floating](int code, const std::string&, CefRefPtr<CefDictionaryValue>) {
      if (code) return;
      CefPostDelayedTask(TID_UI, base::BindOnce([](std::weak_ptr<State> weak, CefRefPtr<CefBrowser> host,
          std::string id, std::string home, bool floating) {
        const auto state = weak.lock();
        if (!state || !host->IsValid()) return;
        const auto page = state->workspace->LiveBrowser(id);
        if (!page) return;
        page->GetMainFrame()->LoadURL(home.substr(0, home.rfind('/')) + "/pinned-away");
        auto pin_id = CefValue::Create(); pin_id->SetString(id);
        const std::string script = std::string("(async(floating,id)=>{") + R"JS(
          const pause=ms=>new Promise(resolve=>setTimeout(resolve,ms));
          const wait=async check=>{for(let i=0;i<25;i++){const result=await check();if(result)return result;await pause(40);}};
          const row=await wait(()=>document.querySelector(`[data-node-id="${CSS.escape(id)}"] .tree-row[data-pinned-away=true]`));
          if(!row)return null;
          const icon=row.querySelector('.node-icon'), title=row.querySelector('.node-title'), hint=row.querySelector('.pinned-reset-hint');
          row.scrollIntoView({block:'nearest'});
          const titleY=title.getBoundingClientRect().top, height=row.getBoundingClientRect().height;
          const hidden=getComputedStyle(hint).opacity==='0'&&getComputedStyle(icon).backgroundColor==='rgba(0, 0, 0, 0)';
          icon.addEventListener('pointerenter',async event=>{
            await pause(220);
            const shown=event.isTrusted&&icon.matches(':hover')&&getComputedStyle(hint).opacity==='1'&&hint.textContent.trim().length>0;
            const lifted=titleY-title.getBoundingClientRect().top>=4;
            const sameHeight=height===row.getBoundingClientRect().height;
            const arrowCursor=getComputedStyle(icon).cursor==='default';
            const root=document.documentElement, theme=root.getAttribute('data-theme');
            icon.style.transition='none';
            root.dataset.theme='light';
            const light=getComputedStyle(icon).backgroundColor;
            root.dataset.theme='dark';
            const dark=getComputedStyle(icon).backgroundColor;
            if(theme===null)root.removeAttribute('data-theme');else root.setAttribute('data-theme',theme);
            icon.style.removeProperty('transition');
            const iconHeight=icon.getBoundingClientRect().height;
            const highlight=iconHeight>=height-2&&light==='rgba(0, 0, 0, 0.09)'&&dark==='rgba(255, 255, 255, 0.14)';
            icon.click();
            const atHome=!!await wait(async()=>{
              if(row.dataset.pinnedAway!=='false'||row.querySelector('.pinned-reset-hint'))return false;
              const snapshot=await new Promise(resolve=>window.cefQuery({request:'{"method":"browser.getSnapshot"}',persistent:false,
                onSuccess:raw=>resolve(JSON.parse(raw)),onFailure:()=>resolve(null)}));
              const pin=snapshot?.nodes.find(n=>n.id===id);
              return snapshot?.activeId===id&&pin?.currentUrl===pin?.url&&pin?.url.endsWith('/pinned-saved');
            });
            console.info('RED_PEEK:'+JSON.stringify({pinnedHint:floating?'floating':'docked',hidden,shown,lifted,sameHeight,arrowCursor,highlight,atHome,light,dark,height,iconHeight}));
          },{once:true});
          const box=icon.getBoundingClientRect();
          return {x:Math.round(box.x+box.width/2),y:Math.round(box.y+box.height/2)};
        })()JS" + (floating ? "true," : "false,") + CefWriteJSON(pin_id, JSON_WRITER_DEFAULT).ToString() + ")";
        CefRefPtr<PinnedHintHover> observer = new PinnedHintHover;
        const auto registration = host->GetHost()->AddDevToolsMessageObserver(observer);
        auto params = CefDictionaryValue::Create(); params->SetString("expression", script);
        params->SetBool("awaitPromise", true); params->SetBool("returnByValue", true);
        observer->query = host->GetHost()->ExecuteDevToolsMethod(0, "Runtime.evaluate", params);
        CefPostDelayedTask(TID_UI, base::BindOnce([](CefRefPtr<CefRegistration>) {}, registration), 1600);
      }, weak, host, id, home, floating), 300);
    });
    return;
  }
}
void CheckLibraryHost(const std::shared_ptr<State>& state, bool floating);
void CheckFigmaCreate(std::weak_ptr<State> weak, Hover invoke, std::shared_ptr<POINT> cursor) {
  const auto s=weak.lock(); if(!s)return;
  const auto snapshot=s->workspace->Snapshot();
  const auto nodes=snapshot->GetList("nodes");
  std::string node;
  for(size_t i=0;i<nodes->GetSize();++i) {
    const auto row=nodes->GetDictionary(i);
    if(row->GetBool("pinned")&&row->GetString("url")=="https://www.figma.com/files")node=row->GetString("id").ToString();
  }
  invoke(s->sidebar_peek->browser(),false);
  CefPostDelayedTask(TID_UI,base::BindOnce([](std::weak_ptr<State> weak,std::string node,size_t count,std::shared_ptr<POINT> cursor){
    const auto s=weak.lock();if(!s)return;
    const auto hover=s->panel_popup->hover_panel();
    if (const auto browser=hover&&hover->is_presented()?hover->browser():nullptr) {
      const auto frame=browser->GetMainFrame();
      frame->ExecuteJavaScript("document.querySelector('[data-action=\"createFigma\"]')?.click()",frame->GetURL(),0);
    }
    CefPostDelayedTask(TID_UI,base::BindOnce([](std::weak_ptr<State> weak,std::string node,size_t count,std::shared_ptr<POINT> cursor){
      const auto s=weak.lock();if(!s)return;
      const auto browser=s->workspace->LiveBrowser(node);
      const auto frame=browser?browser->GetMainFrame():nullptr;
      const auto snapshot=s->workspace->Snapshot();
      const auto nodes=snapshot->GetList("nodes");
      bool home=false;
      for(size_t i=0;i<nodes->GetSize();++i) {
        const auto row=nodes->GetDictionary(i);
        if(row->GetString("id")==node)home=row->GetString("url")=="https://www.figma.com/files";
      }
      auto result=CefDictionaryValue::Create();
      result->SetBool("passed",frame&&frame->GetURL().ToString()==CommandDocumentUrl("newFigmaDesign")&&
          s->workspace->ActiveNode()==node&&nodes->GetSize()==count&&home);
      s->Record("figma_create_in_pin",result);
      s->panel_popup->Hide(false);s->sidebar_peek->Hide();s->workspace->SetSidebarVisible(true);
      SetCursorPos(cursor->x,cursor->y);
      Later(s, 400, [](const std::shared_ptr<State>& s) { CheckPinnedHint(s, false); });
      Later(s, 1900, [](const std::shared_ptr<State>& s) {
        s->workspace->SetSidebarVisible(false);
        s->sidebar_peek->Show();
      });
      Later(s, 2500, [](const std::shared_ptr<State>& s) { CheckPinnedHint(s, true); });
      // Library last, after these tab switches: each check records 1.3 s
      // after it starts (open, close, glide) and must keep the same page.
      Later(s, 4300, [](const std::shared_ptr<State>& s) { s->workspace->SetSidebarVisible(true); CheckLibraryHost(s, false); });
      Later(s, 6000, [](const std::shared_ptr<State>& s) { s->workspace->SetSidebarVisible(false); s->sidebar_peek->Show(); });
      Later(s, 6900, [](const std::shared_ptr<State>& s) { CheckLibraryHost(s, true); });
    },weak,node,count,cursor),1200);
  },weak,node,nodes->GetSize(),cursor),350);
}
void CheckFloating(std::weak_ptr<State> weak, Hover invoke, std::shared_ptr<POINT> cursor) {
  const auto s=weak.lock();if(!s)return;
  s->panel_popup->Hide(false);s->workspace->SetSidebarVisible(false);s->sidebar_peek->Show();
  CefPostDelayedTask(TID_UI,base::BindOnce([](std::weak_ptr<State> weak,Hover invoke,std::shared_ptr<POINT> cursor){
    const auto s=weak.lock();if(!s)return;invoke(s->sidebar_peek->browser(),false);
    CefPostDelayedTask(TID_UI,base::BindOnce([](std::weak_ptr<State> weak,Hover invoke,std::shared_ptr<POINT> cursor){
      const auto s=weak.lock();if(!s)return;
      const auto hover=s->panel_popup->hover_panel();
      auto data=CefDictionaryValue::Create();data->SetBool("open",hover&&hover->is_presented());
      if (hover && hover->window()) {
        const auto handle = hover->window()->GetWindowHandle();
        DWORD cloaked = 0; BYTE alpha = 255;
        DwmGetWindowAttribute(handle, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
        GetLayeredWindowAttributes(handle, nullptr, &alpha, nullptr);
        data->SetBool("settled", !cloaked && alpha == 255);
        RECT bounds{};
        data->SetBool("hit_testable", GetWindowRect(handle, &bounds) &&
            GetAncestor(WindowFromPoint({(bounds.left + bounds.right) / 2, (bounds.top + bounds.bottom) / 2}), GA_ROOT) == handle);
      }
      s->Record("integration_floating",data);
      POINT away{800,650};ClientToScreen(s->window->GetWindowHandle(),&away);SetCursorPos(away.x,away.y);
      CefPostDelayedTask(TID_UI,base::BindOnce([](std::weak_ptr<State> weak,Hover invoke,std::shared_ptr<POINT> cursor){
        if(const auto s=weak.lock()) {auto data=CefDictionaryValue::Create();data->SetBool("closed",!s->panel_popup->is_open());s->Record("integration_pointer_left",data);}
        CheckFigmaCreate(weak,invoke,cursor);
      },weak,invoke,cursor),650);
    },weak,invoke,cursor),650);
  },weak,invoke,cursor),550);
}
struct CoverWindow {
  HWND hwnd=nullptr;
  ~CoverWindow() { if (IsWindow(hwnd)) DestroyWindow(hwnd); }
};
void CheckInactive(std::weak_ptr<State> weak, Hover invoke, std::shared_ptr<POINT> cursor) {
  const auto s=weak.lock();if(!s||!s->window)return;
  const auto shell=s->browsers.find(s->shell_id);if(shell==s->browsers.end())return;
  const auto root=s->window->GetWindowHandle();
  RECT bounds{};GetWindowRect(root,&bounds);
  auto cover=std::make_shared<CoverWindow>();
  WNDCLASSW cls{};
  cls.lpfnWndProc=DefWindowProcW;cls.hInstance=GetModuleHandleW(nullptr);
  cls.lpszClassName=L"SolOwnedHoverProbeCover";cls.hbrBackground=GetSysColorBrush(COLOR_WINDOW);
  RegisterClassW(&cls);
  cover->hwnd=CreateWindowExW(WS_EX_TOOLWINDOW,cls.lpszClassName,L"Sol hover probe cover",
      WS_POPUP|WS_BORDER,bounds.left+560,bounds.top+260,140,100,nullptr,nullptr,cls.hInstance,nullptr);
  if (!cover->hwnd) {CheckFloating(weak,invoke,cursor);return;}
  s->panel_popup->Hide(false);
  ShowWindow(cover->hwnd,SW_SHOW);SetForegroundWindow(cover->hwnd);
  invoke(shell->second,true);
  CefPostDelayedTask(TID_UI,base::BindOnce([](std::weak_ptr<State> weak,Hover invoke,std::shared_ptr<POINT> cursor,std::shared_ptr<CoverWindow> cover){
    const auto s=weak.lock();if(!s||!s->window)return;
    const auto hover=s->panel_popup->hover_panel();
    auto data=CefDictionaryValue::Create();data->SetBool("open",hover&&hover->is_presented());
    data->SetBool("cover_kept_focus",GetForegroundWindow()==cover->hwnd);
    s->Record("integration_inactive_owner",data);
    POINT p{};
    const auto shell=s->browsers.find(s->shell_id);
    if (shell!=s->browsers.end()) {
      const auto surface=shell->second->GetHost()->GetWindowHandle();
      const float scale=GetDpiForWindow(surface)/96.f;
      p={static_cast<LONG>(60*scale),static_cast<LONG>(116*scale)};
      ClientToScreen(surface,&p);
    }
    SetWindowPos(cover->hwnd,HWND_TOP,p.x-12,p.y-12,140,100,SWP_NOACTIVATE);
    CefPostDelayedTask(TID_UI,base::BindOnce([](std::weak_ptr<State> weak,Hover invoke,std::shared_ptr<POINT> cursor,std::shared_ptr<CoverWindow> cover,POINT point){
      const auto s=weak.lock();if(!s||!s->window)return;
      auto data=CefDictionaryValue::Create();data->SetBool("covered",GetAncestor(WindowFromPoint(point),GA_ROOT)==cover->hwnd);
      data->SetBool("closed",!s->panel_popup->is_open());s->Record("integration_covered_owner",data);
      const auto shell=s->browsers.find(s->shell_id);if(shell!=s->browsers.end())invoke(shell->second,true);
      CefPostDelayedTask(TID_UI,base::BindOnce([](std::weak_ptr<State> weak,Hover invoke,std::shared_ptr<POINT> cursor,std::shared_ptr<CoverWindow> cover){
        const auto s=weak.lock();if(!s||!s->window)return;
        auto data=CefDictionaryValue::Create();data->SetBool("blocked",!s->panel_popup->is_open());
        s->Record("integration_covered_request",data);
        DestroyWindow(cover->hwnd);cover->hwnd=nullptr;
        s->window->Activate();SetForegroundWindow(s->window->GetWindowHandle());
        CheckFloating(weak,invoke,cursor);
      },weak,invoke,cursor,cover),200);
    },weak,invoke,cursor,cover,p),580);
  },weak,invoke,cursor,cover),280);
}
// Runs only in the isolated sidebar acceptance profile, after existing checks.
void IntegrationProbe(const std::shared_ptr<State>& s) {
  const auto snapshot=s->workspace->Snapshot(); const auto id=snapshot->GetString("activeId").ToString();
  auto pin=CefDictionaryValue::Create();pin->SetString("method","tree.pin");pin->SetString("id",id);
  pin->SetBool("value",true);pin->SetDouble("expectedRevision",snapshot->GetDouble("treeRevision"));
  s->workspace->Handle(pin,[weak=std::weak_ptr<State>(s),id](int code,const std::string&,CefRefPtr<CefDictionaryValue>){
    const auto state=weak.lock();if(!state)return;if(code){auto data=CefDictionaryValue::Create();data->SetInt("code",code);state->Record("integration_setup_failed",data);return;}
    std::string error;state->workspace->SetPinnedPage(id,"https://www.figma.com/files","Integration fixture",error);
    CefPostDelayedTask(TID_UI,base::BindOnce([](std::weak_ptr<State> weak,std::string id){
      const auto state=weak.lock();if(!state)return;
      auto close=CefDictionaryValue::Create();close->SetString("method","tree.close");close->SetString("id",id);
      close->SetDouble("expectedRevision",state->workspace->Snapshot()->GetDouble("treeRevision"));
      state->workspace->Handle(close,[weak,id](int code,const std::string&,CefRefPtr<CefDictionaryValue>){
        const auto state=weak.lock();if(!state || code)return;
        const auto profile=state->workspace->ProfileForNode(id);if(!profile || !profile->history)return;
        profile->history->ImportVisits({{0,"https://www.figma.com/design/owned-fixture/Fixture","Owned recent design",1000},
          {0,"https://www.figma.com/files/team/123/drafts","Drafts",1500},
          // One file seen twice: newest while loading ("Figma"), older with its name. Another named only in its address.
          {0,"https://www.figma.com/design/fixtureKey?fuid=1","Figma",2600},
          {0,"https://www.figma.com/design/fixtureKey/Abyss-Cup?node-id=0-1","Abyss Cup \xE2\x80\x93 Figma",2500},
          {0,"https://www.figma.com/design/slugKey/Slug-Name?t=a","Figma",2400},
          {0,"https://www.figma.com.evil.test/design/trap","Spoofed host",2000},
          {0,"https://www.figma.com/login","Login is not a document",3000}},[weak,id](int){
          // tree.close commits before CEF finishes destroying its old browser.
          // Wait for its focus/URL callbacks before requesting the hover card.
          CefPostDelayedTask(TID_UI,base::BindOnce([](std::weak_ptr<State> weak,std::string id){
          const auto state=weak.lock();if(!state)return;
          state->window->Activate();
          const auto root=state->window->GetWindowHandle();
          if (!SetForegroundWindow(root) && !PopupOwnerIsForeground(state->window)) {
            // The runner launches this disposable window without a click.
            // Like the utility-windows probe, let it acquire real foreground.
            INPUT keys[2]{};keys[0].type=keys[1].type=INPUT_KEYBOARD;
            keys[0].ki.wVk=keys[1].ki.wVk=VK_MENU;keys[1].ki.dwFlags=KEYEVENTF_KEYUP;
            SendInput(2,keys,sizeof(INPUT));SetForegroundWindow(root);
          }
          auto cursor=std::make_shared<POINT>();GetCursorPos(cursor.get());
          const auto invoke=[weak,id](CefRefPtr<CefBrowser> browser,bool favorite){
            const auto state=weak.lock();if(!state || !browser)return;
            const auto hover=[weak,id,browser](int code,const std::string&,CefRefPtr<CefDictionaryValue>){
              const auto state=weak.lock();if(!state||code||!browser->IsValid())return;
              POINT p{60,116};const auto h=browser->GetHost()->GetWindowHandle();
              const float scale=GetDpiForWindow(h)/96.f;p.x=static_cast<LONG>(p.x*scale);p.y=static_cast<LONG>(p.y*scale);
              POINT actual{};
              const bool moved=ClientToScreen(h,&p)&&SetCursorPos(p.x,p.y)&&GetCursorPos(&actual)&&actual.x==p.x&&actual.y==p.y;
              auto evidence=CefDictionaryValue::Create();evidence->SetBool("matched",moved);
              evidence->SetBool("foreground",PopupOwnerIsForeground(state->window));state->Record("integration_probe_cursor",evidence);
              const std::string script="window.cefQuery({request:JSON.stringify({method:'integration.preview',id:'"+id+"',x:16,y:100,width:160,height:32}),persistent:false,onSuccess:()=>{},onFailure:()=>{}})";
              browser->GetMainFrame()->ExecuteJavaScript(script,browser->GetMainFrame()->GetURL(),0);
            };
            const auto seed=state->workspace->DetachedSeed(id);
            if (!seed) return;
            if (seed->favorite==favorite) { hover(0,{},nullptr); return; }
            auto place=CefDictionaryValue::Create();place->SetString("method","tree.place");place->SetString("id",id);
            place->SetString("parentId","");place->SetInt("position",-1);place->SetString("section",favorite?"favorites":"pinned");
            place->SetDouble("expectedRevision",state->workspace->Snapshot()->GetDouble("treeRevision"));
            state->workspace->Handle(place,hover);
          };
          const auto shell=state->browsers.find(state->shell_id);if(shell==state->browsers.end())return;
          invoke(shell->second,true);
          CefPostDelayedTask(TID_UI,base::BindOnce([](std::weak_ptr<State> weak,Hover invoke,std::shared_ptr<POINT> cursor){
            const auto state=weak.lock();if(!state)return;
            const auto hover=state->panel_popup->hover_panel();
            auto data=CefDictionaryValue::Create();data->SetBool("open",hover&&hover->is_presented());
            data->SetBool("kept_focus",GetForegroundWindow()==state->window->GetWindowHandle());
            data->SetBool("views_visible",hover&&hover->window()&&hover->window()->IsVisible());
            state->Record("integration_docked",data);
            if(const auto browser=hover?hover->browser():nullptr) {
              const auto capture=CaptureShell(browser,state->evidence_directory/"integration-card.png");
              CefPostDelayedTask(TID_UI,base::BindOnce([](CefRefPtr<CefRegistration>){},capture),1000);
              browser->GetMainFrame()->ExecuteJavaScript(
              "(async()=>{const q=r=>new Promise(ok=>cefQuery({request:JSON.stringify(r),persistent:false,onSuccess:()=>ok({code:0}),onFailure:(code,message)=>ok({code,message})}));"
              "const stale=await q({method:'integration.open',request:-1,revision:0,index:0});"
              "const staleCreate=await q({method:'integration.control',request:-1,action:'createFigma'});"
              "console.info('RED_INTEGRATION:'+JSON.stringify({rows:[...document.querySelectorAll('.integration-figma-row .archive-text > span')].map(r=>r.textContent),"
              "times:[...document.querySelectorAll('.integration-figma-row time')].map(r=>r.dateTime),"
              "visible:document.visibilityState==='visible',stale,staleCreate}));})()",
              browser->GetMainFrame()->GetURL(),0);
            }
            CefPostDelayedTask(TID_UI,base::BindOnce(&CheckInactive,weak,invoke,cursor),120);
          },weak,Hover(invoke),cursor),280);
          },weak,id),600);
        });
      });
    },weak,id),300);
  });
}
void CheckLibraryHost(const std::shared_ptr<State>& state, bool floating) {
  const auto shell = state->browsers.find(state->shell_id);
  const auto source = floating && state->sidebar_peek ? state->sidebar_peek->browser() : shell == state->browsers.end() ? nullptr : shell->second;
  if (!source || shell == state->browsers.end()) return;
  const auto before = state->workspace->Snapshot();
  const int x = before->GetDictionary("contentBounds")->GetInt("x");
  const auto active = state->content;
  const auto frame = source->GetMainFrame();
  frame->ExecuteJavaScript("document.querySelector('[data-action=library]')?.click()", frame->GetURL(), 0);
  CefPostDelayedTask(TID_UI, base::BindOnce([](std::weak_ptr<State> weak, bool floating, int x, CefRefPtr<CefBrowser> active) {
    const auto state = weak.lock();
    if (!state) return;
    const auto snapshot = state->workspace->Snapshot();
    const auto bounds = snapshot->GetDictionary("contentBounds");
    const auto client = state->window->GetClientAreaBoundsInScreen();
    const auto expected = WorkspaceContentArea(client.width, client.height, state->sidebar_visible,
                                               state->sidebar_width, state->VisibleLibrarySection(), state->library_spaces);
    const bool opened = state->library_open && bounds && bounds->GetInt("x") == expected.x &&
                        bounds->GetInt("width") == expected.width && expected.width >= 320;
    const bool layout = state->embedded && state->embedded->ActiveMatchesBounds();
    const auto shell = state->browsers.find(state->shell_id);
    if (shell == state->browsers.end()) return;
    const auto frame = shell->second->GetMainFrame();
    frame->ExecuteJavaScript("document.querySelector('[data-action=close-library]')?.click()", frame->GetURL(), 0);
    CefPostDelayedTask(TID_UI, base::BindOnce([](std::weak_ptr<State> weak, bool floating, int x, CefRefPtr<CefBrowser> active,
                                                bool opened, bool layout) {
      const auto state = weak.lock(); if (!state) return;
      const auto after = state->workspace->Snapshot();
      auto data = CefDictionaryValue::Create();
      data->SetString("surface", floating ? "floating" : "docked");
      data->SetBool("opened", opened && layout);
      data->SetBool("restored", !state->library_open && state->sidebar_visible != floating &&
                    after->GetDictionary("contentBounds")->GetInt("x") == x && state->embedded->ActiveMatchesBounds());
      data->SetBool("same_browser", !active || (state->content && active->IsSame(state->content)));
      data->SetInt("x_before", x); data->SetInt("x_after", after->GetDictionary("contentBounds")->GetInt("x"));
      data->SetBool("page_matches", state->embedded->ActiveMatchesBounds()); data->SetBool("sidebar_visible", state->sidebar_visible);
      state->Record("library_sidebar_host", data);
    }, weak, floating, x, active, opened, layout), 600);  // Library's page glide is 420 ms.
  }, std::weak_ptr<State>(state), floating, x, active), 700);
}
void Record(const std::shared_ptr<State>& s, const char* event) {
  auto data = CefDictionaryValue::Create();
  // Colour in the middle of where the open sidebar rests, from a capture of
  // the browser window itself (children included), whatever covers it.
  const HWND root = s->window->GetWindowHandle();
  RECT client; GetClientRect(root, &client);
  const float scale = s->window->GetDisplay()->GetDeviceScaleFactor();
  HDC screen = GetDC(nullptr), memory = CreateCompatibleDC(screen);
  HBITMAP bitmap = CreateCompatibleBitmap(screen, client.right, client.bottom);
  auto old = SelectObject(memory, bitmap);
  PrintWindow(root, memory, PW_CLIENTONLY | PW_RENDERFULLCONTENT);
  const COLORREF c = GetPixel(memory, static_cast<int>(120 * scale), static_cast<int>(420 * scale));
  SelectObject(memory, old); DeleteObject(bitmap); DeleteDC(memory); ReleaseDC(nullptr, screen);
  char hex[8]; snprintf(hex, sizeof(hex), "#%02x%02x%02x", GetRValue(c), GetGValue(c), GetBValue(c));
  data->SetString("pixel", hex);
  // The sidebar's own rendering inside its host window.
  if (const HWND host = s->sidebar_peek ? s->sidebar_peek->host() : nullptr; host && IsWindowVisible(host)) {
    data->SetBool("root_child", (GetWindowLongPtrW(host, GWL_STYLE) & WS_CHILD) && GetAncestor(host, GA_PARENT) == root);
    data->SetBool("root_owned", (GetWindowLongPtrW(host, GWL_STYLE) & WS_POPUP) && GetWindow(host, GW_OWNER) == root);
    data->SetDouble("display_scale", scale);
    if (s->sidebar_peek->is_open() && s->content) {
      RECT bounds{}; GetWindowRect(host, &bounds);
      const LONG middle = (bounds.top + bounds.bottom) / 2;
      const HWND inside = WindowFromPoint({(bounds.left + bounds.right) / 2, middle});
      const HWND outside = WindowFromPoint({bounds.right + 2, middle});
      const HWND page = s->content->GetHost()->GetWindowHandle();
      data->SetBool("panel_hit", IsChild(host, inside));
      data->SetBool("page_hit", page == outside || IsChild(page, outside));
    }
    RECT box; GetClientRect(host, &box);
    HDC screen2 = GetDC(nullptr), memory2 = CreateCompatibleDC(screen2);
    HBITMAP bitmap2 = CreateCompatibleBitmap(screen2, box.right, box.bottom);
    auto old2 = SelectObject(memory2, bitmap2);
    PrintWindow(host, memory2, PW_RENDERFULLCONTENT);
    const COLORREF h = GetPixel(memory2, box.right / 2, box.bottom - 40);
    SelectObject(memory2, old2); DeleteObject(bitmap2); DeleteDC(memory2); ReleaseDC(nullptr, screen2);
    char hhex[8]; snprintf(hhex, sizeof(hhex), "#%02x%02x%02x", GetRValue(h), GetGValue(h), GetBValue(h));
    data->SetString("host_pixel", hhex);
  }
  data->SetBool("open", s->sidebar_peek && s->sidebar_peek->is_open());
  data->SetBool("visible", s->sidebar_peek && s->sidebar_peek->window_visible());
  s->Record(event, data);
}
}  // namespace
// Measure the first changed screen pixel 32 DIP inside the window from a real
// pointer entry. Sampling from a worker keeps GetPixel off the CEF UI thread.
// |count| entries; the first waits |first_wait_ms| with the pointer away.
static void MeasureEdge(const std::shared_ptr<State>& state, int count, int first_wait_ms) {
  const HWND root = state->window->GetWindowHandle();
  const auto scale = state->window->GetDisplay()->GetDeviceScaleFactor();
  state->sidebar_peek_probe = false;  // Exercise ordinary edge polling and leave detection.
  POINT origin{};
  ClientToScreen(root, &origin);
  const LONG y = origin.y + static_cast<LONG>(350 * scale);
  const POINT edge{origin.x + static_cast<LONG>(3 * scale), y};
  const POINT sample{origin.x + static_cast<LONG>(32 * scale), y};
  const POINT away{origin.x + static_cast<LONG>(550 * scale), y};
  std::thread([root, edge, sample, away, weak = std::weak_ptr<State>(state), scale, count, first_wait_ms] {
    using Clock = std::chrono::steady_clock;
    // steady_clock reads QueryPerformanceCounter, like the native marks.
    const auto clock_ms = [](Clock::time_point t) { return std::chrono::duration<double, std::milli>(t.time_since_epoch()).count(); };
    std::vector<double> entered, painted;
    auto own = [root](POINT p) {
      return GetAncestor(WindowFromPoint(p), GA_ROOTOWNER) == root;
    };
    std::vector<double> delays;
    std::string error;
    POINT old{}, last{};
    const bool have_cursor = GetCursorPos(&old) != FALSE;
    bool moved = false;
    auto move = [&](POINT p) {
      POINT current{};
      if (!have_cursor || !GetCursorPos(&current) || (moved && (current.x != last.x || current.y != last.y)) ||
          !own(p) || !SetCursorPos(p.x, p.y)) return false;
      moved = true; last = p; return true;
    };
    HDC dc = GetDC(nullptr);
    const COLORREF page = RGB(33, 100, 145);
    // 15 cycles per process, then repeat in reverse host order for 30 per host.
    for (int i = 0; i < count && dc; ++i) {
      if (!move(away)) { error = "cursor moved or window covered"; break; }
      Sleep(i ? 650 : first_wait_ms);
      if (!own(sample) || GetPixel(dc, sample.x, sample.y) != page) { error = "fixture pixel unavailable"; break; }
      const auto start = Clock::now();
      if (!move(edge)) { error = "edge unavailable"; break; }
      bool shown = false;
      while (Clock::now() - start < std::chrono::seconds(1)) {
        if (!own(sample)) break;
        const auto color = GetPixel(dc, sample.x, sample.y);
        // The neutral panel is much brighter in red than the blue fixture.
        // A dark shadow over that fixture cannot cross this threshold.
        if (color != CLR_INVALID && GetRValue(color) > 100) {
          const auto now = Clock::now();
          delays.push_back(std::chrono::duration<double, std::milli>(now - start).count());
          entered.push_back(clock_ms(start)); painted.push_back(clock_ms(now));
          shown = true; break;
        }
        Sleep(1);
      }
      if (!shown) { error = "no visible panel pixel"; break; }
      Sleep(190);  // Complete the 170 ms entry before leaving.
    }
    if (dc) ReleaseDC(nullptr, dc); else error = "screen DC unavailable";
    POINT current{};
    if (moved && GetCursorPos(&current) && current.x == last.x && current.y == last.y) SetCursorPos(old.x, old.y);
    CefPostTask(TID_UI, base::BindOnce([](std::weak_ptr<State> weak, std::vector<double> delays, std::vector<double> entered,
                                          std::vector<double> painted, std::string error, float scale, int count) {
      if (auto s = weak.lock()) {
        s->sidebar_peek_probe = true;
        s->sidebar_peek->Hide();
        auto data = CefDictionaryValue::Create();
        const auto list = [](const std::vector<double>& values) {
          auto out = CefListValue::Create();
          for (size_t i = 0; i < values.size(); ++i) out->SetDouble(i, values[i]);
          return out;
        };
        data->SetList("edge_to_pixel_ms", list(delays)); data->SetString("error", error); data->SetDouble("scale", scale);
        data->SetList("entered_ms", list(entered)); data->SetList("painted_ms", list(painted));
        const HWND host = s->sidebar_peek->host();
        data->SetBool("child", host && (GetWindowLongPtrW(host, GWL_STYLE) & WS_CHILD) &&
            GetAncestor(host, GA_PARENT) == s->window->GetWindowHandle());
        s->sidebar_peek_done = error.empty() && static_cast<int>(delays.size()) == count;
        s->Record("peek_edge_measurement", data);
      }
    }, weak, std::move(delays), std::move(entered), std::move(painted), std::move(error), scale, count));
  }).detach();
}
static void Start(const std::shared_ptr<State>& state);
void OnSidebarPeekProbeLoad(const std::shared_ptr<State>& state, const std::string& url) {
  if (!state->sidebar_peek_probe || !url.starts_with("http") || !state->workspace) return;
  if (state->sidebar_peek_phase == 0 && url.ends_with("/a")) {
    state->sidebar_peek_phase = 1; state->workspace->OpenLink(url.substr(0, url.rfind('/')) + "/b", true);
  } else if (state->sidebar_peek_phase == 1 && url.ends_with("/b")) {
    state->sidebar_peek_phase = 2;
    Start(state);
  }
}
// After the shell's own IPC checks (they need the docked sidebar).
static void Start(const std::shared_ptr<State>& state) {
  if (!state->ipc_rejections) { Later(state, 100, &Start); return; }
  {
    // Leave room left of the window so the slide's clipping is visible on screen.
    if (!state->window->IsMaximized()) {
      const auto area = state->window->GetDisplay()->GetWorkArea();
      state->window->SetBounds(CefRect(area.x + 240, area.y + 80, 1100, 820));
    }
    state->workspace->SetSidebarVisible(false);
    const auto mode = CefCommandLine::GetGlobalCommandLine()->GetSwitchValue("probe-sidebar-peek").ToString();
    if (!mode.empty()) ActivateProbeWindow(state);
    if (mode == "child-edge" || mode == "owned-edge") {
      Later(state, 500, [](const std::shared_ptr<State>& s) { MeasureEdge(s, 15, 900); });
      return;
    }
    // "<host>-cold-<ms>": the first opening of this process, after |ms| idle.
    if (mode.starts_with("child-cold-") || mode.starts_with("owned-cold-")) {
      const int wait = std::clamp(std::atoi(mode.substr(11).c_str()), 300, 60000);
      MeasureEdge(state, 1, wait);
      return;
    }
    Later(state,650,[](const std::shared_ptr<State>& s){
      const auto shell=s->browsers.find(s->shell_id);
      if(shell!=s->browsers.end())shell->second->GetMainFrame()->ExecuteJavaScript(
          "window.cefQuery({request:JSON.stringify({method:'browser.restartToUpdate'}),persistent:false,onSuccess:()=>{},onFailure:()=>{}})",
          shell->second->GetMainFrame()->GetURL(),0);
    });
    Later(state, 700, [](const std::shared_ptr<State>& s) { if (s->sidebar_peek) s->sidebar_peek->Show(); });
    Later(state, 1700, [](const std::shared_ptr<State>& s) {
      Record(s, "peek_state_open");
      if (auto browser = s->sidebar_peek ? s->sidebar_peek->browser() : nullptr) {
        // Synthetic UI state only: this disposable non-installed probe must
        // never stage or apply a real package. The native ready state stays empty.
        auto snapshot=s->workspace->Snapshot();snapshot->SetString("updateReady","0.1.999");
        auto media=CefDictionaryValue::Create();media->SetString("id",snapshot->GetString("activeId"));
        media->SetString("title","Synthetic media");media->SetString("artist","Fixture");
        media->SetBool("playing",false);media->SetBool("video",false);media->SetBool("muted",false);
        media->SetDouble("time",0);media->SetDouble("duration",30);
        auto players=CefListValue::Create();players->SetDictionary(0,media);snapshot->SetList("media",players);
        s->sidebar_peek->Publish(snapshot);
        browser->GetMainFrame()->ExecuteJavaScript(kScript, browser->GetMainFrame()->GetURL(), 0);
      }
    });
    Later(state,1800,[](const std::shared_ptr<State>& s){
      if(auto browser=s->sidebar_peek?s->sidebar_peek->browser():nullptr){
        const auto capture=CaptureShell(browser,s->evidence_directory/"peek-update.png");
        CefPostDelayedTask(TID_UI,base::BindOnce([](CefRefPtr<CefRegistration>){},capture),1000);
      }
    });
    Later(state, 2600, [](const std::shared_ptr<State>& s) {
      auto data = CefDictionaryValue::Create(); data->SetString("url", s->workspace->Snapshot()->GetString("url").ToString().substr(0, 200));
      s->Record("peek_after_click", data);
      // Close the menu the script opened (it replies when the menu closes).
      if (s->sidebar_menu) s->sidebar_menu->Cancel();
      // A text field like the rename input, then a real right-click on it.
      if (auto browser = s->sidebar_peek ? s->sidebar_peek->browser() : nullptr) {
        browser->GetMainFrame()->ExecuteJavaScript(
            "{const i=document.createElement('input');i.value='rename me';"
            "i.style.cssText='position:fixed;left:20px;top:200px;width:150px;height:24px;z-index:9';"
            "i.addEventListener('click',e=>{if(e.isTrusted)console.info('RED_PEEK:'+JSON.stringify({nativeInputClick:true}));},{once:true});"
            "document.body.appendChild(i);i.focus();i.select();}", browser->GetMainFrame()->GetURL(), 0);
      }
    });
    // The editing-menu check at 2900 ms needs its original text selection.
    // A native click collapses that selection, so exercise it afterwards.
    Later(state, 3200, [](const std::shared_ptr<State>& s) {
      if (CefCommandLine::GetGlobalCommandLine()->GetSwitchValue("probe-sidebar-peek").empty()) return;
      auto browser = s->sidebar_peek ? s->sidebar_peek->browser() : nullptr;
      if (!browser || !s->sidebar_peek->window_visible()) return;
      const HWND widget = browser->GetHost()->GetWindowHandle();
      const float scale = s->window->GetDisplay()->GetDeviceScaleFactor();
      POINT point{static_cast<LONG>(95 * scale), static_cast<LONG>(212 * scale)};
      ClientToScreen(widget, &point);
      const auto hits_widget = [widget](POINT p) {
        const HWND hit = WindowFromPoint(p);
        return hit == widget || IsChild(widget, hit);
      };
      POINT old{}, current{};
      if (!hits_widget(point) || !GetCursorPos(&old) || !SetCursorPos(point.x, point.y)) return;
      if (GetCursorPos(&current) && current.x == point.x && current.y == point.y && hits_widget(current)) {
        INPUT click[2]{};
        click[0].type = click[1].type = INPUT_MOUSE;
        click[0].mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
        click[1].mi.dwFlags = MOUSEEVENTF_LEFTUP;
        SendInput(2, click, sizeof(INPUT));
      }
      CefPostDelayedTask(TID_UI, base::BindOnce([](POINT old, POINT point) {
        POINT cursor{};
        if (GetCursorPos(&cursor) && cursor.x == point.x && cursor.y == point.y) SetCursorPos(old.x, old.y);
      }, old, point), 75);
    });
    Later(state, 2900, [](const std::shared_ptr<State>& s) {
      auto browser = s->sidebar_peek ? s->sidebar_peek->browser() : nullptr;
      if (!browser) return;
      for (const char* type : {"mousePressed", "mouseReleased"}) {
        auto params = CefDictionaryValue::Create(); params->SetString("type", type);
        params->SetDouble("x", 95); params->SetDouble("y", 212); params->SetString("button", "right"); params->SetInt("clickCount", 1);
        browser->GetHost()->ExecuteDevToolsMethod(0, "Input.dispatchMouseEvent", params);
      }
    });
    Later(state, 3500, [](const std::shared_ptr<State>& s) { if (s->sidebar_peek) s->sidebar_peek->Hide(); });
    Later(state, 4300, [](const std::shared_ptr<State>& s) { Record(s, "peek_state_closed"); s->sidebar_peek_done = true; });
    // Both documents remain subscribed after docking. A browser-menu flow
    // must have exactly one active consumer, rather than toggling twice.
    Later(state,5000,[](const std::shared_ptr<State>& s){s->workspace->SetSidebarVisible(true);});
    Later(state,6000,[](const std::shared_ptr<State>& s){s->workspace->RequestSidebar("editSpace");});
    Later(state,7200,[](const std::shared_ptr<State>& s){
      auto data=CefDictionaryValue::Create();data->SetBool("docked",s->sidebar_visible);
      data->SetBool("editor_open",s->panel_popup && s->panel_popup->is_open());s->Record("sidebar_request_owner",data);
      if(s->panel_popup)s->panel_popup->Hide(false);
    });
    Later(state,8000,[](const std::shared_ptr<State>& s){
      const auto shell=s->browsers.find(s->shell_id);
      if(shell!=s->browsers.end())CheckXBadge(shell->second,false);
    });
    Later(state,9300,[](const std::shared_ptr<State>& s){ CheckPinEditor(s, false); });
    Later(state,11000,[](const std::shared_ptr<State>& s){s->workspace->SetSidebarVisible(false);s->sidebar_peek->Show();});
    Later(state,11600,[](const std::shared_ptr<State>& s){CheckXBadge(s->sidebar_peek->browser(),true);});
    Later(state,12400,[](const std::shared_ptr<State>& s){
      // X's counter reports nothing unread: the dot clears even though the X tab may be loaded
      // (activated earlier) with "(4)" still in its title.
      s->x_unread_fixture=0;
      if(const auto counter=s->workspace->x_unread())counter->RefreshSoon();
    });
    Later(state,13100,[](const std::shared_ptr<State>& s){ CheckPinEditor(s, true); });
    Later(state,14500,[](const std::shared_ptr<State>& s){s->sidebar_peek->Hide();s->workspace->SetSidebarVisible(true);});
    Later(state,15300,&CheckWhatsAppPin);
    // Leave time for an interactive runner to foreground this owned test window.
    Later(state,18000,&IntegrationProbe);
    // The Library checks follow the integration and pinned-hint chain (CheckFigmaCreate).
  }
}
}  // namespace red
