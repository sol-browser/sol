#include "experiments/profile_settings_probe.h"
#include "experiments/shell_capture.h"
#include "windows/settings_window.h"
#include "windows/panel_popup.h"
#include "windows/workspace_layout.h"
#include "trust_policy.h"
#include "include/cef_parser.h"
#include "include/base/cef_callback.h"
#include "include/wrapper/cef_closure_task.h"

namespace red {
namespace {
std::weak_ptr<State> probing;
// Registrations live only until the Settings browser is closed by the probe.
CefRefPtr<CefRegistration> capture;
void Drive(std::weak_ptr<State> weak,int attempts) {
  const auto state=weak.lock(); if (!state || !state->probe || !state->organization_probe) return;
  const auto browser=state->settings_window?state->settings_window->browser():nullptr;
  if (!browser || browser->IsLoading()) {
    if (attempts>0) CefPostDelayedTask(TID_UI,base::BindOnce(&Drive,weak,attempts-1),30); return;
  }
  browser->GetMainFrame()->ExecuteJavaScript(R"JS((async()=>{
    const pause=ms=>new Promise(resolve=>setTimeout(resolve,ms));
    const until=async fn=>{for(let i=0;i<120;i++){const value=fn();if(value)return value;await pause(30);}throw Error('Profile form timeout');};
    try {
      const create=await until(()=>document.querySelector('.profiles-intro button'));
      create.click();
      const dialog=await until(()=>document.querySelector('.profile-dialog[open]'));
      const input=dialog.querySelector('input[aria-label="Profile name"]');
      Object.getOwnPropertyDescriptor(HTMLInputElement.prototype,'value').set.call(input,'Studio');input.dispatchEvent(new Event('input',{bubbles:true}));
      await pause(80);
      for(const checkbox of dialog.querySelectorAll('input[type=checkbox]'))if(checkbox.checked){checkbox.click();await pause(40);}
      for(const name of ['UI Work','UI Personal']) {
        const row=[...dialog.querySelectorAll('.profile-space-option')].find(row=>row.querySelector('strong').textContent===name);
        if(!row)throw Error('Missing Space '+name);row.querySelector('input').click();await pause(40);
      }
      const selected=[...dialog.querySelectorAll('input[type=checkbox]')].filter(input=>input.checked).length;
      if(selected!==2)throw Error('Wrong selection count');
      console.info('RED_PROFILE_SETTINGS:'+JSON.stringify({phase:'preview',passed:true,selected,dialog:dialog.getBoundingClientRect().toJSON(),viewport:{width:innerWidth,height:innerHeight}}));
      await pause(400);dialog.querySelector('button[type=submit]').click();
      await until(()=>!document.querySelector('.profile-dialog'));
      const saved=!![...document.querySelectorAll('.profile-card h3')].find(node=>node.textContent.includes('Studio'));
      create.click();const another=await until(()=>document.querySelector('.profile-dialog[open]'));
      if(another.querySelector('input[aria-label="Profile name"]').value!=='')throw Error('New profile retained the old name');
      another.querySelector('button[aria-label="Close profile dialog"]').click();await until(()=>!document.querySelector('.profile-dialog'));
      console.info('RED_PROFILE_SETTINGS:'+JSON.stringify({phase:'saved',passed:saved}));
    } catch(error){console.info('RED_PROFILE_SETTINGS:'+JSON.stringify({phase:'error',passed:false,error:String(error)}));}
  })())JS",std::string(kSettingsUrl),0);
}
}
void CloseProfileSettingsProbe(int browser) {
  const auto state=probing.lock();
  if (state && state->settings_id==browser) { capture=nullptr; probing.reset(); }
}
void CaptureProfileEditor(const std::shared_ptr<State>& state,int attempts) {
  if (!state->probe || !state->organization_probe || !state->window || !state->panel_popup) return;
  const auto panel=state->panel_popup;
  if (!panel->window() || !panel->browser() || panel->browser()->IsLoading()) {
    if (attempts>0) CefPostDelayedTask(TID_UI,base::BindOnce(&CaptureProfileEditor,state,attempts-1),30); return;
  }
  const auto root=state->window->GetClientAreaBoundsInScreen(); const auto bounds=panel->window()->GetBounds();
  auto data=CefDictionaryValue::Create();
  data->SetInt("x",bounds.x-root.x); data->SetInt("y",bounds.y-root.y); data->SetInt("width",bounds.width); data->SetInt("height",bounds.height);
  data->SetBool("sidebar_clear",bounds.x-root.x>=SidebarWidth(state->sidebar_width,root.width)+kContentInset);
  data->SetBool("presented",panel->is_presented());
  state->Record("profile_editor_placement",data);
  const auto registration=panel->is_presented()?CaptureShell(panel->browser(),state->evidence_directory/"space-editor.png"):nullptr;
  CefPostDelayedTask(TID_UI,base::BindOnce([](std::shared_ptr<State> owner,CefRefPtr<CefRegistration>) {
    if (owner->panel_popup) owner->panel_popup->Hide(true);
    if (const auto shell=owner->browsers.find(owner->shell_id);shell!=owner->browsers.end())
      shell->second->GetMainFrame()->ExecuteJavaScript("Reflect.set(window,'profileEditorChecked',true)",std::string(kUiUrl),0);
  },state,registration),250);
}
void StartProfileSettingsProbe(const std::shared_ptr<State>& state) {
  if (!state->probe || !state->organization_probe) return;
  capture=nullptr; probing=state; ShowProfileSettings(state,false); Drive(state,100);
}
void ObserveProfileSettingsProbe(CefRefPtr<CefBrowser> browser,const std::string& message) {
  const auto state=probing.lock();
  if (!state || !state->probe || !state->organization_probe || browser->GetIdentifier()!=state->settings_id) return;
  if (message=="RED_PROFILE_SETTINGS_CLOSE") { capture=nullptr; state->settings_window->Close(); return; }
  const std::string prefix="RED_PROFILE_SETTINGS:";
  if (!message.starts_with(prefix)) return;
  const auto value=CefParseJSON(message.substr(prefix.size()),JSON_PARSER_RFC);
  if (!value || value->GetType()!=VTYPE_DICTIONARY) return;
  const auto data=value->GetDictionary();
  const auto phase=data->GetString("phase").ToString();
  state->Record("profile_settings_ui",data);
  if (phase=="preview") capture=CaptureShell(browser,state->evidence_directory/"profile-dialog.png");
  if (phase=="saved") capture=CaptureShell(browser,state->evidence_directory/"profile-settings.png");
}
}
