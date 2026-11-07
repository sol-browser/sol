#include "windows/browser_dialogs.h"
#include "include/base/cef_callback.h"
#include "include/wrapper/cef_closure_task.h"
namespace red {
namespace {
struct Closing;
std::map<int,std::shared_ptr<Closing>> pending;
std::vector<HWND> Dialogs(const std::shared_ptr<State>& state, CefRefPtr<CefBrowser> browser) {
  struct Scan { HWND root; std::set<HWND> surfaces; std::vector<HWND> dialogs; } scan{};
  scan.root=GetAncestor(browser->GetHost()->GetWindowHandle(),GA_ROOT);
  if (!scan.root) return {};
  for (const auto& [id,item]:state->browsers) if(item->IsValid())
    scan.surfaces.insert(GetAncestor(item->GetHost()->GetWindowHandle(),GA_ROOT));
  EnumThreadWindows(GetCurrentThreadId(),[](HWND window,LPARAM data)->BOOL {
    auto& scan=*reinterpret_cast<Scan*>(data);
    const HWND owner=GetWindow(window,GW_OWNER);
    if (!owner || GetAncestor(owner,GA_ROOTOWNER)!=scan.root || scan.surfaces.contains(window))return TRUE;
    wchar_t klass[64]{};GetClassNameW(window,klass,64);
    if(std::wstring(klass)==L"Chrome_WidgetWin_1" && (GetWindowLongPtrW(window,GWL_STYLE)&WS_POPUP))scan.dialogs.push_back(window);
    return TRUE;
  },reinterpret_cast<LPARAM>(&scan));
  return scan.dialogs;
}
struct Closing : std::enable_shared_from_this<Closing> {
  std::weak_ptr<State> state;
  CefRefPtr<CefBrowser> browser;
  bool force=false;
  std::set<HWND> notified;
  void Advance() {
    const auto s=state.lock();
    if (!s || !browser->IsValid()) { pending.erase(browser->GetIdentifier()); return; }
    const auto dialogs=Dialogs(s,browser);
    if (dialogs.empty()) {
      pending.erase(browser->GetIdentifier());
      browser->GetHost()->CloseBrowser(force);
      return;
    }
    for (const auto window:dialogs) if(notified.insert(window).second) SendMessageW(window,WM_CLOSE,0,0);
    // Widget::Close is asynchronous. Wait for actual native destruction, not
    // a guessed delay after which a still-live bubble would outlive its owner.
    CefPostDelayedTask(TID_UI,base::BindOnce([](std::shared_ptr<Closing> self){self->Advance();},shared_from_this()),10);
  }
};
}
void CloseBrowserWithDialogs(std::weak_ptr<State> state,CefRefPtr<CefBrowser> browser,bool force) {
  if (!browser || !browser->IsValid())return;
  const int id=browser->GetIdentifier();
  if (const auto found=pending.find(id);found!=pending.end()) {found->second->force|=force;return;}
  const auto s=state.lock();if(!s) {browser->GetHost()->CloseBrowser(force);return;}
  if(Dialogs(s,browser).empty()){browser->GetHost()->CloseBrowser(force);return;}
  auto closing=std::make_shared<Closing>();closing->state=state;closing->browser=browser;closing->force=force;
  pending[id]=closing;
  s->Record("browser_dialogs_closing");closing->Advance();
}
}
