#include "experiments/menu_probe.h"
#include "include/cef_devtools_message_observer.h"
#include "include/cef_command_line.h"
#include <algorithm>
#include "include/base/cef_callback.h"
#include "include/wrapper/cef_closure_task.h"
#include "windows/main_menu.h"
#include "windows/page_menu.h"
#include "include/cef_id_mappers.h"
#include "windows/settings_window.h"
#include "windows/passwords_window.h"
#include "windows/background_host.h"
#include "windows/menu_popup.h"
#include "app/updates.h"
#include "downloads/download_manager.h"
#include "windows/page_capture.h"
#include "windows/capture_window.h"
#include "experiments/shell_capture.h"
#include "navigation/shortcuts.h"
#include "translation/page_translator.h"
#include <windows.h>
#include "app/session.h"
#include "workspace/controller.h"
#include "include/views/cef_display.h"
namespace red {
namespace {
// Finds the row and reports its centre; native input then moves to it (and clicks), as a person would.
std::string Row(const std::string& key, const char* action) {
  return std::string("(() => { const row = document.querySelector('[data-menu-key=\"") + key + "\"]');"
         " const r = row ? row.getBoundingClientRect() : { left: -1, top: -1, width: 0, height: 0 };"
         " window.cefQuery({ request: JSON.stringify({ method: 'menu.probe:" + action + ":' + Math.round(r.left + r.width / 2) + ':' + Math.round(r.top + r.height / 2) }),"
         " persistent: false, onSuccess() {}, onFailure() {} }); })()";
}
// Geometry acceptance uses the real frontend's click handler for warm
// levels, independent of CDP's cached hover state in a moved native widget.
std::string ActivateRow(const char* label) {
  return std::string("[...document.querySelectorAll('.menu-row')].find(e=>e.textContent.startsWith('")+label+"'))?.click()";
}
std::string ActivateKey(const std::string& key) {
  return "document.querySelector('[data-menu-key=\"" + key + "\"]')?.click()";
}
void Later(const std::shared_ptr<State>& state, int delay_ms, void (*step)(const std::shared_ptr<State>&)) {
  CefPostDelayedTask(TID_UI, base::BindOnce([](std::weak_ptr<State> weak, void (*step)(const std::shared_ptr<State>&)) {
    if (const auto state = weak.lock(); state && state->menu_popup) step(state);
  }, std::weak_ptr<State>(state), step), delay_ms);
}
std::string ClipboardText() {
  std::string text;
  if (!OpenClipboard(nullptr)) return text;
  if (const HANDLE handle = GetClipboardData(CF_UNICODETEXT)) {
    if (const auto* wide = static_cast<const wchar_t*>(GlobalLock(handle))) { text = CefString(std::wstring(wide)).ToString(); GlobalUnlock(handle); }
  }
  CloseClipboard();
  return text;
}
// Input on the page as a person gives it (CDP input is trusted, like the real thing).
// While the capture window is open it lies over the page and gets the input, as it would from a person.
CefRefPtr<CefBrowser> InputTarget(const std::shared_ptr<State>& s) {
  if (s->capture_window && s->capture_window->browser()) return s->capture_window->browser();
  return s->content;
}
void Mouse(const std::shared_ptr<State>& s, const char* type, double x, double y, bool down) {
  const auto target = InputTarget(s);
  if (!target) return;
  auto input = CefDictionaryValue::Create();
  input->SetString("type", type); input->SetDouble("x", x); input->SetDouble("y", y);
  input->SetString("button", down || std::string(type) == "mouseReleased" ? "left" : "none");
  input->SetInt("buttons", down ? 1 : 0); input->SetInt("clickCount", std::string(type) == "mouseMoved" ? 0 : 1);
  target->GetHost()->ExecuteDevToolsMethod(0, "Input.dispatchMouseEvent", input);
}
void Wheel(const std::shared_ptr<State>& s, double x, double y, double delta_y) {
  const auto target = InputTarget(s);
  if (!target) return;
  auto input = CefDictionaryValue::Create();
  input->SetString("type", "mouseWheel"); input->SetDouble("x", x); input->SetDouble("y", y);
  input->SetDouble("deltaX", 0); input->SetDouble("deltaY", delta_y);
  target->GetHost()->ExecuteDevToolsMethod(0, "Input.dispatchMouseEvent", input);
}
void KeyTo(CefRefPtr<CefBrowser> target, const char* key, const char* code, int vk, int modifiers = 0) {
  if (!target) return;
  for (const char* type : {"rawKeyDown", "keyUp"}) {
    auto input = CefDictionaryValue::Create();
    input->SetString("type", type); input->SetString("key", key); input->SetString("code", code);
    input->SetInt("windowsVirtualKeyCode", vk); input->SetInt("modifiers", modifiers);
    target->GetHost()->ExecuteDevToolsMethod(0, "Input.dispatchKeyEvent", input);
  }
}
void Key(const std::shared_ptr<State>& s, const char* key, const char* code, int vk, int modifiers) {
  KeyTo(InputTarget(s), key, code, vk, modifiers);
}
// A picture of the capture window as it is now (its own document, not the page under it).
void OverlayPicture(const std::shared_ptr<State>& s, const std::string& name) {
  static CefRefPtr<CefRegistration> capture;
  if (s->capture_window && s->capture_window->browser())
    capture = CaptureShell(s->capture_window->browser(), s->evidence_directory / (name + ".png"));
}
// What a copied picture looks like to other apps: "PNG" and CF_DIB (most apps paste only the bitmap).
void RecordClipboardPicture(const std::shared_ptr<State>& s) {
  auto data = CefDictionaryValue::Create();
  data->SetBool("png", IsClipboardFormatAvailable(RegisterClipboardFormatW(L"PNG")) != FALSE);
  data->SetBool("dib", IsClipboardFormatAvailable(CF_DIB) != FALSE);
  if (OpenClipboard(nullptr)) {
    if (const HANDLE handle = GetClipboardData(CF_DIB)) {
      if (const auto* header = static_cast<const BITMAPINFOHEADER*>(GlobalLock(handle))) {
        data->SetInt("width", header->biWidth); data->SetInt("height", header->biHeight); data->SetInt("bits", header->biBitCount);
        // Two pixels as the picture shows them (rows are stored bottom-up): 40 px down and 140 px down, 20 px in.
        if (header->biBitCount == 32 && header->biWidth > 20 && header->biHeight > 140) {
          const auto* pixels = reinterpret_cast<const unsigned char*>(header) + header->biSize;
          const auto at = [&](int x, int y) {
            const auto* p = pixels + (static_cast<size_t>(header->biHeight - 1 - y) * header->biWidth + x) * 4;
            return (p[2] << 16) | (p[1] << 8) | p[0];
          };
          data->SetInt("rgb_at_20_40", at(20, 40)); data->SetInt("rgb_at_20_140", at(20, 140));
        }
        GlobalUnlock(handle);
      }
    }
    CloseClipboard();
  }
  s->Record("capture_clipboard", data);
}
// The download list as the toolbar's Downloads button shows it: names and states.
void RecordDownloads(const std::shared_ptr<State>& s) {
  auto data = CefDictionaryValue::Create();
  auto names = CefListValue::Create();
  if (s->downloads) {
    const auto list = s->downloads->List();
    for (size_t i = 0; i < list->GetSize(); ++i)
      if (const auto row = list->GetDictionary(i)) names->SetString(i, row->GetString("name").ToString() + "|" + row->GetString("state").ToString());
    data->SetInt("started", s->downloads->started());
  }
  data->SetList("names", names);
  s->Record("downloads_list", data);
}
// Presses one of the capture window's top-right tiles (0: Save full page, 1: Save visible) as a click would.
void Tile(const std::shared_ptr<State>& s, int index) {
  if (s->capture_window && s->capture_window->browser())
    s->capture_window->browser()->GetMainFrame()->ExecuteJavaScript(
        "document.querySelectorAll('.capture-tile')[" + std::to_string(index) + "]?.click()", "", 0);
}
void RecordActive(const std::shared_ptr<State>& s, const char* step) {
  auto data = CefDictionaryValue::Create();
  data->SetString("step", step); data->SetString("active", s->workspace->Snapshot()->GetString("activeId"));
  s->Record("recent_probe", data);
}
// Polls the status every 250 ms: records each change, stops when the check ends or after two minutes.
void WatchUpdate(const std::shared_ptr<State>& state, int polls, int phase, int percent, bool restarted) {
  const auto status = CurrentUpdateStatus();
  const int now = static_cast<int>(status.phase);
  if (now != phase || status.percent != percent) {
    auto details = CefDictionaryValue::Create();
    details->SetInt("phase", now); details->SetInt("percent", status.percent); details->SetString("version", status.version);
    details->SetBool("window", state->update_id.load() > 0);
    state->Record("update_probe_status", details);
  }
  const bool ended = status.phase == UpdatePhase::Ready || status.phase == UpdatePhase::UpToDate ||
                     status.phase == UpdatePhase::Failed || status.phase == UpdatePhase::NotInstalled;
  if (ended && state->menu_update_restart && status.phase == UpdatePhase::Ready && !restarted) {
    state->Record("update_probe_restart");
    RequestUpdateRestart(state);  // The window's "Restart to update" does exactly this.
    restarted = true;
  }
  if ((ended && !(state->menu_update_restart && status.phase == UpdatePhase::Ready)) || polls > 480) {
    auto data = CefDictionaryValue::Create();
    data->SetBool("closed", !state->menu_popup->is_open()); data->SetBool("window", state->update_id.load() > 0);
    state->Record("menu_probe_result", data); state->menu_done = true;
    return;
  }
  CefPostDelayedTask(TID_UI, base::BindOnce([](std::weak_ptr<State> weak, int polls, int phase, int percent, bool restarted) {
    if (const auto owner = weak.lock()) WatchUpdate(owner, polls + 1, phase, percent, restarted);
  }, std::weak_ptr<State>(state), polls + 1, now, status.percent, restarted), 250);
}
}  // namespace
void OnMenuProbeLoad(const std::shared_ptr<State>& state, const std::string& url) {
  if (!state->menu_probe || state->menu_probe_started || !url.starts_with("http")) return;
  state->menu_probe_started = true;
  const auto mode = CefCommandLine::GetGlobalCommandLine()->GetSwitchValue("probe-menu").ToString();
  if (mode == "translate-offer") return;  // The trusted shell drives the offer scenario.
  if (mode == "library") {
    state->menu_popup = std::make_shared<MenuPopup>(state);  // Later() requires the probe owner.
    Later(state, 1200, [](const std::shared_ptr<State>& s) {
      const auto shell = s->browsers.find(s->shell_id);
      if (shell == s->browsers.end() || !s->content) return;
      const auto page = s->content->GetMainFrame();
      page->ExecuteJavaScript("document.getElementById('library-fixture').value='unsaved';", page->GetURL(), 0);
      const auto frame = shell->second->GetMainFrame();
      frame->ExecuteJavaScript(R"JS((async()=>{
        const get=(selector,root=document)=>root.querySelector(selector);
        const pause=()=>new Promise(resolve=>setTimeout(resolve,30));
        const wait=async(fn)=>{for(let i=0;i<160;++i){const item=fn();if(item)return item;await pause();}throw Error('Library DOM wait timed out');};
        const q=request=>new Promise((resolve,reject)=>cefQuery({request:JSON.stringify(request),persistent:false,
          onSuccess:raw=>resolve(JSON.parse(raw)),onFailure:(code,message)=>reject(Error(code+': '+message))}));
        const snapshot=()=>q({method:'browser.getSnapshot'});
        const mutation=async request=>q({...request,expectedRevision:(await snapshot()).treeRevision});
        const input=(element,text)=>{Object.getOwnPropertyDescriptor(HTMLInputElement.prototype,'value').set.call(element,text);
          element.dispatchEvent(new Event('input',{bubbles:true}));};
        const preview=async name=>{
          await new Promise(resolve=>setTimeout(resolve,230));
          console.info('RED_LIBRARY_PREVIEW:'+name);
          await new Promise(resolve=>setTimeout(resolve,350));
        };
        try {
          // A loud page card: the native frame probe finds its edge on the screen
          // next to the page's (library_rail_frame), until the previews. CSSOM,
          // since the shell's CSP refuses an inline <style>.
          get('.web-slot').style.setProperty('background','#00d46a','important');
          await wait(()=>get('[data-action=library]'));
          const before=await snapshot();
          await q({method:'library.getRoutes'}).then(()=>{throw Error('Closed Library exposed routing');},error=>{if(!error.message.startsWith('409'))throw error;});
          // The shell's side of a glide, in ms after the command: when the page card's
          // animation started and where the card is on each main-thread frame (gaps are
          // the Library's render holding this thread) (library_shell_glide).
          const trace=(label,command)=>new Promise(resolve=>{
            const slot=get('.web-slot'), t0=performance.now(), samples=[], events={}, busy=[];
            const observer=new PerformanceObserver(list=>{for(const e of list.getEntries())busy.push({at:Math.round(e.startTime-t0),ms:Math.round(e.duration),
              block:Math.round(e.blockingDuration??0),render:Math.round(e.renderStart?e.renderStart-t0:-1),style:Math.round(e.styleAndLayoutStart?e.styleAndLayoutStart-t0:-1),
              scripts:(e.scripts??[]).map(x=>[x.invoker,Math.round(x.duration),x.sourceFunctionName].join('|')).slice(0,6)});});
            observer.observe({type:'long-animation-frame',buffered:false});
            const step=now=>{
              const started=slot.getAnimations().find(a=>typeof a.startTime==='number');
              if(started&&events.start===undefined)events.start=Math.round(started.startTime-t0);
              samples.push([Math.round(now-t0),Math.round(slot.getBoundingClientRect().left)]);
              if(now-t0<640)requestAnimationFrame(step);else{observer.disconnect();console.info('RED_LIBRARY_GLIDE:'+JSON.stringify({label,events,busy,samples}));resolve();}};
            requestAnimationFrame(step); command();
          });
          const open=async ()=>(await wait(()=>get('[data-action=library]'))).click();
          const close=()=>get('[data-action=close-library]').click();
          await trace('open',()=>get('[data-action=library]').click()); await wait(()=>get('.library'));
          await trace('close',close); await wait(()=>!get('.library'));
          // Mid-glide, this thread is held for 120 ms: the card (a compositor
          // animation) must keep pace with the page regardless.
          await trace('reopen',()=>{get('[data-action=library]').click();setTimeout(()=>{const end=performance.now()+120;while(performance.now()<end);},90);}); await wait(()=>get('.library'));
          const expanded=await snapshot();
          if(!expanded.libraryOpen||expanded.contentBounds.x<=before.contentBounds.x||expanded.activeId!==before.activeId)throw Error('Open geometry');
          console.info('RED_LIBRARY_RESIZE');
          await wait(()=>get('.library').getBoundingClientRect().width!==expanded.contentBounds.x-8);
          const resized=await snapshot();
          // The Library's width eases to the new layout (library.css, 420 ms).
          await wait(()=>Math.abs(get('.library').getBoundingClientRect().right-(resized.contentBounds.x-8))<=1);
          if(resized.activeId!==before.activeId||resized.contentBounds.width<320||
              Math.abs(get('.library').getBoundingClientRect().right-(resized.contentBounds.x-8))>1)throw Error('Resize alignment');
          close(); await wait(()=>!get('.library'));
          const restored=await snapshot();
          if(restored.contentBounds.x!==before.contentBounds.x||restored.activeId!==before.activeId)throw Error('Close geometry');
          await new Promise(resolve=>setTimeout(resolve,560));  // The page edge glides (420 ms).
          console.info('RED_LIBRARY_LAYOUT_DONE');
          await new Promise(resolve=>setTimeout(resolve,250));
          await open(); await wait(()=>get('[data-library-section=spaces]'));
          const themed=getComputedStyle(get('.library-rail'));
          const background=themed.backgroundColor;
          if(background==='rgba(0, 0, 0, 0)'||!themed.getPropertyValue('--lib-line').trim())throw Error('Missing Library palette');
          const theme=document.documentElement.dataset.theme;
          document.documentElement.dataset.theme=theme==='dark'?'light':'dark';
          if(getComputedStyle(get('.library-rail')).backgroundColor===background)throw Error('Library palettes are identical');
          document.documentElement.dataset.theme=theme;
          console.info('RED_LIBRARY_BOARD_SIZE');
          await wait(()=>window.innerWidth>=1000);
          // Two Spaces: the board fits its columns, so one Space is no wider than the lists.
          const created=await mutation({method:'space.create',title:'Library Work',icon:''});
          const work=created.spaces.find(space=>space.name==='Library Work');
          const narrow=await snapshot();
          get('[data-library-section=spaces]').click(); await wait(()=>get('.library-spaces'));
          // The shell's page card (an empty tab draws only this) glides with the native page.
          if(!matchMedia('(prefers-reduced-motion: reduce)').matches&&!get('.web-slot').getAnimations().some(a=>a.playState==='running'))
            throw Error('Page card does not glide');
          const wide=await snapshot();
          if(wide.librarySection!=='spaces'||wide.contentBounds.x<=narrow.contentBounds.x||
              wide.activeId!==narrow.activeId||wide.contentBounds.width<320)throw Error('Spaces board geometry');
          const animation=getComputedStyle(get('.library-content')).animationName;
          if(!matchMedia('(prefers-reduced-motion: reduce)').matches&&animation==='none')throw Error('No section transition');
          const card=()=>get(`[data-library-space="${work.id}"]`);
          await wait(card);
          get('[data-action=space-menu]',card()).click();  // The Space's own native menu: Rename.
          for(let tries=0;;++tries){try{await q({method:'probe.contextMenuSelect',action:'rename'});break;}
            catch(error){if(tries>40)throw error;await new Promise(resolve=>setTimeout(resolve,100));}}
          input(await wait(()=>get('.library-rename input')),'Renamed Work'); await pause();
          get('.library-rename [type=submit]').click();
          await wait(()=>get('.library-space-name',card()).textContent.includes('Renamed Work'));
          get('[data-action=move-space]',card()).dispatchEvent(new KeyboardEvent('keydown',{key:'ArrowLeft',bubbles:true,cancelable:true}));
          await wait(()=>get('.library-space')===card());
          {  // Drag the column by its handle past the last one.
            const frame=()=>new Promise(resolve=>requestAnimationFrame(()=>requestAnimationFrame(resolve)));
            const handover=new DataTransfer(), board=get('.library-spaces');
            get('[data-action=move-space]',card()).dispatchEvent(new DragEvent('dragstart',{bubbles:true,cancelable:true,dataTransfer:handover}));
            await frame();
            const last=[...document.querySelectorAll('.library-space')].at(-1).getBoundingClientRect();
            const at={bubbles:true,cancelable:true,dataTransfer:handover,clientX:Math.round(last.right-4),clientY:Math.round(last.top+40)};
            board.dispatchEvent(new DragEvent('dragover',at)); await frame();
            board.dispatchEvent(new DragEvent('drop',at));
            await wait(()=>[...document.querySelectorAll('.library-space')].at(-1)===card());
          }
          const transfer=new DataTransfer();
          const source=await wait(()=>get(`[data-library-tab="${before.activeId}"] button`));
          source.dispatchEvent(new DragEvent('dragstart',{bubbles:true,cancelable:true,dataTransfer:transfer}));
          card().dispatchEvent(new DragEvent('drop',{bubbles:true,cancelable:true,dataTransfer:transfer}));
          await wait(()=>get(`[data-library-tab="${before.activeId}"]`,card()));
          const nodeRow=id=>get(`[data-library-node="${id}"]`);
          const drag=async (source,destination,ratio,marker)=>{
            const payload=new DataTransfer();
            source.dispatchEvent(new DragEvent('dragstart',{bubbles:true,cancelable:true,dataTransfer:payload}));
            await wait(()=>get('.library-board').dataset.dragging==='true');
            const box=destination.getBoundingClientRect();
            const args={bubbles:true,cancelable:true,dataTransfer:payload,
              clientX:Math.round(box.left+box.width/2),clientY:Math.round(box.top+box.height*ratio)};
            destination.dispatchEvent(new DragEvent('dragover',args));
            const hint=()=>destination.closest('.library-tree-entry')?.dataset.entryDrop;
            if(marker) {
              await wait(()=>hint()===marker);
              const entry=destination.closest('.library-tree-entry');
              if(marker==='inside' ? getComputedStyle(destination).boxShadow==='none' :
                  getComputedStyle(entry,'::after').height!=='2px')throw Error('Missing drop indicator');
            } else {
              await pause();
              if(hint()) throw Error('Invalid drop advertised');
            }
            destination.dispatchEvent(new DragEvent('drop',args));
            source.dispatchEvent(new DragEvent('dragend',args));
          };
          await mutation({method:'space.switch',id:work.id});
          const target=(await mutation({method:'tree.createFolder',title:'Drop destination',parentId:''})).createdId;
          const existing=(await mutation({method:'tree.createFolder',title:'Existing child',parentId:target})).createdId;
          await mutation({method:'space.switch',id:before.activeSpace});
          const sourceFolder=(await mutation({method:'tree.createFolder',title:'Folder to move',parentId:''})).createdId;
          const nested=(await mutation({method:'tree.createFolder',title:'Nested child',parentId:sourceFolder})).createdId;
          await wait(()=>nodeRow(sourceFolder) && nodeRow(target));
          await drag(nodeRow(sourceFolder),nodeRow(target),.5,'inside');
          await wait(()=>nodeRow(sourceFolder)?.closest('details')?.parentElement?.closest('details')===nodeRow(target)?.parentElement);
          const placed=await snapshot();
          if(placed.nodes.find(n=>n.id===nested)?.parentId!==sourceFolder)throw Error('Nested folder did not move');
          await drag(nodeRow(sourceFolder),await wait(()=>nodeRow(existing)?.getClientRects().length && nodeRow(existing)),.06,'before');
          const entry=id=>nodeRow(id)?.closest('.library-tree-entry');
          await wait(()=>entry(sourceFolder)?.nextElementSibling===entry(existing));
          await drag(nodeRow(sourceFolder),nodeRow(existing),.95,'after');
          await wait(()=>entry(sourceFolder)?.previousElementSibling===entry(existing));
          const after=await snapshot();
          if(after.nodes.find(n=>n.id===sourceFolder)?.position<=after.nodes.find(n=>n.id===existing)?.position)
            throw Error('Folder was not inserted after the indicated sibling');
          await wait(()=>nodeRow(nested)?.getClientRects().length);
          await drag(nodeRow(sourceFolder),nodeRow(nested),.5);
          if((await snapshot()).treeRevision!==after.treeRevision)throw Error('A cyclic folder drop changed the tree');
          await mutation({method:'space.switch',id:work.id});
          get('.web-slot').style.removeProperty('background');
          await preview('spaces');
          const url=new URL('?archive=library-keyword', before.url).href;
          const archived=await mutation({method:'tree.createTab',parentId:'',title:'Library Archive Fixture',url});
          await mutation({method:'tree.archive',id:archived.createdId});
          get('[data-library-section=archive]').click();
          const archiveRow=()=>get(`[data-library-restore="${archived.createdId}"]`);
          await wait(archiveRow);
          const list=get('.library-archive .library-scroll');
          if(!get('.library-day h2')||list.scrollWidth>list.clientWidth+1||(await snapshot()).contentBounds.x!==narrow.contentBounds.x)
            throw Error('Archive groups or narrow geometry');
          await preview('archive');
          const search=get('.library input[type=search]');
          input(search,'missing-xyz'); await wait(()=>!archiveRow());
          input(search,'library-keyword'); await wait(archiveRow);
          archiveRow().click(); await wait(()=>!archiveRow());
          const selected=await snapshot();
          if(selected.activeId!==archived.createdId||selected.activeSpace!==work.id)throw Error('Archive restore');
          get('[data-library-section=routes]').click();
          (await wait(()=>get('[data-action=new-library-route]:enabled'))).click();
          const editor=await wait(()=>get('.library-route-editor'));
          const regex=get('.library-match-types button:last-child',editor);
          regex.click();
          await wait(()=>regex.getAttribute('aria-pressed')==='true');
          const contains=get('.library-match-types button:first-child',editor);
          contains.click();
          await wait(()=>contains.getAttribute('aria-pressed')==='true');
          input(get('input',editor),'example.test/team/'); await pause();
          await preview('editor');
          const saved=await q({method:'library.getRoutes'});
          get('.library-route-editor [type=submit]').click();
          await wait(()=>!get('.library-route-editor')&&get('.library-route-row'));
          const stored=await q({method:'library.getRoutes'});
          if(stored.revision!==saved.revision+1||stored.routing.rules[0].match!=='contains'||
              stored.routing.rules[0].pattern!=='example.test/team/')throw Error('Settings persistence');
          const routeSearch=get('.library-routes input[type=search]');
          input(routeSearch,'no matching rule'); await wait(()=>!get('.library-route-row'));
          input(routeSearch,'example.test'); await wait(()=>get('.library-route-row'));
          await preview('routes');
          await q({method:'library.saveRoutes',expectedRevision:saved.revision,routing:saved.routing}).then(
            ()=>{throw Error('Stale save accepted');},error=>{if(!error.message.startsWith('409'))throw error;});
          // Library's + closes it and turns the docked sidebar into the Create Space form.
          get('[data-library-section=spaces]').click();
          (await wait(()=>get('.library-new-space:enabled'))).click();
          const form=await wait(()=>!get('.library')&&get('.space-creator input'));
          input(form,'Made in form'); await pause();
          (await wait(()=>get('.space-creator-submit:enabled'))).click();
          // Started from Library: Create returns there, with the new Space on screen.
          await wait(()=>!get('.space-creator')&&get('.library'));
          const made=await snapshot(), madeSpace=made.spaces.find(space=>space.name==='Made in form');
          if(!madeSpace||made.activeSpace!==madeSpace.id||!made.libraryOpen)throw Error('Create Space form');
          // Back to a Space with the fixture page, so the final layout check has a page on screen.
          await mutation({method:'space.delete',id:madeSpace.id}); await pause();
          close(); await wait(()=>!get('.library'));
          console.info('RED_LIBRARY_RESULT:pass');
        } catch(error){
          console.info('RED_LIBRARY_RESULT:'+error+' ['+document.body.innerText.trim().slice(0,180)+']');
        }
      })())JS", frame->GetURL(), 0);
    });
    return;
  }
  if (mode == "page-passwords") {
    // Menu > Passwords > Passwords for This Page on a page with no saved
    // passwords (scripts/page_passwords_smoke.py): Sol's password manager
    // opens searched for the page's host, and the browser keeps running.
    CefPostDelayedTask(TID_UI, base::BindOnce([](std::weak_ptr<State> weak) {
      if (const auto owner = weak.lock()) ShowMainMenu(owner, kMainMenuAnchor);
    }, std::weak_ptr<State>(state)), 1200);
    Later(state, 2200, [](const std::shared_ptr<State>& s) { s->menu_popup->ProbeRun(0, ActivateKey("passwords")); });
    Later(state, 3200, [](const std::shared_ptr<State>& s) { s->menu_popup->ProbeRun(1, ActivateKey("pagePasswords")); });
    Later(state, 8000, [](const std::shared_ptr<State>& s) {
      auto data = CefDictionaryValue::Create(); std::string url;
      for (const auto& [profile, window] : s->password_windows)
        if (window && window->browser()) url = window->browser()->GetMainFrame()->GetURL().ToString();
      data->SetString("url", url);
      s->Record("page_passwords_result", data);
      for (const auto& [profile, window] : s->password_windows) if (window) window->Close();
      s->menu_done = true;
    });
    return;
  }
  if (mode == "settings-picker") {
    state->menu_popup = std::make_shared<MenuPopup>(state);
    Later(state, 700, [](const std::shared_ptr<State>& s) { ShowSettings(s); });
    Later(state, 2200, [](const std::shared_ptr<State>& s) {
      const auto window = s->settings_window ? s->settings_window->window() : nullptr;
      if (!window) return;
      const auto bounds = window->GetBounds();
      window->SetBounds(CefRect(bounds.x, bounds.y, 720, 520));
      s->settings_window->ShowSection("languages");
    });
    Later(state, 2550, [](const std::shared_ptr<State>& s) {
      const auto browser = s->settings_window ? s->settings_window->browser() : nullptr;
      if (!browser) return;
      const auto frame = browser->GetMainFrame();
      frame->ExecuteJavaScript("document.querySelector('#translation-target')?.focus()", frame->GetURL(), 0);
    });
    Later(state, 2800, [](const std::shared_ptr<State>& s) {
      if (s->settings_window) KeyTo(s->settings_window->browser(), "ArrowDown", "ArrowDown", VK_DOWN, 1);
    });
    Later(state, 3100, [](const std::shared_ptr<State>& s) {
      const auto browser = s->settings_window ? s->settings_window->browser() : nullptr;
      if (!browser) return;
      static CefRefPtr<CefRegistration> capture;
      capture = CaptureShell(browser, s->evidence_directory / "settings-picker.png");
      const auto frame = browser->GetMainFrame();
      frame->ExecuteJavaScript(R"JS((() => {
        const select = document.querySelector('#translation-target');
        const picker = getComputedStyle(select, '::picker(select)');
        const first = select.options[0].getBoundingClientRect();
        console.info('RED_SETTINGS_PICKER:' + JSON.stringify({
          open: select.matches(':open'), custom: getComputedStyle(select).appearance === 'base-select',
          graphite: picker.backgroundImage.includes('rgb(46, 48, 55)'),
          visible: first.top >= 0 && first.bottom <= innerHeight && first.height > 0,
          width: innerWidth, options: select.options.length, value: select.value
        }));
      })())JS", frame->GetURL(), 0);
    });
    Later(state, 3500, [](const std::shared_ptr<State>& s) {
      if (s->settings_window) KeyTo(s->settings_window->browser(), "Escape", "Escape", VK_ESCAPE);
    });
    Later(state, 3700, [](const std::shared_ptr<State>& s) {
      const auto browser = s->settings_window ? s->settings_window->browser() : nullptr;
      if (!browser) return;
      const auto frame = browser->GetMainFrame();
      frame->ExecuteJavaScript(R"JS((() => {
        const select = document.querySelector('#translation-target');
        console.info('RED_PICKER_ESCAPE:' + JSON.stringify({ closed: !select.matches(':open'),
          focus: document.activeElement === select, value: select.value }));
      })())JS", frame->GetURL(), 0);
    });
    Later(state, 3900, [](const std::shared_ptr<State>& s) {
      if (s->settings_window) KeyTo(s->settings_window->browser(), "ArrowDown", "ArrowDown", VK_DOWN, 1);
    });
    Later(state, 4200, [](const std::shared_ptr<State>& s) {
      const auto browser = s->settings_window ? s->settings_window->browser() : nullptr;
      KeyTo(browser, "ArrowDown", "ArrowDown", VK_DOWN);
      KeyTo(browser, "Enter", "Enter", VK_RETURN);
    });
    Later(state, 4800, [](const std::shared_ptr<State>& s) {
      auto data = CefDictionaryValue::Create();
      data->SetString("target", s->session->preferences->Get().translation_target);
      s->Record("settings_picker_saved", data);
      if (s->settings_window) s->settings_window->Close();
      s->menu_done = true;
    });
    return;
  }
  if (mode == "little-sol-hotkey" && state->session && state->session->preferences) {
    state->menu_popup = std::make_shared<MenuPopup>(state);
    auto& session = *state->session;
    session.background = std::make_shared<BackgroundHost>(BackgroundHost::Actions{}, session.interface_locale,
                                                         session.preferences->Get().little_sol_binding);
    session.background->SetEnabled(true);
    auto data = CefDictionaryValue::Create();
    data->SetBool("registered", session.background->hotkey_registered());
    state->Record("little_sol_hotkey_before", data);
    Later(state, 800, [](const std::shared_ptr<State>& s) { ShowSettings(s); });
    Later(state, 2800, [](const std::shared_ptr<State>& s) {
      const auto browser = s->settings_window ? s->settings_window->browser() : nullptr;
      if (!browser) return;
      const auto frame = browser->GetMainFrame();
      frame->ExecuteJavaScript(R"JS((async () => {
        const query = request => new Promise((resolve, reject) => cefQuery({
          request: JSON.stringify(request), persistent: false,
          onSuccess: raw => resolve(JSON.parse(raw)), onFailure: reject
        }));
        const save = key => query({ method: 'settings.shortcut', action: 'littleSolGlobal',
          binding: { key, ctrl: true, alt: true, shift: false } });
        let conflict = 0;
        try { await save(131); } catch (code) { conflict = code; }
        const afterConflict = (await query({ method: 'settings.get' })).preferences.littleSolBinding?.key;
        document.querySelector('[data-section="keyboard"]').click();
        const pause = () => new Promise(resolve => setTimeout(resolve, 30));
        await pause();
        const row = document.querySelector('.settings-shortcut-row');
        row.querySelector('.settings-shortcut-key').click();
        await pause();
        const editor = document.querySelector('.settings-shortcut-editor');
        const key = editor.querySelectorAll('select')[1];
        key.value = '132';
        key.dispatchEvent(new Event('change', { bubbles: true }));
        await pause();
        editor.requestSubmit();
        for (let i = 0; i < 45 && !document.querySelector('.settings-shortcut-key')?.textContent.includes('F21'); i++) await pause();
        const configured = (await query({ method: 'settings.get' })).preferences.littleSolBinding?.key;
        const label = document.querySelector('.settings-shortcut-key')?.textContent;
        console.info('RED_LITTLE_SOL_HOTKEY:' + JSON.stringify({ conflict, afterConflict, configured, label }));
      })())JS", frame->GetURL(), 0);
    });
    Later(state, 6500, [](const std::shared_ptr<State>& s) {
      constexpr int check_id = 0x534F;
      const bool old_free = RegisterLittleSolHotkey(nullptr, check_id, {VK_F22, true, false, true});
      if (old_free) UnregisterHotKey(nullptr, check_id);
      const bool new_free = RegisterLittleSolHotkey(nullptr, check_id, {VK_F21, true, false, true});
      if (new_free) UnregisterHotKey(nullptr, check_id);
      auto result = CefDictionaryValue::Create();
      result->SetBool("old_free", old_free);
      result->SetBool("new_registered", !new_free && s->session->background->hotkey_registered());
      s->Record("little_sol_hotkey_after", result);
      s->session->background.reset();
      if (s->settings_window) s->settings_window->Close();
      s->menu_done = true;
    });
    return;
  }
  if (mode == "language-save" || mode == "language-read") {
    CefPostDelayedTask(TID_UI, base::BindOnce([](std::weak_ptr<State> weak) {
      if (const auto owner = weak.lock()) ShowMainMenu(owner, kMainMenuAnchor);
    }, std::weak_ptr<State>(state)), 1200);
    Later(state, 3500, [](const std::shared_ptr<State>& s) {
      s->menu_popup->ProbeRun(0, R"JS(
        const row = document.querySelector('[data-menu-key="settings"]');
        if (row?.querySelector('.menu-label')?.textContent ===
            (document.documentElement.lang === 'uk' ? 'Налаштування' : 'Settings')) row.click()
      )JS");
    });
    Later(state, 5300, [](const std::shared_ptr<State>& s) {
      const auto window = s->settings_window ? s->settings_window->window() : nullptr;
      if (!window) return;
      const auto bounds = window->GetBounds();
      window->SetBounds(CefRect(bounds.x, bounds.y, 720, 520));
      auto data = CefDictionaryValue::Create();
      data->SetString("title", window->GetTitle());
      s->Record("language_window", data);
    });
    Later(state, 5550, [](const std::shared_ptr<State>& s) {
      const auto browser = s->settings_window ? s->settings_window->browser() : nullptr;
      if (!browser) return;
      const auto frame = browser->GetMainFrame();
      frame->ExecuteJavaScript("document.querySelector('[data-subsection=links]')?.click()", frame->GetURL(), 0);
    });
    Later(state, 5900, [](const std::shared_ptr<State>& s) {
      static CefRefPtr<CefRegistration> capture;
      if (const auto browser = s->settings_window ? s->settings_window->browser() : nullptr)
        capture = CaptureShell(browser, s->evidence_directory / "settings-little-sol.png");
    });
    Later(state, 6300, [](const std::shared_ptr<State>& s) {
      if (!s->settings_window) return;
      const auto browser = s->settings_window->browser();
      if (!browser) return;
      const bool save = CefCommandLine::GetGlobalCommandLine()->GetSwitchValue("probe-menu") == "language-save";
      const auto script = std::string("(async(save)=>{") + R"JS(
        const go = async selector => {
          document.querySelector(selector).click();
          await new Promise(resolve => requestAnimationFrame(() => requestAnimationFrame(resolve)));
        };
        const query = request => new Promise((resolve, reject) => cefQuery({
          request: JSON.stringify(request), persistent: false,
          onSuccess: raw => resolve(JSON.parse(raw)), onFailure: reject
        }));
        const hasOverflow = () => [...document.querySelectorAll('.settings-nav, .settings-content, .settings-card, .settings-nav button, .settings-subnav, .settings-subnav button, .settings-card-actions button, .settings-inline-card button')]
          .some(element => element.scrollWidth > element.clientWidth + 1);
        const sidebar = [...document.querySelectorAll('.settings-nav [data-section]')].map(row => row.dataset.section);
        const browserGroup = document.querySelector('.settings-nav [aria-current="page"]')?.dataset.section === 'general' &&
          [...document.querySelectorAll('[data-subsection]')].map(row => row.dataset.subsection).join(',') === 'general,startup,downloads,links';
        const little = document.querySelector('#little-sol-heading')?.closest('section');
        const rows = [...little.querySelectorAll('.settings-option-row')];
        const littleRows = rows.length === 3 && rows.every((row, i) => !!row.querySelector('[role="switch"]') &&
          (i === 1 ? !row.querySelector('p') : !!row.querySelector('p'))) && !hasOverflow();
        await go('#little-sol-heading + button');
        const shortcutLink = !!document.querySelector('.settings-shortcut-row');
        await go('.settings-nav [data-section=profiles]');
        await go('[data-subsection=passwords]');
        const passwords = !!document.querySelector('#password-profile') &&
          document.querySelector('.settings-nav [aria-current="page"]')?.dataset.section === 'profiles';
        await go('[data-subsection=import]');
        const imports = !!document.querySelector('#import-profile');
        await go('.settings-nav [data-section=integrations]');
        const integrations = !!document.querySelector('#notes-provider') && !!document.querySelector('#integration-services');
        await go('.settings-nav [data-section=appearance]');
        const appearance = !!document.querySelector('#clock-format') && !!document.querySelector('#window-backdrop');
        await go('.settings-nav [data-section=releases]');
        const about = !!document.querySelector('[data-action=release-notes]');
        await go('.settings-nav [data-section=languages]');
        const select = document.querySelector('#interface-language');
        const languages = !!select && !!document.querySelector('#translation-target');
        console.info('RED_SETTINGS_GROUPS:' + JSON.stringify({ sidebar, browserGroup, littleRows, shortcutLink,
          passwords, imports, integrations, appearance, about, languages }));
        let invalid = 0;
        const before = select?.value;
        if (save && select && !select.disabled) {
          try { await query({ method: 'settings.set', key: 'interfaceLanguage', value: '<invalid>' }); }
          catch (code) { invalid = code; }
          select.value = 'uk';
          select.dispatchEvent(new Event('change', { bubbles: true }));
          for (let i = 0; i < 50 && (select.disabled || select.value !== 'uk'); i++)
            await new Promise(resolve => setTimeout(resolve, 20));
        }
        const data = await query({ method: 'settings.get' });
        const overflow = hasOverflow();
        const heading = document.querySelector('#interface-language-heading')?.textContent;
        for (const element of document.querySelectorAll('.settings-nav button span, .settings-card h2, .settings-card p, .settings-card-actions button, .settings-inline-card button'))
          for (const child of [...element.childNodes])
            if (child.nodeType === Node.TEXT_NODE && child.textContent.trim()) child.textContent += ' ' + child.textContent;
        const expandedOverflow = hasOverflow();
        document.querySelector('[data-section="search"]').click();
        await new Promise(resolve => requestAnimationFrame(() => requestAnimationFrame(resolve)));
        const search = document.querySelector('.settings-page h2')?.textContent;
        const searchOverflow = hasOverflow();
        document.querySelector('[data-section="keyboard"]').click();
        await new Promise(resolve => requestAnimationFrame(() => requestAnimationFrame(resolve)));
        const shortcuts = !!document.querySelector('.settings-shortcut-row');
        console.info('RED_LANGUAGE:' + JSON.stringify({
          lang: document.documentElement.lang, before, value: data.preferences.interfaceLanguage,
          heading, width: innerWidth, overflow, expandedOverflow, invalid, search, searchOverflow, shortcuts
        }));
      })()JS" + (save ? "true)" : "false)");
      browser->GetMainFrame()->ExecuteJavaScript(script, browser->GetMainFrame()->GetURL(), 0);
    });
    Later(state, 10300, [](const std::shared_ptr<State>& s) {
      if (s->settings_window) s->settings_window->Close();
      s->menu_done = true;
    });
    return;
  }
  if(state->menu_edges) {
    for(int i=0;i<4;++i) CefPostDelayedTask(TID_UI,base::BindOnce([](std::weak_ptr<State> weak,int corner) {
      const auto s=weak.lock();if(!s || !s->window)return;
      if(!s->menu_popup)s->menu_popup=std::make_shared<MenuPopup>(s);
      const auto work=s->window->GetDisplay()->GetWorkArea();
      MenuPopup::Builder menu;
      menu.Add("First","copy",[] {});
      menu.Add("Шукати «Довгий виділений текст зі сторінки, який може займати кілька рядків "
               "і має залишатися всередині контекстного меню навіть біля краю екрана» у Google",
               "search", [] {}, "Ctrl+Shift+F", true, false, "", "wideRow");
      auto& sub=menu.Submenu("Branch","folder");
      sub.Add("Child","copy",[] {});
      sub.Submenu("Nested","folder").Add("Pick","copy",[] {});
      menu.Add("Last","copy",[] {});
      s->menu_popup->Open(menu,CefPoint(work.x+(corner%2 ? work.width-1 : 1),work.y+(corner/2 ? work.height-1 : 1)),"edge:"+std::to_string(corner));
      Later(s,900,[](const std::shared_ptr<State>& s) {
        s->menu_popup->ProbeRun(0, R"JS((() => {
          const menu = document.querySelector('.menu');
          const row = menu?.querySelector('[data-menu-key="wideRow"]');
          const label = row?.querySelector('.menu-label');
          const shortcut = row?.querySelector('.menu-shortcut');
          const inside = (element, container) => {
            const a = element.getBoundingClientRect(), b = container.getBoundingClientRect();
            return a.width > 0 && a.left >= b.left - 1 && a.right <= b.right + 1;
          };
          const fits = menu && row && label && shortcut && menu.getBoundingClientRect().right <= innerWidth + 1 &&
            inside(row, menu) && inside(label, row) && inside(shortcut, row) && shortcut.scrollWidth <= shortcut.clientWidth + 1 &&
            label.scrollWidth > label.clientWidth && getComputedStyle(label).textOverflow === 'ellipsis';
          window.cefQuery({ request: JSON.stringify({ method: fits ? 'menu.probe:long-row-fits' : 'menu.probe:long-row-overflows' }),
            persistent: false, onSuccess() {}, onFailure() {} });
        })())JS");
      });
      Later(s,1300,[](const std::shared_ptr<State>& s) {s->menu_popup->ProbeRun(0,ActivateRow("Branch"));});
      Later(s,2200,[](const std::shared_ptr<State>& s) {s->menu_popup->ProbeRun(1,ActivateRow("Nested"));});
      Later(s,3000,[](const std::shared_ptr<State>& s) {s->menu_popup->ProbeRun(2,ActivateRow("Pick"));});
    },std::weak_ptr<State>(state),i),1200+i*4000);
    Later(state,17500,[](const std::shared_ptr<State>& s) {
      auto d=CefDictionaryValue::Create();d->SetBool("closed",!s->menu_popup->is_open());s->Record("menu_probe_result",d);s->menu_done=true;
    });
    return;
  }
  if (state->menu_translate) {
    // Sol's on-device page translation from its page menu (scripts/translate_experiment.py):
    // "Translate page to <language>", then "Show original" from the same menu.
    CefPostDelayedTask(TID_UI, base::BindOnce([](std::weak_ptr<State> weak) {
      const auto state = weak.lock(); if (!state || !state->content) return;
      if (!state->menu_popup) state->menu_popup = std::make_shared<MenuPopup>(state);
      if (state->menu_translate_now) {
        // A real website: the page menu's command itself, without aiming a right click at the page.
        TranslatePage(state, state->content);
        Later(state, 24000, [](const std::shared_ptr<State>& s) {
          s->Record("menu_probe_result", CefDictionaryValue::Create()); s->menu_done = true;
        });
        return;
      }
      static constexpr auto right_click = [](const std::shared_ptr<State>& s) {
        s->Record("translate_probe_right_click");
        Mouse(s, "mouseMoved", 120, 60, false);
        for (const char* type : {"mousePressed", "mouseReleased"}) {
          auto input = CefDictionaryValue::Create(); input->SetString("type", type); input->SetDouble("x", 120); input->SetDouble("y", 60);
          input->SetString("button", "right"); input->SetInt("clickCount", 1);
          s->content->GetHost()->ExecuteDevToolsMethod(0, "Input.dispatchMouseEvent", input);
        }
      };
      right_click(state);
      Later(state, 1500, [](const std::shared_ptr<State>& s) {
        s->menu_popup->ProbeRun(0, ActivateKey(std::to_string(kTranslatePageMenu)));
      });
      // Then "Show original" from the same menu must put the German text back.
      Later(state, 14000, [](const std::shared_ptr<State>& s) { if (s->content) right_click(s); });
      Later(state, 15500, [](const std::shared_ptr<State>& s) {
        s->menu_popup->ProbeRun(0, ActivateKey(std::to_string(kShowOriginalMenu)));
      });
      Later(state, 20000, [](const std::shared_ptr<State>& s) {
        auto data = CefDictionaryValue::Create(); data->SetBool("closed", !s->menu_popup->is_open());
        s->Record("menu_probe_result", data); s->menu_done = true;
      });
    }, std::weak_ptr<State>(state)), 4000);
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
      Later(state, 1500, [](const std::shared_ptr<State>& s) { s->menu_popup->ProbeRun(0, Row(std::to_string(cef_id_for_command_id_name("IDC_RELOAD")), "click")); });
      Later(state, 4000, [](const std::shared_ptr<State>& s) {
        auto data = CefDictionaryValue::Create(); data->SetBool("closed", !s->menu_popup->is_open());
        data->SetInt("loads_before", s->menu_loads); data->SetInt("loads_after", s->content_loads);
        s->Record("menu_probe_result", data); s->menu_done = true;
      });
    }, std::weak_ptr<State>(state)), 1200);
    return;
  }
  if (state->menu_settings) {
    // Settings on this profile: does the window come up, and what does its page get (settings-console.txt)?
    if (!state->menu_popup) state->menu_popup = std::make_shared<MenuPopup>(state);  // Later() runs steps only with one.
    Later(state, 1500, [](const std::shared_ptr<State>& s) { ShowSettings(s); });
    Later(state, 7000, [](const std::shared_ptr<State>& s) {
      auto data = CefDictionaryValue::Create();
      const auto window = s->settings_window ? s->settings_window->window() : nullptr;
      data->SetBool("exists", window != nullptr); data->SetBool("visible", window && window->IsVisible());
      s->Record("settings_probe", data);
      const auto browser = s->settings_window ? s->settings_window->browser() : nullptr;
      if (!browser) return;
      if (CefCommandLine::GetGlobalCommandLine()->GetSwitchValue("probe-menu") == "link-routing") {
        browser->GetMainFrame()->ExecuteJavaScript(R"JS((async () => {
          const pause = () => new Promise(resolve => setTimeout(resolve, 30));
          const wait = async fn => { for (let i = 0; i < 100; ++i) { if (fn()) return; await pause(); } throw Error('Editor timed out'); };
          const query = request => new Promise((resolve, reject) => cefQuery({request: JSON.stringify(request),
            onSuccess: raw => resolve(JSON.parse(raw)), onFailure: reject}));
          try {
            document.querySelector('[data-subsection="links"]').click();
            const add = () => document.querySelector('.settings-add-link-route:enabled');
            const rows = () => [...document.querySelectorAll('.settings-link-route')];
            const save = () => document.querySelector('#link-routing-heading').parentElement.querySelector('button[type=submit]');
            const enter = async (input, value) => {
              Object.getOwnPropertyDescriptor(HTMLInputElement.prototype, 'value').set.call(input, value);
              input.dispatchEvent(new Event('input', {bubbles: true})); await pause();
            };
            await wait(add);
            for (const [index, pattern] of ['example.test/', 'example.test/team/'].entries()) {
              add().click(); await wait(() => rows().length === index + 1);
              const input = rows()[index].querySelector('input');
              if (document.activeElement !== input || !save().disabled) throw Error('New rule field state');
              if (index === 1) {
                const mode = rows()[index].querySelector('select');
                mode.value = 'regex'; mode.dispatchEvent(new Event('change', {bubbles: true})); await pause();
              }
              await enter(input, pattern);
            }
            rows()[1].querySelector('.settings-link-route-actions button').click();
            await wait(() => rows()[0].querySelector('input').value === 'example.test/team/');
            save().click(); await wait(() => save().disabled && add());
            document.querySelector('[data-subsection="general"]').click(); await pause();
            document.querySelector('[data-subsection="links"]').click(); await wait(add);
            const stored = await query({method: 'settings.get'});
            const {linkRouting} = stored.preferences;
            if (linkRouting.fallback !== 'littleSol' || linkRouting.rules[0].match !== 'regex' ||
                linkRouting.rules.map(rule => rule.pattern).join(',') !== 'example.test/team/,example.test/' ||
                rows()[0].querySelector('input').value !== 'example.test/team/' || !save().disabled) throw Error('Saved rule order');
            let rejected = 0;
            try { await query({method: 'settings.set', key: 'linkRouting', expectedRevision: stored.linkRoutingRevision,
                value: {...linkRouting, fallback: '../default'}}); }
            catch (code) { rejected = code; }
            if (rejected !== 400) throw Error('Native settings accepted an invalid destination');
            rejected = 0;
            try { await query({method: 'settings.set', key: 'linkRouting', expectedRevision: stored.linkRoutingRevision, value: {...linkRouting,
                rules: [{match: 'regex', pattern: '(', destination: 'recent'}]}}); }
            catch (code) { rejected = code; }
            if (rejected !== 400) throw Error('Native settings accepted invalid regex syntax');
            console.info('LINK_ROUTING_SETTINGS pass');
          } catch (error) { console.info('LINK_ROUTING_SETTINGS fail ' + error); }
        })())JS", "", 0);
      } else {
        browser->GetMainFrame()->ExecuteJavaScript(
            "(() => { const t = performance.now(); window.cefQuery({ request: JSON.stringify({ method: 'settings.get' }), persistent: false,"
            " onSuccess: r => console.log('SETTINGS_GET ' + Math.round(performance.now() - t) + 'ms ' + r),"
            " onFailure: (c, m) => console.log('SETTINGS_FAIL ' + Math.round(performance.now() - t) + 'ms ' + c + ' ' + m) }); })()", "", 0);
      }
    });
    Later(state, 16000, [](const std::shared_ptr<State>& s) {
      auto done = CefDictionaryValue::Create(); done->SetBool("closed", true);
      s->Record("menu_probe_result", done); s->menu_done = true;
    });
    return;
  }
  if (state->menu_capture_tiles) {
    // The capture window's top-right tiles (scripts/capture_picker_experiment.py --tiles): Save visible after the
    // whole-page picture is there, then Save full page; pictures of the window before each.
    if (!state->menu_popup) state->menu_popup = std::make_shared<MenuPopup>(state);  // Later() runs steps only with one.
    Later(state, 1500, [](const std::shared_ptr<State>& s) { StartCapturePicker(s); });
    Later(state, 5000, [](const std::shared_ptr<State>& s) { OverlayPicture(s, "tiles-visible"); });
    Later(state, 5400, [](const std::shared_ptr<State>& s) { Tile(s, 1); });
    Later(state, 8000, [](const std::shared_ptr<State>& s) { StartCapturePicker(s); });
    Later(state, 11500, [](const std::shared_ptr<State>& s) { OverlayPicture(s, "tiles-full"); });
    Later(state, 11900, [](const std::shared_ptr<State>& s) { Tile(s, 0); });
    Later(state, 17000, [](const std::shared_ptr<State>& s) {
      RecordDownloads(s);
      auto done = CefDictionaryValue::Create(); done->SetBool("closed", true);
      s->Record("menu_probe_result", done); s->menu_done = true;
    });
    return;
  }
  if (state->menu_capture) {
    // The capture window (scripts/capture_picker_experiment.py reads page_screenshot, the saved pictures and the
    // window's own pictures overlay.png / glide.png / adjust.png in the report's folder):
    //  1. hover the element: a picture of the overlay; click it; a picture of the adjust state; Ctrl+S downloads it
    //  2. drag an area, resize it by its corner handle, Enter copies it (clipboard only, no file)
    //  3. Escape cancels
    if (!state->menu_popup) state->menu_popup = std::make_shared<MenuPopup>(state);  // Later() runs steps only with one.
    Later(state, 1500, [](const std::shared_ptr<State>& s) { StartCapturePicker(s); });
    Later(state, 2600, [](const std::shared_ptr<State>& s) { Mouse(s, "mouseMoved", 500, 260, false); });
    Later(state, 2900, [](const std::shared_ptr<State>& s) { OverlayPicture(s, "overlay"); });
    // Moving to another element: the highlight glides, so a picture taken 60 ms into the move shows it between the two.
    Later(state, 3200, [](const std::shared_ptr<State>& s) { Mouse(s, "mouseMoved", 150, 470, false); });
    Later(state, 3260, [](const std::shared_ptr<State>& s) { OverlayPicture(s, "glide"); });
    Later(state, 3600, [](const std::shared_ptr<State>& s) { Mouse(s, "mouseMoved", 500, 260, false); });
    Later(state, 3900, [](const std::shared_ptr<State>& s) { Mouse(s, "mousePressed", 500, 260, true); Mouse(s, "mouseReleased", 500, 260, false); });
    Later(state, 4800, [](const std::shared_ptr<State>& s) { OverlayPicture(s, "adjust"); });
    Later(state, 6000, [](const std::shared_ptr<State>& s) { Key(s, "s", "KeyS", 83, 2); });
    Later(state, 8500, [](const std::shared_ptr<State>& s) { StartCapturePicker(s); });
    Later(state, 9300, [](const std::shared_ptr<State>& s) { Mouse(s, "mouseMoved", 60, 80, false); Mouse(s, "mousePressed", 60, 80, true); });
    Later(state, 9700, [](const std::shared_ptr<State>& s) { Mouse(s, "mouseMoved", 160, 150, true); Mouse(s, "mouseMoved", 260, 200, true); });
    Later(state, 10200, [](const std::shared_ptr<State>& s) { Mouse(s, "mouseReleased", 260, 200, false); });
    Later(state, 10900, [](const std::shared_ptr<State>& s) { Mouse(s, "mouseMoved", 260, 200, false); Mouse(s, "mousePressed", 260, 200, true); });
    Later(state, 11300, [](const std::shared_ptr<State>& s) { Mouse(s, "mouseMoved", 300, 240, true); });
    Later(state, 11700, [](const std::shared_ptr<State>& s) { Mouse(s, "mouseReleased", 300, 240, false); });
    Later(state, 12500, [](const std::shared_ptr<State>& s) { Key(s, "Enter", "Enter", 13, 0); });
    Later(state, 13600, [](const std::shared_ptr<State>& s) { RecordClipboardPicture(s); });
    // A drag that starts low in the window, is carried by the wheel to the bottom of the page and then goes
    // far past the page's end: the area must end exactly at the page's edge, not beyond it.
    Later(state, 14500, [](const std::shared_ptr<State>& s) { StartCapturePicker(s); });
    Later(state, 15300, [](const std::shared_ptr<State>& s) { Mouse(s, "mouseMoved", 100, 600, false); Mouse(s, "mousePressed", 100, 600, true); });
    Later(state, 15700, [](const std::shared_ptr<State>& s) { Wheel(s, 100, 600, 5000); });
    Later(state, 15790, [](const std::shared_ptr<State>& s) { OverlayPicture(s, "scrolling"); });
    Later(state, 16500, [](const std::shared_ptr<State>& s) { Mouse(s, "mouseMoved", 400, 770, true); Mouse(s, "mouseMoved", 900000, 770, true); });
    Later(state, 16800, [](const std::shared_ptr<State>& s) { OverlayPicture(s, "long-drag"); });
    Later(state, 17000, [](const std::shared_ptr<State>& s) { Mouse(s, "mouseReleased", 400, 770, false); });
    Later(state, 17500, [](const std::shared_ptr<State>& s) { OverlayPicture(s, "long"); });
    Later(state, 17800, [](const std::shared_ptr<State>& s) { Key(s, "s", "KeyS", 83, 2); });
    Later(state, 20500, [](const std::shared_ptr<State>& s) { StartCapturePicker(s); });
    Later(state, 21500, [](const std::shared_ptr<State>& s) { Key(s, "Escape", "Escape", 27, 0); });
    Later(state, 23500, [](const std::shared_ptr<State>& s) {
      RecordDownloads(s);
      auto done = CefDictionaryValue::Create(); done->SetBool("closed", true);
      s->Record("menu_probe_result", done); s->menu_done = true;
    });
    return;
  }
  if (state->menu_switcher_again) {
    // After a restart: which tabs does the strip offer? (A tab closed from the strip must not come back.)
    if (!state->menu_popup) state->menu_popup = std::make_shared<MenuPopup>(state);  // Later() runs steps only with one.
    Later(state, 3000, [](const std::shared_ptr<State>& s) { s->probe_ctrl_held = true; ExecuteShortcut(s, Shortcut::RecentTab); });
    Later(state, 4200, [](const std::shared_ptr<State>& s) { s->probe_ctrl_held = false; });
    Later(state, 6000, [](const std::shared_ptr<State>& s) {
      auto done = CefDictionaryValue::Create(); done->SetBool("closed", true);
      s->Record("menu_probe_result", done); s->menu_done = true;
    });
    return;
  }
  if (state->menu_switcher_empty) {
    // An empty tab (no page in front): the strip is drawn over the empty page area all the same. Ctrl is
    // "held", two presses, then Ctrl is let go (scripts/tab_switcher_experiment.py --empty).
    if (!state->menu_popup) state->menu_popup = std::make_shared<MenuPopup>(state);  // Later() runs steps only with one.
    Later(state, 1200, [](const std::shared_ptr<State>& s) { s->workspace->CloseActive(); });  // The only open page: the tab is empty after it.
    Later(state, 2500, [](const std::shared_ptr<State>& s) {
      auto data = CefDictionaryValue::Create(); data->SetBool("content", s->content != nullptr); s->Record("switcher_empty_start", data);
      s->probe_ctrl_held = true; ExecuteShortcut(s, Shortcut::RecentTab);
    });
    Later(state, 3200, [](const std::shared_ptr<State>& s) { ExecuteShortcut(s, Shortcut::RecentTab); });
    Later(state, 5200, [](const std::shared_ptr<State>& s) { s->probe_ctrl_held = false; });
    Later(state, 7000, [](const std::shared_ptr<State>& s) {
      RecordActive(s, "after_release");
      auto done = CefDictionaryValue::Create(); done->SetBool("closed", true);
      s->Record("menu_probe_result", done); s->menu_done = true;
    });
    return;
  }
  if (state->menu_switcher) {
    // The Ctrl+Tab strip (scripts/tab_switcher_experiment.py): a second tab is visited so it has a picture, then
    // Ctrl is "held" (probe_ctrl_held), the strip shows, the selection moves to the third tab, Ctrl+W closes that
    // tab from the strip, and letting go switches to the tab that is then selected. The script photographs the
    // window while the strip is up.
    if (!state->menu_popup) state->menu_popup = std::make_shared<MenuPopup>(state);  // Later() runs steps only with one.
    Later(state, 3500, [](const std::shared_ptr<State>& s) { RecordActive(s, "start"); ExecuteShortcut(s, Shortcut::RecentTab); });
    Later(state, 6500, [](const std::shared_ptr<State>& s) { RecordActive(s, "visited_b"); ExecuteShortcut(s, Shortcut::RecentTab); });
    Later(state, 9500, [](const std::shared_ptr<State>& s) { RecordActive(s, "back_on_a"); s->probe_ctrl_held = true; ExecuteShortcut(s, Shortcut::RecentTab); });
    Later(state, 11000, [](const std::shared_ptr<State>& s) { ExecuteShortcut(s, Shortcut::RecentTab); });
    Later(state, 12500, [](const std::shared_ptr<State>& s) { ExecuteShortcut(s, Shortcut::CloseTab); });  // Ctrl+W: closes it from the strip.
    Later(state, 14000, [](const std::shared_ptr<State>& s) { s->probe_ctrl_held = false; });
    Later(state, 16500, [](const std::shared_ptr<State>& s) {
      RecordActive(s, "after_release");
      auto done = CefDictionaryValue::Create(); done->SetBool("closed", true);
      s->Record("menu_probe_result", done); s->menu_done = true;
    });
    return;
  }
  if (state->menu_copy) {
    // Right click on the copy-link button opens this menu; "Super Copy" must leave the address without trackers on the clipboard.
    CefPostDelayedTask(TID_UI, base::BindOnce([](std::weak_ptr<State> weak) {
      const auto state = weak.lock(); if (!state) return;
      ShowCopyMenu(state, kMainMenuAnchor);
      Later(state, 1500, [](const std::shared_ptr<State>& s) { s->menu_popup->ProbeRun(0, ActivateKey("superCopy")); });
      Later(state, 3000, [](const std::shared_ptr<State>& s) {
        auto data = CefDictionaryValue::Create(); data->SetString("clipboard", ClipboardText()); s->Record("copy_menu_probe", data);
        auto done = CefDictionaryValue::Create(); done->SetBool("closed", !s->menu_popup->is_open());
        s->Record("menu_probe_result", done); s->menu_done = true;
      });
    }, std::weak_ptr<State>(state)), 1200);
    return;
  }
  if (state->menu_recent) {
    // Ctrl+Tab by recent use and Super Copy, through the entry the keys use (ExecuteShortcut).
    // No key is held, so every press is its own cycle: A -> the tab used before -> back to A.
    if (!state->menu_popup) state->menu_popup = std::make_shared<MenuPopup>(state);  // Later() runs steps only with one.
    Later(state, 3500, [](const std::shared_ptr<State>& s) { RecordActive(s, "start"); ExecuteShortcut(s, Shortcut::RecentTab); });
    Later(state, 5500, [](const std::shared_ptr<State>& s) { RecordActive(s, "after_first"); ExecuteShortcut(s, Shortcut::RecentTab); });
    Later(state, 7500, [](const std::shared_ptr<State>& s) { RecordActive(s, "after_second"); ExecuteShortcut(s, Shortcut::CopyUrl); });
    Later(state, 8500, [](const std::shared_ptr<State>& s) {
      auto data = CefDictionaryValue::Create(); data->SetString("clipboard", ClipboardText()); s->Record("recent_probe_copy", data);
      auto done = CefDictionaryValue::Create(); done->SetBool("closed", true);
      s->Record("menu_probe_result", done); s->menu_done = true;
    });
    return;
  }
  if (state->menu_screenshot) {
    // The visible area, then the whole page (scripts/cheap_parity_experiment.py reads page_screenshot).
    if (!state->menu_popup) state->menu_popup = std::make_shared<MenuPopup>(state);  // Later() runs steps only with one.
    Later(state, 1500, [](const std::shared_ptr<State>& s) { CapturePage(s, false); });
    Later(state, 5000, [](const std::shared_ptr<State>& s) { CapturePage(s, true); });
    Later(state, 9000, [](const std::shared_ptr<State>& s) {
      auto data = CefDictionaryValue::Create(); data->SetBool("closed", true);
      s->Record("menu_probe_result", data); s->menu_done = true;
    });
    return;
  }
  if (state->menu_update) {
    // "Check for Updates" in the main menu opens the update window. The probe then records every
    // status the window would show until the check ends (scripts/update_window_e2e.py); with
    // --probe-menu=update-restart it presses "Restart to update" once a version waits.
    CefPostDelayedTask(TID_UI, base::BindOnce([](std::weak_ptr<State> weak) {
      const auto state = weak.lock(); if (!state) return;
      ShowMainMenu(state, kMainMenuAnchor);
      // The menu's own click handler, as the edge probe uses: a native mouse click can miss when someone moves the pointer.
      Later(state, 1500, [](const std::shared_ptr<State>& s) { s->menu_popup->ProbeRun(0, ActivateKey("checkUpdates")); });
      Later(state, 2500, [](const std::shared_ptr<State>& s) { WatchUpdate(s, 0, -1, -1, false); });
    }, std::weak_ptr<State>(state)), 1200);
    return;
  }
  CefPostDelayedTask(TID_UI, base::BindOnce([](std::weak_ptr<State> weak) {
    const auto state = weak.lock(); if (!state) return;
    ShowMainMenu(state, kMainMenuAnchor);
    Later(state, 1500, [](const std::shared_ptr<State>& s) { s->menu_popup->ProbeRun(0, Row("tabs", "move")); });
    Later(state, 3000, [](const std::shared_ptr<State>& s) { s->menu_popup->ProbeRun(1, Row("nextTab", "click")); });
    Later(state, 4500, [](const std::shared_ptr<State>& s) {
      auto data = CefDictionaryValue::Create(); data->SetBool("closed", !s->menu_popup->is_open());
      s->Record("menu_probe_result", data); s->menu_done = true;
    });
  }, std::weak_ptr<State>(state)), 1200);
}
}  // namespace red
