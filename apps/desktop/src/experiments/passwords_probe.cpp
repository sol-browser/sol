// Synthetic localhost tests only. Never reads real website credentials.
#include "experiments/passwords_probe.h"
#include "windows/passwords_window.h"
#include "windows/settings_window.h"
#include "experiments/shell_capture.h"
#include "include/cef_devtools_message_observer.h"
#include "include/cef_parser.h"
#include "include/cef_resource_bundle.h"
#include "include/base/cef_callback.h"
#include "include/wrapper/cef_closure_task.h"
#include "include/cef_id_mappers.h"
#include "include/views/cef_display.h"
#include "workspace/controller.h"
#include <objbase.h>
#include <oleauto.h>
#include <UIAutomation.h>
#include <wrl/client.h>
#include <thread>
namespace red {
namespace {
class Result final : public CefDevToolsMessageObserver {
 public:
  explicit Result(std::weak_ptr<State> state) : state_(std::move(state)) {}
  void OnDevToolsMethodResult(CefRefPtr<CefBrowser>, int, bool ok, const void* data, size_t size) override {
    if (!ok || size > 4096) return;
    const auto json = CefParseJSON(data, size, JSON_PARSER_RFC);
    const auto outer = json && json->GetType() == VTYPE_DICTIONARY ? json->GetDictionary() : nullptr;
    const auto result = outer ? outer->GetDictionary("result") : nullptr;
    const auto value = result ? result->GetDictionary("value") : nullptr;
    if (!value || !value->GetBool("sol_password_probe")) return;
    if (auto state = state_.lock()) state->Record("passwords_probe_result", value->Copy(false));
  }
 private:
  std::weak_ptr<State> state_;
  IMPLEMENT_REFCOUNTING(Result);
};
void Eval(CefRefPtr<CefBrowser> page, const std::string& js) {
  if (!page || !page->IsValid()) return;
  auto args = CefDictionaryValue::Create(); args->SetString("expression", js);
  args->SetBool("returnByValue", true); args->SetBool("awaitPromise", true); args->SetBool("userGesture", true);
  page->GetHost()->ExecuteDevToolsMethod(0, "Runtime.evaluate", args);
}
void Click(CefRefPtr<CefBrowser> page, int x, int y) {
  for (const char* kind : {"mousePressed", "mouseReleased"}) {
    auto args=CefDictionaryValue::Create();args->SetString("type",kind);args->SetInt("x",x);args->SetInt("y",y);
    args->SetString("button","left");args->SetInt("clickCount",1);
    page->GetHost()->ExecuteDevToolsMethod(0,"Input.dispatchMouseEvent",args);
  }
}
void Text(CefRefPtr<CefBrowser> page, const char* text) {
  auto args=CefDictionaryValue::Create();args->SetString("text",text);
  page->GetHost()->ExecuteDevToolsMethod(0,"Input.insertText",args);
}
// Input.dispatchMouseEvent is asynchronous. Wait for each protocol reply before
// selecting/inserting text in the next field; otherwise IME text can precede
// the focus change and all three synthetic values land in the first field.
class ChangeInput final : public CefDevToolsMessageObserver {
 public:
  explicit ChangeInput(CefRefPtr<CefBrowser> page):page_(page) {
    Replace(85,"Synthetic-password-42!");Replace(135,"Synthetic-updated-73!");Replace(185,"Synthetic-updated-73!");
    auto check=CefDictionaryValue::Create();check->SetBool("returnByValue",true);
    check->SetString("expression",R"JS((()=>({sol_password_probe:true,phase:'change_input',old:document.querySelector('#old-password')?.value==='Synthetic-password-42!',updated:document.querySelector('#new-password')?.value==='Synthetic-updated-73!',confirmed:document.querySelector('#confirm-password')?.value==='Synthetic-updated-73!'}))())JS");
    steps_.emplace_back("Runtime.evaluate",check);Mouse(50,235);
  }
  void Start(){Next();}
  void OnDevToolsMethodResult(CefRefPtr<CefBrowser>,int id,bool ok,const void*,size_t) override {
    if(id==pending_){if(ok)Next();else steps_.clear();}
  }
 private:
  void Mouse(int x,int y) {
    for(const char* type:{"mousePressed","mouseReleased"}) {
      auto args=CefDictionaryValue::Create();args->SetString("type",type);args->SetInt("x",x);args->SetInt("y",y);
      args->SetString("button","left");args->SetInt("clickCount",1);steps_.emplace_back("Input.dispatchMouseEvent",args);
    }
  }
  void Replace(int y,const char* value) {
    Mouse(60,y);
    for(const char* type:{"rawKeyDown","keyUp"}) {
      auto args=CefDictionaryValue::Create();args->SetString("type",type);args->SetString("key","a");
      args->SetString("code","KeyA");args->SetInt("windowsVirtualKeyCode",65);args->SetInt("modifiers",2);
      if(std::string_view(type)=="rawKeyDown"){auto commands=CefListValue::Create();commands->SetString(0,"selectAll");args->SetList("commands",commands);}
      steps_.emplace_back("Input.dispatchKeyEvent",args);
    }
    auto text=CefDictionaryValue::Create();text->SetString("text",value);steps_.emplace_back("Input.insertText",text);
  }
  void Next() {
    if(index_==steps_.size()||!page_->IsValid())return;
    static int next_id=700000;pending_=++next_id;
    const auto step=steps_[index_++];page_->GetHost()->ExecuteDevToolsMethod(pending_,step.first,step.second);
  }
  CefRefPtr<CefBrowser> page_;
  std::vector<std::pair<std::string,CefRefPtr<CefDictionaryValue>>> steps_;
  size_t index_=0;
  int pending_=0;
  IMPLEMENT_REFCOUNTING(ChangeInput);
};
// Test-only: invoke the single hyperlink in this process's synthetic save
// bubble. It neither reads credential fields nor approves OS authentication.
bool InvokeSyntheticManagerLink() {
  using Microsoft::WRL::ComPtr;
  if(FAILED(CoInitializeEx(nullptr,COINIT_MULTITHREADED)))return false;
  bool invoked=false;
  {
    ComPtr<IUIAutomation> automation;
    if(SUCCEEDED(CoCreateInstance(__uuidof(CUIAutomation),nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&automation)))) {
      ComPtr<IUIAutomation2> options;if(SUCCEEDED(automation.As(&options))){options->put_ConnectionTimeout(2000);options->put_TransactionTimeout(2000);}
      std::vector<HWND> windows;
      EnumWindows([](HWND w,LPARAM data)->BOOL{DWORD pid=0;GetWindowThreadProcessId(w,&pid);RECT r{};GetClientRect(w,&r);
        wchar_t klass[64]{};GetClassNameW(w,klass,64);
        if(pid==GetCurrentProcessId()&&IsWindowVisible(w)&&std::wstring(klass)==L"Chrome_WidgetWin_1"&&r.right>=400&&r.right<=650&&r.bottom>=300&&r.bottom<=600)
          reinterpret_cast<std::vector<HWND>*>(data)->push_back(w);
        return TRUE;
      },reinterpret_cast<LPARAM>(&windows));
      VARIANT type{};type.vt=VT_I4;type.lVal=UIA_HyperlinkControlTypeId;
      ComPtr<IUIAutomationCondition> condition;automation->CreatePropertyCondition(UIA_ControlTypePropertyId,type,&condition);
      for(auto w:windows) {
        ComPtr<IUIAutomationElement> root,link;automation->ElementFromHandle(w,&root);
        if(!root||!condition)continue;
        root->FindFirst(TreeScope_Descendants,condition.Get(),&link);
        ComPtr<IUIAutomationInvokePattern> invoke;
        if(link&&SUCCEEDED(link->GetCurrentPatternAs(UIA_InvokePatternId,IID_PPV_ARGS(&invoke)))&&invoke&&SUCCEEDED(invoke->Invoke())){invoked=true;break;}
      }
    }
  }
  CoUninitialize();return invoked;
}
HWND SyntheticBubble() {
  struct Scan {HWND found=nullptr;} scan;
  EnumThreadWindows(GetCurrentThreadId(),[](HWND w,LPARAM data)->BOOL {
    RECT r{};GetClientRect(w,&r);wchar_t klass[64]{};GetClassNameW(w,klass,64);
    if(IsWindowVisible(w)&&GetWindow(w,GW_OWNER)&&std::wstring(klass)==L"Chrome_WidgetWin_1"&&r.right>=400&&r.right<=650&&r.bottom>=300&&r.bottom<=600) {
      reinterpret_cast<Scan*>(data)->found=w;return FALSE;
    } return TRUE;
  },reinterpret_cast<LPARAM>(&scan));return scan.found;
}
void MeasureSyntheticBubble(const std::shared_ptr<State>& state,const char* phase,HWND bubble) {
  RECT root{},rect{};GetWindowRect(state->window->GetWindowHandle(),&root);
  const bool visible=IsWindow(bubble)&&IsWindowVisible(bubble)&&GetWindowRect(bubble,&rect);
  auto data=CefDictionaryValue::Create();data->SetString("phase",phase);data->SetBool("visible",visible);
  if(visible){data->SetInt("right",root.right-rect.right);data->SetInt("top",rect.top-root.top);}
  state->Record("passwords_probe_bubble_position",data);
}
void AcceptSyntheticSave(const std::shared_ptr<State>& state) {
  struct Run { HWND root; bool clicked=false; } run{state->window->GetWindowHandle()};
  EnumWindows([](HWND window,LPARAM value)->BOOL{
    auto& run=*reinterpret_cast<Run*>(value);
    DWORD pid=0;GetWindowThreadProcessId(window,&pid);
    if(pid!=GetCurrentProcessId() || !IsWindowVisible(window) || window==run.root)return TRUE;
    wchar_t klass[80]{};GetClassNameW(window,klass,80);RECT r{};GetClientRect(window,&r);
    // This disposable process has one synthetic login and no other dialogs.
    // Click the native Save button in this fixed-size synthetic fixture.
    if(std::wstring(klass)!=L"Chrome_WidgetWin_1" || r.right<400 || r.right>650 || r.bottom<300 || r.bottom>600)return TRUE;
    const LPARAM at=MAKELPARAM(r.right*63/100,r.bottom*76/100);
    PostMessageW(window,WM_LBUTTONDOWN,MK_LBUTTON,at);PostMessageW(window,WM_LBUTTONUP,0,at);
    run.clicked=true;return FALSE;
  },reinterpret_cast<LPARAM>(&run));
  auto d=CefDictionaryValue::Create();d->SetBool("invoked",run.clicked);state->Record("passwords_probe_save_prompt",d);
}
HWND SyntheticUpdateBubble() {
  struct Scan {std::wstring title;HWND found=nullptr;} scan{
      CefResourceBundle::GetGlobal()->GetLocalizedString(cef_id_for_pack_string_name("IDS_UPDATE_PASSWORD")).ToWString()};
  if(scan.title.empty())return nullptr;
  EnumThreadWindows(GetCurrentThreadId(),[](HWND w,LPARAM data)->BOOL {
    auto& scan=*reinterpret_cast<Scan*>(data);wchar_t title[160]{},klass[64]{};
    GetWindowTextW(w,title,160);GetClassNameW(w,klass,64);
    if(IsWindowVisible(w)&&GetWindow(w,GW_OWNER)&&std::wstring(klass)==L"Chrome_WidgetWin_1"&&std::wstring(title)==scan.title){scan.found=w;return FALSE;}
    return TRUE;
  },reinterpret_cast<LPARAM>(&scan));return scan.found;
}
bool InvokeSyntheticUpdate(HWND window,const std::wstring& button_name) {
  // Bound to the exact synthetic update bubble, never the manager, credential
  // fields, or Windows reauthentication. UIA must run off the browser UI thread.
  using Microsoft::WRL::ComPtr;
  DWORD pid=0;GetWindowThreadProcessId(window,&pid);
  if(!IsWindow(window)||pid!=GetCurrentProcessId()||FAILED(CoInitializeEx(nullptr,COINIT_MULTITHREADED)))return false;
  bool invoked=false;
  {
    ComPtr<IUIAutomation> automation;
    if(SUCCEEDED(CoCreateInstance(__uuidof(CUIAutomation),nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&automation)))) {
      ComPtr<IUIAutomation2> options;if(SUCCEEDED(automation.As(&options))){options->put_ConnectionTimeout(2000);options->put_TransactionTimeout(2000);}
      VARIANT name{};name.vt=VT_BSTR;name.bstrVal=SysAllocString(button_name.c_str());
      VARIANT type{};type.vt=VT_I4;type.lVal=UIA_ButtonControlTypeId;
      ComPtr<IUIAutomationCondition> name_condition,type_condition,condition;
      if(name.bstrVal)automation->CreatePropertyCondition(UIA_NamePropertyId,name,&name_condition);
      SysFreeString(name.bstrVal);
      automation->CreatePropertyCondition(UIA_ControlTypePropertyId,type,&type_condition);
      if(name_condition&&type_condition)automation->CreateAndCondition(name_condition.Get(),type_condition.Get(),&condition);
      ComPtr<IUIAutomationElement> root,button;automation->ElementFromHandle(window,&root);
      if(root&&condition)root->FindFirst(TreeScope_Descendants,condition.Get(),&button);
      ComPtr<IUIAutomationInvokePattern> invoke;
      if(button&&SUCCEEDED(button->GetCurrentPatternAs(UIA_InvokePatternId,IID_PPV_ARGS(&invoke)))&&invoke)invoked=SUCCEEDED(invoke->Invoke());
    }
  }
  CoUninitialize();return invoked;
}
// The same synthetic origin/profile is retained by the Python fixture across
// save -> password-change -> restart. No private API edits the credential.
void PasswordChange(const std::shared_ptr<State>& state,CefRefPtr<CefBrowser> page,bool restarting) {
  const auto observer=page->GetHost()->AddDevToolsMessageObserver(new Result(state));
  using Task=std::function<void(std::weak_ptr<State>,CefRefPtr<CefBrowser>)>;
  const auto later=[&](int ms,Task task){CefPostDelayedTask(TID_UI,base::BindOnce(
      [](Task task,std::weak_ptr<State> weak,CefRefPtr<CefBrowser> page){task(weak,page);},std::move(task),std::weak_ptr<State>(state),page),ms);};
  later(1200,[restarting](std::weak_ptr<State> weak,CefRefPtr<CefBrowser> page){
    if(auto s=weak.lock()){s->window->Activate();page->GetHost()->SetFocus(true);}
    Click(page,60,restarting?35:85);
  });
  if(!restarting) {
    later(1800,[](std::weak_ptr<State>,CefRefPtr<CefBrowser> page){
      CefRefPtr<ChangeInput> input=new ChangeInput(page);
      const auto watch=page->GetHost()->AddDevToolsMessageObserver(input);input->Start();
      CefPostDelayedTask(TID_UI,base::BindOnce([](CefRefPtr<CefRegistration>){},watch),20000);
    });
    later(3500,[](std::weak_ptr<State> weak,CefRefPtr<CefBrowser>){if(auto s=weak.lock()) {
      const auto bubble=SyntheticUpdateBubble();auto data=CefDictionaryValue::Create();
      data->SetBool("visible",bubble!=nullptr);data->SetBool("updateTitle",bubble!=nullptr);
      s->Record("passwords_probe_update_prompt",data);
    }});
    later(4000,[](std::weak_ptr<State> weak,CefRefPtr<CefBrowser>){
      const auto bubble=SyntheticUpdateBubble();
      const auto button=CefResourceBundle::GetGlobal()->GetLocalizedString(cef_id_for_pack_string_name("IDS_PASSWORD_MANAGER_UPDATE_BUTTON")).ToWString();
      std::thread([weak,bubble,button]{const bool invoked=bubble&&!button.empty()&&InvokeSyntheticUpdate(bubble,button);
        CefPostTask(TID_UI,base::BindOnce([](std::weak_ptr<State> weak,bool invoked){if(auto s=weak.lock()){
          auto data=CefDictionaryValue::Create();data->SetBool("invoked",invoked);s->Record("passwords_probe_update_accept",data);
        }},weak,invoked));
      }).detach();
    });
    later(6500,[](std::weak_ptr<State>,CefRefPtr<CefBrowser> page){
      const auto url=page->GetMainFrame()->GetURL().ToString();
      page->GetMainFrame()->LoadURL(url.substr(0,url.find('/',url.find("://")+3))+"/source?passwords-updated");
    });
    later(7200,[](std::weak_ptr<State>,CefRefPtr<CefBrowser> page){Click(page,60,35);});
  }
  later(restarting?1800:7600,[](std::weak_ptr<State>,CefRefPtr<CefBrowser> page){
    Eval(page,R"JS((()=>({sol_password_probe:true,phase:'updated_autofill',username:document.querySelector('#username').value==='sol-synthetic',updated:document.querySelector('#password').value==='Synthetic-updated-73!',old:document.querySelector('#password').value==='Synthetic-password-42!'}))())JS");
  });
  later(8500,[](std::weak_ptr<State> weak,CefRefPtr<CefBrowser>){if(auto s=weak.lock())ShowPasswords(s,"default");});
  later(11000,[](std::weak_ptr<State> weak,CefRefPtr<CefBrowser>){if(auto s=weak.lock()) {
    const auto found=s->password_windows.find("default");if(found==s->password_windows.end())return;
    const auto browser=found->second->browser();if(!browser)return;
    const auto watch=browser->GetHost()->AddDevToolsMessageObserver(new Result(s));
    Eval(browser,R"JS((async()=>{const list=await chrome.passwordsPrivate.getSavedPasswordList();return {sol_password_probe:true,phase:'updated_store',count:list.length,synthetic:list.filter(e=>e.username==='sol-synthetic').length,noBridge:typeof window.cefQuery==='undefined'};})())JS");
    CefPostDelayedTask(TID_UI,base::BindOnce([](CefRefPtr<CefRegistration>){},watch),2000);
  }});
  CefPostDelayedTask(TID_UI,base::BindOnce([](std::weak_ptr<State> weak,CefRefPtr<CefRegistration>){if(auto s=weak.lock())s->link_headers_done=true;},std::weak_ptr<State>(state),observer),27000);
}
}
void StartPasswordsProbe(const std::shared_ptr<State>& state, CefRefPtr<CefBrowser> page, const std::string& url) {
  if (!state->probe || !state->passwords_probe || !url.starts_with("http://127.0.0.1:")) return;
  if(url.ends_with("/source?passwords-update")||url.ends_with("/source?passwords-updated")) {
    PasswordChange(state,page,url.ends_with("/source?passwords-updated"));return;
  }
  const bool dismissing=url.ends_with("/source?passwords-dismiss");
  const bool linking=url.ends_with("/source?passwords-link");
  const bool moving=url.ends_with("/source?passwords-move");
  const bool saving=dismissing || linking || moving || url.ends_with("/source?passwords-save");
  const auto observer=page->GetHost()->AddDevToolsMessageObserver(new Result(state));
  using Task=std::function<void(std::weak_ptr<State>,CefRefPtr<CefBrowser>)>;
  const auto later=[&](int ms,Task task){CefPostDelayedTask(TID_UI,base::BindOnce(
      [](Task task,std::weak_ptr<State> weak,CefRefPtr<CefBrowser> page){task(weak,page);},std::move(task),std::weak_ptr<State>(state),page),ms);};
  later(1200,[](std::weak_ptr<State> weak,CefRefPtr<CefBrowser> page){
    if (auto s=weak.lock()) {s->window->Activate();page->GetHost()->SetFocus(true);}
    Click(page,60,35); // A real gesture allows Chromium to expose a stored password.
  });
  later(1600,[](std::weak_ptr<State>,CefRefPtr<CefBrowser> page){
    Eval(page,R"JS((() => ({sol_password_probe:true,phase:'autofill',username:document.querySelector('#username').value==='sol-synthetic',password:document.querySelector('#password').value==='Synthetic-password-42!'}))())JS");
  });
  if (saving) {
    later(1800,[](std::weak_ptr<State>,CefRefPtr<CefBrowser> page){
      Click(page,60,35);Text(page,"sol-synthetic");Click(page,60,85);Text(page,"Synthetic-password-42!");Click(page,50,135);
    });
    later(3000,[](std::weak_ptr<State> weak,CefRefPtr<CefBrowser> page){
      const int command=cef_id_for_command_id_name("IDC_MANAGE_PASSWORDS_FOR_PAGE");
      const bool enabled=page->GetHost()->CanExecuteChromeCommand(command);
      if(auto s=weak.lock()){auto d=CefDictionaryValue::Create();d->SetBool("enabled",enabled);s->Record("passwords_probe_command",d);}
      // Do not open it manually: verify the automatic prompt.
    });
    if(moving) {
      auto bubble=std::make_shared<HWND>(nullptr);
      later(3500,[bubble](std::weak_ptr<State> weak,CefRefPtr<CefBrowser>){if(auto s=weak.lock()){*bubble=SyntheticBubble();MeasureSyntheticBubble(s,"before",*bubble);}});
      later(4000,[](std::weak_ptr<State> weak,CefRefPtr<CefBrowser>){if(auto s=weak.lock()) {
        auto b=s->window->GetBounds();const auto work=CefDisplay::GetDisplayMatchingBounds(b,false)->GetWorkArea();
        b.x+=(b.x+b.width+80<=work.x+work.width?80:-80);b.y+=(b.y+b.height+40<=work.y+work.height?40:-40);s->window->SetBounds(b);
      }});
      later(4500,[bubble](std::weak_ptr<State> weak,CefRefPtr<CefBrowser>){if(auto s=weak.lock())MeasureSyntheticBubble(s,"moved",*bubble);});
      later(5000,[](std::weak_ptr<State> weak,CefRefPtr<CefBrowser>){if(auto s=weak.lock()) {
        auto b=s->window->GetBounds();const auto work=CefDisplay::GetDisplayMatchingBounds(b,false)->GetWorkArea();
        b.width+=(b.x+b.width+80<=work.x+work.width?80:-80);s->window->SetBounds(b);
      }});
      later(5500,[bubble](std::weak_ptr<State> weak,CefRefPtr<CefBrowser>){if(auto s=weak.lock())MeasureSyntheticBubble(s,"resized",*bubble);});
      later(6000,[](std::weak_ptr<State> weak,CefRefPtr<CefBrowser>){if(auto s=weak.lock())AcceptSyntheticSave(s);});
    }
    else if(linking) later(4000,[](std::weak_ptr<State> weak,CefRefPtr<CefBrowser>){
      std::thread([weak]{const bool invoked=InvokeSyntheticManagerLink();
        CefPostTask(TID_UI,base::BindOnce([](std::weak_ptr<State> weak,bool invoked){if(auto s=weak.lock()){auto data=CefDictionaryValue::Create();data->SetBool("invoked",invoked);s->Record("passwords_probe_manager_link",data);}},weak,invoked));
      }).detach();
    });
    else if(!dismissing) later(4000,[](std::weak_ptr<State> weak,CefRefPtr<CefBrowser>){if(auto s=weak.lock())AcceptSyntheticSave(s);});
    else later(4000,[](std::weak_ptr<State> weak,CefRefPtr<CefBrowser>){
      if(auto s=weak.lock()) {
        const auto snapshot=s->workspace->Snapshot();auto request=CefDictionaryValue::Create();
        request->SetString("method","tree.close");request->SetString("id",snapshot->GetString("activeId"));request->SetDouble("expectedRevision",snapshot->GetDouble("treeRevision"));
        const auto url=s->initial_url;const auto origin=url.substr(0,url.find('/',url.find("://")+3));
        s->workspace->Handle(request,[weak,origin](int code,const std::string&,CefRefPtr<CefDictionaryValue>){if(auto s=weak.lock()){auto d=CefDictionaryValue::Create();d->SetBool("closed",!code);s->Record("passwords_probe_tab_closed",d);if(!code)s->workspace->OpenLink(origin+"/done",true);}});
      }
    });
  }
  if (saving && !dismissing && !linking) later(6500,[](std::weak_ptr<State>,CefRefPtr<CefBrowser> page){
    const auto url=page->GetMainFrame()->GetURL().ToString();
    page->GetMainFrame()->LoadURL(url.substr(0,url.find('/',url.find("://")+3))+"/source?passwords-check");
  });
  if (!saving) later(2500,[](std::weak_ptr<State> weak,CefRefPtr<CefBrowser>){
    const auto s=weak.lock();if(!s || !s->workspace)return;
    const auto snapshot=s->workspace->Snapshot();
    auto request=CefDictionaryValue::Create();request->SetString("method","profile.create");request->SetString("title","Password isolation probe");
    request->SetDouble("expectedRevision",snapshot->GetDouble("treeRevision"));
    s->workspace->Handle(request,[weak](int code,const std::string&,CefRefPtr<CefDictionaryValue> result){
      const auto s=weak.lock();if(!s || code || !result)return;
      const auto profiles=result->GetList("profiles");if(!profiles)return;
      for(size_t i=0;i<profiles->GetSize();++i){const auto p=profiles->GetDictionary(i);if(p->GetString("name")=="Password isolation probe")ShowPasswords(s,p->GetString("id"));}
    });
  });
  if (!saving) later(6500,[](std::weak_ptr<State> weak,CefRefPtr<CefBrowser>){
    if(const auto s=weak.lock()) for(const auto& [id,manager]:s->password_windows) if(id!="default") if(const auto browser=manager->browser()) {
      const auto watch=browser->GetHost()->AddDevToolsMessageObserver(new Result(s));
      Eval(browser,R"JS((async()=>{
        try {
        const p=chrome.passwordsPrivate;
        const listWhen=async predicate=>{for(let i=0;i<100;++i){const list=await p.getSavedPasswordList();if(predicate(list))return list;await new Promise(r=>setTimeout(r,50));}throw new Error('list did not settle');};
        globalThis.solPasswordProbeStage='list';
        const before=await p.getSavedPasswordList();
        globalThis.solPasswordProbeStage='add';
        await p.addPassword({url:'https://password-probe.invalid/',username:'sol-isolated',password:'Synthetic-other-42!',note:'',useAccountStore:false});
        globalThis.solPasswordProbeStage='after';
        const after=await listWhen(list=>list.some(e=>e.username==='sol-isolated'));
        const added=after.find(e=>e.username==='sol-isolated');
        if(added)p.removeCredential(added.id,'DEVICE');
        const removed=await listWhen(list=>list.length===0);
        return {sol_password_probe:true,phase:'isolation',empty:before.length===0,added:after.length===1&&!!added,removed:removed.length===0,noBridge:typeof window.cefQuery==='undefined'};
        } catch(error) {return {sol_password_probe:true,phase:'isolation',error:'native-operation-failed'};}
      })())JS");
      CefPostDelayedTask(TID_UI,base::BindOnce([](CefRefPtr<CefRegistration>){},watch),20000);
    }
  });
  if(!linking) later(8000,[](std::weak_ptr<State> weak,CefRefPtr<CefBrowser>){if(auto s=weak.lock()) ShowPasswords(s,"default");});
  CefPostDelayedTask(TID_UI,base::BindOnce([](std::weak_ptr<State> weak,CefRefPtr<CefRegistration> observer){
    if(auto s=weak.lock()) {
      const auto manager=s->password_windows.find("default");
      if(manager!=s->password_windows.end()) if(const auto browser=manager->second->browser()) {
        const auto watch=browser->GetHost()->AddDevToolsMessageObserver(new Result(s));
        Eval(browser,R"JS((async () => {const list=await chrome.passwordsPrivate.getSavedPasswordList();return {sol_password_probe:true,phase:'store',available:true,count:list.length,synthetic:list.filter(e=>e.username==='sol-synthetic').length,ui:!!document.querySelector('password-manager-app'),noBridge:typeof window.cefQuery==='undefined'};})())JS");
        CefPostDelayedTask(TID_UI,base::BindOnce([](CefRefPtr<CefRegistration>){},watch),1000);
      }
    }
  },std::weak_ptr<State>(state),observer),11000);
  if(!saving) later(17000,[](std::weak_ptr<State> weak,CefRefPtr<CefBrowser>){if(auto s=weak.lock())ShowSettings(s);});
  if(!saving) later(19000,[](std::weak_ptr<State> weak,CefRefPtr<CefBrowser>){
    if(auto s=weak.lock();s&&s->settings_window)if(auto ui=s->settings_window->browser())
      Eval(ui,"[...document.querySelectorAll('nav button')].find(b=>b.textContent==='Passwords')?.click()");
  });
  if(!saving) later(20000,[](std::weak_ptr<State> weak,CefRefPtr<CefBrowser>){
    if(auto s=weak.lock();s&&s->settings_window)if(auto ui=s->settings_window->browser()) {
      const auto watch=ui->GetHost()->AddDevToolsMessageObserver(new Result(s));
      Eval(ui,R"JS((async()=>{
        const q=r=>new Promise((resolve,reject)=>window.cefQuery({request:JSON.stringify(r),persistent:false,onSuccess:resolve,onFailure:code=>reject(code)}));
        let wrongProfile=0,extraField=0;
        try{await q({method:'settings.passwords',profile:'../Default',settings:false});}catch(code){wrongProfile=code;}
        try{await q({method:'settings.passwords',profile:'default',settings:false,password:'unexpected'});}catch(code){extraField=code;}
        const snapshot=await q({method:'settings.get'});
        const select=document.querySelector('#password-profile');
        const profileChoices=select?.options.length||0;
        document.querySelector('.settings-page .settings-primary')?.click();
        return {sol_password_probe:true,phase:'settings',profileChoices,selected:select?.value==='default',wrongProfile,extraField,noSecrets:!snapshot.includes('Synthetic-password-42!')&&!snapshot.includes('sol-synthetic')};
      })())JS");
      const auto capture=CaptureShell(ui,s->evidence_directory/"password-settings.png");
      CefPostDelayedTask(TID_UI,base::BindOnce([](CefRefPtr<CefRegistration>,CefRefPtr<CefRegistration>){},watch,capture),2000);
    }
  });
  if(!saving) later(23000,[](std::weak_ptr<State> weak,CefRefPtr<CefBrowser>){
    if(auto s=weak.lock())for(const auto& [id,manager]:s->password_windows)if(id!="default")if(const auto browser=manager->browser())
      Eval(browser,"({sol_password_probe:true,phase:'isolation_stage',stage:globalThis.solPasswordProbeStage})");
  });
  later(27000,[](std::weak_ptr<State> weak,CefRefPtr<CefBrowser>){if(auto s=weak.lock())s->link_headers_done=true;});
}
}
