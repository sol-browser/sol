// Disposable native-window fixture only; no real credentials or OS auth UI.
#include "experiments/utility_windows_probe.h"
#include "windows/settings_window.h"
#include "windows/passwords_window.h"
#include "windows/extensions_window.h"
#include "windows/utility_window.h"
#include "include/views/cef_display.h"
#include "include/views/cef_browser_view.h"
#include "include/cef_id_mappers.h"
#include "include/base/cef_callback.h"
#include "include/wrapper/cef_closure_task.h"
#include <windows.h>

namespace red {
namespace {
struct Run {
  std::weak_ptr<State> state;
  std::vector<CefRefPtr<CefDisplay>> displays;
  HWND other=nullptr, last=nullptr;
  ~Run() {if(IsWindow(other))DestroyWindow(other);}
  void AcquireForeground(HWND window) {
    // This probe is not launched by a user click. Generate a benign Alt gesture
    // once in the disposable test so Windows permits its activation; production
    // utilities never inject input or bypass foreground restrictions.
    INPUT keys[2]{};keys[0].type=keys[1].type=INPUT_KEYBOARD;
    keys[0].ki.wVk=keys[1].ki.wVk=VK_MENU;keys[1].ki.dwFlags=KEYEVENTF_KEYUP;
    SendInput(2,keys,sizeof(INPUT));SetForegroundWindow(window);
  }
  void Move(size_t index) {
    const auto s=state.lock();if(!s||!s->window||displays.empty())return;
    const auto work=displays[index%displays.size()]->GetWorkArea();
    s->window->SetBounds(UtilityWindowBounds(work,work,CefSize(1100,820)));s->window->Activate();
  }
  void Inspect(const std::string& name,CefRefPtr<CefWindow> window) {
    const auto s=state.lock();if(!s||!s->window)return;
    last=window?window->GetWindowHandle():nullptr;auto d=CefDictionaryValue::Create();d->SetString("name",name);d->SetBool("found",IsWindow(last));
    if(IsWindow(last)) {
      const auto root=s->window->GetWindowHandle();const auto style=GetWindowLongPtr(last,GWL_STYLE);
      RECT rect{};GetWindowRect(last,&rect);MONITORINFO monitor{sizeof(monitor)};GetMonitorInfo(MonitorFromWindow(last,MONITOR_DEFAULTTONEAREST),&monitor);
      const auto menu=GetSystemMenu(last,FALSE);SendMessage(last,WM_INITMENU,reinterpret_cast<WPARAM>(menu),0);
      d->SetBool("owned",GetWindow(last,GW_OWNER)==root);
      d->SetBool("sameDisplay",MonitorFromWindow(root,MONITOR_DEFAULTTONEAREST)==MonitorFromWindow(last,MONITOR_DEFAULTTONEAREST));
      d->SetBool("withinWorkArea",rect.left>=monitor.rcWork.left&&rect.top>=monitor.rcWork.top&&rect.right<=monitor.rcWork.right&&rect.bottom<=monitor.rcWork.bottom);
      d->SetBool("resizable",(style&WS_THICKFRAME)!=0);
      d->SetBool("noMinimize",!(style&WS_MINIMIZEBOX));d->SetBool("noMaximize",!(style&WS_MAXIMIZEBOX));
      d->SetBool("minDisabled",(GetMenuState(menu,SC_MINIMIZE,MF_BYCOMMAND)&(MF_DISABLED|MF_GRAYED))!=0);
      d->SetBool("maxDisabled",(GetMenuState(menu,SC_MAXIMIZE,MF_BYCOMMAND)&(MF_DISABLED|MF_GRAYED))!=0);
      SendMessage(last,WM_SYSCOMMAND,SC_MINIMIZE,0);SendMessage(last,WM_SYSCOMMAND,SC_MAXIMIZE,0);
      SendMessage(last,WM_NCLBUTTONDBLCLK,HTCAPTION,0);
      d->SetBool("systemCommandsBlocked",!IsIconic(last)&&!IsZoomed(last));
      d->SetBool("foreground",GetForegroundWindow()==last);
      d->SetBool("ownerForeground",GetForegroundWindow()==root);
      DWORD foreground_pid=0;GetWindowThreadProcessId(GetForegroundWindow(),&foreground_pid);
      d->SetBool("processForeground",foreground_pid==GetCurrentProcessId());
      const auto view=window->GetChildViewCount()?window->GetChildViewAt(0)->AsBrowserView():nullptr;
      const auto browser=view?view->GetBrowser():nullptr;
      if(browser) {
        CefKeyEvent key;key.type=KEYEVENT_RAWKEYDOWN;key.windows_key_code=VK_F11;browser->GetHost()->SendKeyEvent(key);
        key.type=KEYEVENT_KEYUP;browser->GetHost()->SendKeyEvent(key);
        browser->GetHost()->ExecuteChromeCommand(cef_id_for_command_id_name("IDC_FULLSCREEN"),CEF_WOD_CURRENT_TAB);
      }
    }
    s->Record("utility_probe_window",d);
  }
  void Closed(const std::string& name,bool background=false) {
    const auto s=state.lock();if(!s||!s->window)return;
    auto d=CefDictionaryValue::Create();d->SetString("name",name);d->SetBool("destroyed",!IsWindow(last));
    d->SetBool("correctForeground",GetForegroundWindow()==(background?other:s->window->GetWindowHandle()));
    s->Record("utility_probe_closed",d);
  }
  void Normal(const std::string& name,CefRefPtr<CefWindow> window) {
    const auto s=state.lock();if(!s)return;
    auto d=CefDictionaryValue::Create();d->SetString("name",name);
    d->SetBool("normal",window&&!window->IsFullscreen()&&!window->IsMaximized()&&!window->IsMinimized());
    s->Record("utility_probe_fullscreen",d);
  }
};
}
void StartUtilityWindowsProbe(const std::shared_ptr<State>& state) {
  if(!state->probe||!state->passwords_probe)return;
  auto run=std::make_shared<Run>();run->state=state;CefDisplay::GetAllDisplays(run->displays);
  // Prefer a non-primary display; display enumeration order is not specified.
  const auto primary=CefDisplay::GetPrimaryDisplay();
  if(run->displays.size()>1&&primary&&run->displays[0]->GetID()==primary->GetID())std::swap(run->displays[0],run->displays[1]);
  auto setup=CefDictionaryValue::Create();setup->SetBool("multipleDisplays",run->displays.size()>1);
  const auto negative=UtilityWindowBounds(CefRect(-1500,-500,900,700),CefRect(-1920,-900,1920,900),CefSize(1000,760));
  const auto small=UtilityWindowBounds(CefRect(0,0,600,400),CefRect(0,0,600,400),CefSize(1000,760));
  setup->SetBool("geometry",negative.x>=-1920&&negative.y>=-900&&negative.x+negative.width<=0&&negative.y+negative.height<=0&&small==CefRect(0,0,600,400));
  state->Record("utility_probe_setup",setup);
  using Action=std::function<void(std::shared_ptr<Run>,std::shared_ptr<State>)>;
  const auto later=[&](int ms,Action action){CefPostDelayedTask(TID_UI,base::BindOnce(
      [](std::shared_ptr<Run> run,Action action){if(auto s=run->state.lock();s&&s->window)action(run,s);},run,std::move(action)),ms);};
  later(100,[](auto r,auto s){r->Move(0);r->other=CreateWindowExW(0,L"STATIC",L"Other application fixture",WS_OVERLAPPEDWINDOW,80,80,440,280,nullptr,nullptr,GetModuleHandle(nullptr),nullptr);ShowWindow(r->other,SW_SHOWNOACTIVATE);r->AcquireForeground(s->window->GetWindowHandle());ShowSettings(s);});
  later(2200,[](auto r,auto s){r->Inspect("settings",s->settings_window->window());});
  later(2500,[](auto r,auto s){r->Normal("settings",s->settings_window->window());s->settings_window->Close();});
  later(3000,[](auto r,auto){r->Closed("settings");});
  later(3300,[](auto,auto s){ShowSettings(s);});
  later(4400,[](auto r,auto s){r->last=s->settings_window->window()?s->settings_window->window()->GetWindowHandle():nullptr;ShowWindow(r->other,SW_SHOW);SetForegroundWindow(r->other);});
  later(4700,[](auto,auto s){s->settings_window->Close();});
  later(5200,[](auto r,auto s){r->Closed("settings-background",true);s->window->Activate();ShowPasswords(s,"default");});
  later(7200,[](auto r,auto s){const auto it=s->password_windows.find("default");r->Inspect("passwords",it==s->password_windows.end()?nullptr:it->second->window());});
  later(7500,[](auto r,auto s){r->Normal("passwords",s->password_windows.at("default")->window());s->password_windows.at("default")->Close();});
  later(8000,[](auto r,auto){r->Closed("passwords");});
  later(8300,[](auto,auto s){if(!s->extensions_window)s->extensions_window=std::make_shared<ExtensionsWindow>(s);s->extensions_window->Show();});
  later(10300,[](auto r,auto s){r->Inspect("extensions",s->extensions_window->window());});
  later(10600,[](auto r,auto s){r->Normal("extensions",s->extensions_window->window());s->extensions_window->Close();});
  later(11100,[](auto r,auto){r->Closed("extensions");});
  later(11400,[](auto r,auto s){r->Move(1);ShowSettings(s);});
  later(13000,[](auto r,auto s){r->Inspect("settings-reopened",s->settings_window->window());});
  later(13400,[](auto r,auto s){r->Move(0);ShowSettings(s);});
  later(14300,[](auto r,auto s){r->Inspect("settings-existing-moved",s->settings_window->window());s->settings_window->Close();});
  later(15000,[](auto r,auto s){r->Closed("settings-moved");if(IsWindow(r->other))DestroyWindow(r->other);r->other=nullptr;s->link_headers_done=true;});
}
}  // namespace red
