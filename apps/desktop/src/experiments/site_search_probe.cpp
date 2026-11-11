#include "experiments/site_search_probe.h"
#include "workspace/controller.h"
#include "experiments/shell_capture.h"
#include "app/session.h"
#include "windows/settings_window.h"
#include "trust_policy.h"
#include "include/cef_parser.h"
#include "include/base/cef_callback.h"
#include "include/wrapper/cef_closure_task.h"
namespace red {
namespace {
std::weak_ptr<State> probing;
std::function<void()> finished;
CefRefPtr<CefRegistration> capture;
void Finish(std::weak_ptr<State> weak, int attempts) {
  const auto state = weak.lock(); if (!state) return;
  if (state->settings_window && (state->settings_window->window() || state->settings_window->browser())) {
    if (attempts > 0) CefPostDelayedTask(TID_UI, base::BindOnce(&Finish, weak, attempts-1), 30);
    return;
  }
  // The fixture returns to its own owner only after the utility actually closed.
  // Normal command preparation still checks real foreground ownership.
  state->window->Activate();
  auto done = std::move(finished); probing.reset(); if (done) done();
}
void Drive(std::weak_ptr<State> weak, std::string url, int attempts) {
  const auto state = weak.lock(); if (!state || !state->probe || !state->browsing_probe) return;
  const auto browser = state->settings_window ? state->settings_window->browser() : nullptr;
  if (!browser || browser->IsLoading()) {
    if (attempts > 0) CefPostDelayedTask(TID_UI, base::BindOnce(&Drive, weak, url, attempts-1), 30);
    return;
  }
  auto encoded = CefValue::Create(); encoded->SetString(url);
  browser->GetMainFrame()->ExecuteJavaScript(R"JS((async()=>{
    const pause=ms=>new Promise(r=>setTimeout(r,ms));
    const until=async fn=>{for(let i=0;i<100;i++){if(fn())return;await pause(30);}throw Error('Site search form timeout');};
    const call=request=>new Promise((resolve,reject)=>cefQuery({request:JSON.stringify(request),onSuccess:raw=>resolve(JSON.parse(raw)),onFailure:code=>reject(code)}));
    try {
      await until(()=>document.querySelector('.site-search-settings'));
      const before=await call({method:'settings.get'}), base=before.preferences.siteSearches;
      const site={name:'Owned',keyword:'owned.test',url:)JS" + CefWriteJSON(encoded, JSON_WRITER_DEFAULT).ToString() + R"JS(};
      const reject=async value=>{let code=0;try{await call({method:'settings.set',key:'siteSearches',value});}catch(e){code=e;}if(code!==400)throw Error('Unsafe site search accepted');};
      await reject([site,site]);
      await reject([{...site,url:'https://%s.example/'}]);
      await reject([{...site,url:'https://user:pass@example.test/?q=%s'}]);
      await reject([{...site,url:'javascript:alert(%s)'}]);
      await reject([{...site,extra:true}]);
      await reject([{...site,name:'\u00a0'}]);
      await reject([{...site,enabled:1}]);
      let activationRejected=0;try{await call({method:'settings.set',key:'siteSearchActivation',value:'enter'});}catch(code){activationRejected=code;}
      if(activationRejected!==400)throw Error('Unsafe activation accepted');
      const root=document.querySelector('.site-search-settings');
      const field=async(label,value)=>{const i=root.querySelector(`input[aria-label="${label}"]`);Object.getOwnPropertyDescriptor(HTMLInputElement.prototype,'value').set.call(i,value);i.dispatchEvent(new Event('input',{bubbles:true}));await pause(40);};
      const entry=keyword=>[...root.querySelectorAll('.site-search-entry')].find(row=>row.querySelector('code').textContent===keyword);
      const openAdd=async()=>{root.querySelector('.site-search-group-heading > button').click();await until(()=>root.querySelector('dialog[open]'));};
      const add=async(name,keyword)=>{await openAdd();await field('Site search name',name);await field('Site search shortcut',keyword);await field('Site search URL',site.url);root.querySelector('form').requestSubmit();await until(()=>entry(keyword)&&!root.querySelector('dialog[open]'));};
      await openAdd();await field('Site search name','Cancelled');root.querySelector('dialog button[type=button]').click();await until(()=>!root.querySelector('dialog[open]'));
      await add('Owned','owned.test');
      await add('Temporary','temporary');
      const menu=(keyword,text)=>{const row=entry(keyword);row.querySelector('summary').click();[...row.querySelectorAll('.site-search-menu button')].find(b=>b.textContent===text).click();};
      menu('temporary','Delete');await until(()=>!entry('temporary'));
      menu('owned.test','Deactivate');await until(()=>entry('owned.test')?.querySelector('button[aria-label="Activate Owned"]'));
      entry('owned.test').querySelector('button[aria-label="Activate Owned"]').click();await until(()=>!entry('owned.test')?.querySelector('button[aria-label="Activate Owned"]'));
      entry('owned.test').querySelector('button[aria-label="Edit Owned"]').click();await until(()=>root.querySelector('dialog[open]'));
      if(document.activeElement!==root.querySelector('input[aria-label="Site search name"]'))throw Error('Dialog did not focus the name');
      console.info('RED_SITE_SEARCH_DIALOG');await pause(160);
      await field('Site search name','Owned search');root.querySelector('form').requestSubmit();
      await until(()=>entry('owned.test')?.querySelector('strong').textContent==='Owned search'&&!root.querySelector('dialog[open]'));
      root.querySelector('input[value="tab"]').click();await until(()=>!root.querySelector('input[value="tab"]').matches(':disabled'));await pause(40);
      if((await call({method:'settings.get'})).preferences.siteSearchActivation!=='tab')throw Error('Tab setting not saved');
      root.querySelector('input[value="spaceOrTab"]').click();await until(()=>!root.querySelector('input[value="spaceOrTab"]').matches(':disabled'));await pause(40);
      const saved=await call({method:'settings.get'});
      if(saved.preferences.siteSearches.length!==base.length+1||!saved.preferences.siteSearches.some(s=>s.keyword==='owned.test'&&s.name==='Owned search'&&s.enabled!==false)||saved.preferences.siteSearchActivation!=='spaceOrTab')throw Error('Settings did not retain search');
      root.scrollIntoView({block:'start'});await pause(80);
      // What's new is a link to the website's notes for this version (no bundled history).
      [...document.querySelectorAll('.settings-nav button')].find(b=>b.textContent.trim()==='What’s new').click();
      const notesButton=()=>[...document.querySelectorAll('button')].find(b=>b.textContent.includes('Open release notes')&&!b.disabled);
      await until(notesButton);const notes=notesButton();
      const bundledHistory=!!document.querySelector('.settings-release, .settings-releases');
      notes.click();await pause(400);
      console.info('RED_SITE_SEARCH_SETTINGS:'+JSON.stringify({passed:!bundledHistory,nativeRejected:8,dialog:true,activation:true,whatsNewClicked:true,bundledHistory}));
    }catch(error){console.info('RED_SITE_SEARCH_SETTINGS:'+JSON.stringify({passed:false,error:String(error)}));}
  })())JS", std::string(kSettingsUrl), 0);
}
}  // namespace
void StartSiteSearchProbe(const std::shared_ptr<State>& state, const std::string& input, std::function<void()> done) {
  if (!state->probe || !state->browsing_probe) return;
  probing = state; finished = std::move(done);
  const auto host = input.substr(0, input.find('/'));
  // The command renderer opens Settings through its real allowlisted action.
  Drive(state, "http://" + host + "/%s", 100);
}
void ObserveSiteSearchProbe(CefRefPtr<CefBrowser> browser, const std::string& message) {
  const auto state = probing.lock();
  const std::string prefix = "RED_SITE_SEARCH_SETTINGS:";
  if (!state || !state->probe || !state->browsing_probe || browser->GetIdentifier() != state->settings_id) return;
  if (message == "RED_SITE_SEARCH_DIALOG") { capture = CaptureShell(browser, state->evidence_directory / "site-search-dialog.png"); return; }
  if (!message.starts_with(prefix)) return;
  const auto value = CefParseJSON(message.substr(prefix.size()), JSON_PARSER_RFC);
  if (!value || value->GetType() != VTYPE_DICTIONARY) return;
  const auto details = value->GetDictionary();
  auto loaded = std::make_shared<BrowserPreferences>(state->profile / "BrowserData" / "settings.sqlite3");
  const auto& sites = loaded->Get().site_searches;
  const bool persisted = loaded->error().empty() && std::any_of(sites.begin(), sites.end(), [](const SiteSearch& site) {
    return site.keyword == "owned.test" && site.name == "Owned search" && site.enabled;
  }) && loaded->Get().site_search_activation == "spaceOrTab";
  // The What's new button opened this version's notes on the website as a tab.
  bool release_notes = false;
  if (state->workspace) if (const auto snapshot = state->workspace->Snapshot()) if (const auto nodes = snapshot->GetList("nodes"))
    for (size_t i = 0; i < nodes->GetSize(); ++i)
      if (nodes->GetDictionary(i)->GetString("url").ToString().starts_with("https://sol-browser.app/changelog/#v")) release_notes = true;
  details->SetBool("releaseNotesTab", release_notes);
  const bool passed = details->GetBool("passed") && persisted && release_notes;
  loaded->Stop(); details->SetBool("persisted", persisted); state->Record("site_search_settings_probe", details);
  capture = CaptureShell(browser, state->evidence_directory / "site-search-settings.png");
  CefPostDelayedTask(TID_UI, base::BindOnce([](std::shared_ptr<State> owner, bool passed) {
    capture = nullptr; owner->settings_window->Close();
    if (!passed) { owner->exit_code = 2; return; }
    Finish(owner, 100);
  }, state, passed), 250);
}
}  // namespace red
