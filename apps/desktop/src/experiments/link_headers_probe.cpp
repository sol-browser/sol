#include "experiments/utility_windows_probe.h"
#include "experiments/link_headers_probe.h"
#include "experiments/platform_codecs_probe.h"
#include "experiments/devtools_probe.h"
#include "experiments/profiles_probe.h"
#include "experiments/passwords_probe.h"
#include "experiments/page_background_probe.h"
#include "include/base/cef_callback.h"
#include "include/wrapper/cef_closure_task.h"
#include "workspace/controller.h"
#include "windows/devtools.h"
#include "app/session.h"
namespace red {
namespace {
void Click(CefRefPtr<CefBrowser> page) {
  for (const char* type : {"mousePressed", "mouseReleased"}) {
    auto params = CefDictionaryValue::Create(); params->SetString("type", type);
    params->SetDouble("x", 60); params->SetDouble("y", 60); params->SetString("button", "left"); params->SetInt("clickCount", 1);
    page->GetHost()->ExecuteDevToolsMethod(0, "Input.dispatchMouseEvent", params);
  }
  // Leave the test link after clicking: its URL status bubble obscures the
  // source's bottom-left corner and invalidates composed corner captures.
  auto move = CefDictionaryValue::Create(); move->SetString("type", "mouseMoved");
  move->SetDouble("x", 200); move->SetDouble("y", 200);
  page->GetHost()->ExecuteDevToolsMethod(0, "Input.dispatchMouseEvent", move);
}
// Cycle mode: open Link Peek, let it settle, dismiss it, repeat. The close
// capture/return evidence comes from EmbeddedContent's link_peek_* records.
void Cycle(std::weak_ptr<State> weak, CefRefPtr<CefBrowser> page, int left) {
  auto state = weak.lock(); if (!state) return;
  // The last dismissal (1.6 s after the last click) must land before the probe ends.
  if (!left) {
    CefPostDelayedTask(TID_UI, base::BindOnce([](std::weak_ptr<State> weak) { if (auto s = weak.lock()) s->link_headers_done = true; }, weak), 1200);
    return;
  }
  CefPostDelayedTask(TID_UI, base::BindOnce([](std::weak_ptr<State> weak) {
    if (auto state = weak.lock(); state && state->workspace) state->workspace->DismissLinkPeek();
  }, weak), state->link_headers_quick ? 300 : 1600);
  CefPostDelayedTask(TID_UI, base::BindOnce([](std::weak_ptr<State> weak, CefRefPtr<CefBrowser> page, int left) {
    if (left > 1) Click(page);
    Cycle(weak, page, left - 1);
  }, weak, page, left), 2800);
}
void Hover(CefRefPtr<CefBrowser> page, int x, int y) {
  if (!page || !page->IsValid()) return;
  auto params = CefDictionaryValue::Create(); params->SetString("type", "mouseMoved");
  params->SetInt("x", x); params->SetInt("y", y);
  page->GetHost()->ExecuteDevToolsMethod(0, "Input.dispatchMouseEvent", params);
}
void LinkStatus(std::weak_ptr<State> weak, CefRefPtr<CefBrowser> source) {
  const auto at = [&](int ms, auto task) { CefPostDelayedTask(TID_UI, base::BindOnce(task, weak, source), ms); };
  if (auto s = weak.lock(); s && s->workspace) s->workspace->TogglePin();
  at(800, [](std::weak_ptr<State>, CefRefPtr<CefBrowser> page) { Hover(page,60,60); });
  at(1800, [](std::weak_ptr<State>, CefRefPtr<CefBrowser> page) { Hover(page,300,250); });
  at(2300, [](std::weak_ptr<State>, CefRefPtr<CefBrowser> page) { Hover(page,60,110); });
  at(3200, [](std::weak_ptr<State> weak, CefRefPtr<CefBrowser>) { if (auto s=weak.lock(); s && s->window) s->window->Maximize(); });
  at(4100, [](std::weak_ptr<State> weak, CefRefPtr<CefBrowser>) { if (auto s=weak.lock(); s && s->window) s->window->Restore(); });
  at(4700, [](std::weak_ptr<State>, CefRefPtr<CefBrowser> page) { Hover(page,60,160); });
  at(5600, [](std::weak_ptr<State>, CefRefPtr<CefBrowser> page) { Hover(page,300,250); });
  at(6200, [](std::weak_ptr<State>, CefRefPtr<CefBrowser> page) { Click(page); });
  at(8500, [](std::weak_ptr<State> weak, CefRefPtr<CefBrowser> source) {
    if (auto s=weak.lock(); s && s->content && s->content!=source) Hover(s->content,60,60);
  });
  // An empty callback from the source must not clear the preview's label.
  at(9000, [](std::weak_ptr<State>, CefRefPtr<CefBrowser> page) { Hover(page,300,250); });
  at(9500, [](std::weak_ptr<State> weak, CefRefPtr<CefBrowser> source) {
    if (auto s=weak.lock(); s && s->content && s->content!=source) Hover(s->content,300,250);
  });
  at(10200, [](std::weak_ptr<State> weak, CefRefPtr<CefBrowser>) { if (auto s=weak.lock(); s && s->workspace) s->workspace->DismissLinkPeek(); });
  at(11400, [](std::weak_ptr<State>, CefRefPtr<CefBrowser> page) { Hover(page,60,60); });
  at(12400, [](std::weak_ptr<State> weak, CefRefPtr<CefBrowser> page) {
    Hover(page,300,250); if (auto s=weak.lock()) s->link_headers_done=true;
  });
}
// Park mode: open Link Peek, open another tab (the preview parks with its
// source), then return to the source tab (it comes back). Records whether a
// preview is shown and whether the source tab carries the preview's mark.
void Step(std::weak_ptr<State> weak, const char* name) {
  auto state = weak.lock(); if (!state || !state->workspace) return;
  bool marked = false;
  if (const auto snapshot = state->workspace->Snapshot()) if (const auto nodes = snapshot->GetList("nodes"))
    for (size_t i = 0; i < nodes->GetSize(); ++i) if (const auto node = nodes->GetDictionary(i); node && node->GetBool("peek")) marked = true;
  auto data = CefDictionaryValue::Create(); data->SetString("step", name);
  data->SetBool("shown", state->workspace->HasLinkPeek()); data->SetBool("marked", marked);
  state->Record("link_peek_park_step", data);
}
void Park(std::weak_ptr<State> weak, std::string other) {
  const auto at = [&](int ms, auto task) { CefPostDelayedTask(TID_UI, base::BindOnce(task, weak, other), ms); };
  at(1600, [](std::weak_ptr<State> weak, std::string) { Step(weak, "opened"); });
  at(1700, [](std::weak_ptr<State> weak, std::string other) { if (auto s = weak.lock(); s && s->workspace) s->workspace->OpenLink(other, true); });
  at(3200, [](std::weak_ptr<State> weak, std::string) { Step(weak, "away"); });
  at(3300, [](std::weak_ptr<State> weak, std::string) { if (auto s = weak.lock(); s && s->workspace) s->workspace->CycleTab(true); });
  at(4800, [](std::weak_ptr<State> weak, std::string) { Step(weak, "back"); });
  at(5200, [](std::weak_ptr<State> weak, std::string) { if (auto s = weak.lock()) s->link_headers_done = true; });
}
// Minimize mode: the visible page's native size before, while and after the
// window is minimized (a page resized while minimized repaints on restore).
void PageSize(std::weak_ptr<State> weak, const char* name) {
  auto state = weak.lock(); if (!state || !state->content) return;
  RECT rect{}; GetWindowRect(state->content->GetHost()->GetWindowHandle(), &rect);
  auto data = CefDictionaryValue::Create(); data->SetString("step", name);
  data->SetInt("width", rect.right - rect.left); data->SetInt("height", rect.bottom - rect.top);
  data->SetBool("minimized", state->window && state->window->IsMinimized());
  data->SetInt("preferred_sidebar",state->sidebar_width);
  if(state->workspace)data->SetInt("snapshot_sidebar",state->workspace->Snapshot()->GetInt("sidebarWidth"));
  state->Record("link_headers_page_size", data);
  if(state->window&&!state->window->IsMinimized()&&state->workspace){
    const auto shell=state->browsers.find(state->shell_id);
    if(shell!=state->browsers.end()){
      const auto expected=state->workspace->Snapshot()->GetInt("sidebarWidth");
      const auto script="requestAnimationFrame(()=>requestAnimationFrame(()=>{const sidebar=document.querySelector('.sidebar');const width=sidebar?.getBoundingClientRect().width??-1;console.info('RED_SIDEBAR_RESTORE:'+JSON.stringify({step:'"+std::string(name)+"',width,expected:"+std::to_string(expected)+",passed:Math.abs(width-"+std::to_string(expected)+")<1}));}));";
      shell->second->GetMainFrame()->ExecuteJavaScript(script,shell->second->GetMainFrame()->GetURL(),0);
    }
  }
}
void Minimize(std::weak_ptr<State> weak) {
  const auto at = [&](int ms, auto task) { CefPostDelayedTask(TID_UI, base::BindOnce(task, weak), ms); };
  at(500, [](std::weak_ptr<State> weak) { if(auto s=weak.lock();s&&s->workspace)s->workspace->SetSidebarWidth(336,false,[](int,const std::string&,CefRefPtr<CefDictionaryValue>){}); });
  at(1000, [](std::weak_ptr<State> weak) { PageSize(weak, "before"); if (auto s = weak.lock(); s && s->window) s->window->Minimize(); });
  // A media/title update can publish while minimized. Force that race rather
  // than depending on the fixture to emit a title change at the right instant.
  at(1800, [](std::weak_ptr<State> weak) { if(auto s=weak.lock();s&&s->workspace&&s->content)s->workspace->OnPageChanged(s->content); });
  at(2200, [](std::weak_ptr<State> weak) { PageSize(weak, "minimized"); if (auto s = weak.lock(); s && s->window) s->window->Restore(); });
  at(3200, [](std::weak_ptr<State> weak) { PageSize(weak, "restored"); });
  at(3600, [](std::weak_ptr<State> weak) { if(auto s=weak.lock();s&&s->window)s->window->Maximize(); });
  at(4200, [](std::weak_ptr<State> weak) { PageSize(weak,"max_before");if(auto s=weak.lock();s&&s->window)s->window->Minimize(); });
  at(4800, [](std::weak_ptr<State> weak) { if(auto s=weak.lock();s&&s->workspace&&s->content)s->workspace->OnPageChanged(s->content); });
  at(5200, [](std::weak_ptr<State> weak) { PageSize(weak,"max_minimized");if(auto s=weak.lock();s&&s->window)s->window->Restore(); });
  at(6400, [](std::weak_ptr<State> weak) { PageSize(weak,"max_restored"); });
  at(7000, [](std::weak_ptr<State> weak) { if(auto s=weak.lock();s&&s->window)s->window->Restore(); });
  at(7500, [](std::weak_ptr<State> weak) { PageSize(weak,"repeat_before");if(auto s=weak.lock();s&&s->window)s->window->Minimize(); });
  at(8100, [](std::weak_ptr<State> weak) { if(auto s=weak.lock();s&&s->workspace&&s->content)s->workspace->OnPageChanged(s->content); });
  at(8500, [](std::weak_ptr<State> weak) { PageSize(weak,"repeat_minimized");if(auto s=weak.lock();s&&s->window)s->window->Restore(); });
  at(9500, [](std::weak_ptr<State> weak) { PageSize(weak,"repeat_restored"); });
  at(10000, [](std::weak_ptr<State> weak) { if(auto s=weak.lock())s->link_headers_done=true; });
}
// Unload mode (with --probe-unload: 3 s idle limit): the source tab goes to
// the background, must be unloaded (not live, still listed), and must load
// again when selected.
void SourceLive(std::weak_ptr<State> weak, const char* name) {
  auto state = weak.lock(); if (!state || !state->workspace) return;
  bool listed = false, live = false;
  if (const auto snapshot = state->workspace->Snapshot()) if (const auto nodes = snapshot->GetList("nodes"))
    for (size_t i = 0; i < nodes->GetSize(); ++i) if (const auto node = nodes->GetDictionary(i);
        node && node->GetString("currentUrl").ToString().find("/source") != std::string::npos) { listed = !node->GetBool("archived"); live = node->GetBool("live"); }
  auto data = CefDictionaryValue::Create(); data->SetString("step", name); data->SetBool("listed", listed); data->SetBool("live", live);
  state->Record("unload_probe_step", data);
}
void Unload(std::weak_ptr<State> weak, std::string other) {
  const auto at = [&](int ms, auto task) { CefPostDelayedTask(TID_UI, base::BindOnce(task, weak, other), ms); };
  at(1000, [](std::weak_ptr<State> weak, std::string) { SourceLive(weak, "start"); });
  at(1100, [](std::weak_ptr<State> weak, std::string other) { if (auto s = weak.lock(); s && s->workspace) s->workspace->OpenLink(other, true); });
  at(10000, [](std::weak_ptr<State> weak, std::string) { SourceLive(weak, "idle"); if (auto s = weak.lock(); s && s->workspace) s->workspace->CycleTab(true); });
  at(12500, [](std::weak_ptr<State> weak, std::string) { SourceLive(weak, "back"); });
  at(13000, [](std::weak_ptr<State> weak, std::string) { if (auto s = weak.lock()) s->link_headers_done = true; });
}
// Print mode: window.print() with a user gesture, then the native windows
// under the root (class, size) to see what print preview shows.
void PrintWindows(std::weak_ptr<State> weak, const char* name) {
  auto state = weak.lock(); if (!state || !state->window) return;
  auto list = CefListValue::Create();
  EnumChildWindows(state->window->GetWindowHandle(), [](HWND window, LPARAM param) -> BOOL {
    auto* rows = reinterpret_cast<CefListValue*>(param); if (!IsWindowVisible(window)) return TRUE;
    wchar_t name[64] = {}; GetClassNameW(window, name, 64); RECT box{}; GetWindowRect(window, &box);
    auto row = CefDictionaryValue::Create(); row->SetString("class", CefString(name));
    row->SetInt("w", box.right - box.left); row->SetInt("h", box.bottom - box.top); rows->SetDictionary(rows->GetSize(), row);
    return TRUE;
  }, reinterpret_cast<LPARAM>(list.get()));
  auto data = CefDictionaryValue::Create(); data->SetString("step", name); data->SetList("windows", list);
  state->Record("print_probe_windows", data);
}
void Print(std::weak_ptr<State> weak, CefRefPtr<CefBrowser> page) {
  CefPostDelayedTask(TID_UI, base::BindOnce([](std::weak_ptr<State> weak, CefRefPtr<CefBrowser> page) {
    PrintWindows(weak, "before");
    auto params = CefDictionaryValue::Create(); params->SetString("expression", "setTimeout(()=>window.print(),0)"); params->SetBool("userGesture", true);
    page->GetHost()->ExecuteDevToolsMethod(0, "Runtime.evaluate", params);
  }, weak, page), 1500);
  CefPostDelayedTask(TID_UI, base::BindOnce([](std::weak_ptr<State> weak) { PrintWindows(weak, "after"); }, weak), 7000);
  CefPostDelayedTask(TID_UI, base::BindOnce([](std::weak_ptr<State> weak) { if (auto s = weak.lock()) s->link_headers_done = true; }, weak), 7500);
}
// Peek menu mode: open Link Peek, right-click inside it, record whether the
// page asked for a menu and which top-level windows of Red are visible.
void TopWindows(std::weak_ptr<State> weak, const char* name) {
  auto state = weak.lock(); if (!state) return;
  auto list = CefListValue::Create();
  EnumWindows([](HWND window, LPARAM param) -> BOOL {
    auto* rows = reinterpret_cast<CefListValue*>(param); DWORD pid = 0; GetWindowThreadProcessId(window, &pid);
    if (pid != GetCurrentProcessId() || !IsWindowVisible(window)) return TRUE;
    wchar_t name[64] = {}; GetClassNameW(window, name, 64); RECT box{}; GetWindowRect(window, &box);
    auto row = CefDictionaryValue::Create(); row->SetString("class", CefString(name));
    row->SetInt("w", box.right - box.left); row->SetInt("h", box.bottom - box.top); rows->SetDictionary(rows->GetSize(), row);
    return TRUE;
  }, reinterpret_cast<LPARAM>(list.get()));
  auto data = CefDictionaryValue::Create(); data->SetString("step", name); data->SetList("windows", list);
  state->Record("peek_menu_windows", data);
}
void PeekMenu(std::weak_ptr<State> weak, CefRefPtr<CefBrowser> source) {
  CefPostDelayedTask(TID_UI, base::BindOnce([](std::weak_ptr<State> weak) {
    auto s = weak.lock(); if (!s || !s->content) return;
    TopWindows(weak, "before");
    for (const char* type : {"mousePressed", "mouseReleased"}) {
      auto params = CefDictionaryValue::Create(); params->SetString("type", type);
      params->SetDouble("x", 300); params->SetDouble("y", 200); params->SetString("button", "right"); params->SetInt("clickCount", 1);
      s->content->GetHost()->ExecuteDevToolsMethod(0, "Input.dispatchMouseEvent", params);
    }
    auto data = CefDictionaryValue::Create(); data->SetBool("peek", s->workspace && s->workspace->HasLinkPeek()); s->Record("peek_menu_clicked", data);
  }, weak), 3200);
  CefPostDelayedTask(TID_UI, base::BindOnce([](std::weak_ptr<State> weak) { TopWindows(weak, "early"); }, weak), 3350);
  CefPostDelayedTask(TID_UI, base::BindOnce([](std::weak_ptr<State> weak) { TopWindows(weak, "after"); }, weak), 4200);
  CefPostDelayedTask(TID_UI, base::BindOnce([](std::weak_ptr<State> weak) { if (auto s = weak.lock()) s->link_headers_done = true; }, weak), 4800);
}
// Tab menu mode: the sidebar tab menu actions on real tabs. Opens three more
// Today tabs (/t1../t3), then: archive below the first, archive the others,
// copy a link, pin the source, replace its saved address with the current
// one, edit address and title, and move it to a new space. Each step records
// the tree (Today titles, archived count, pinned url/title, spaces).
void TabMenuStep(std::weak_ptr<State> weak, const char* name, int code = 0) {
  auto s = weak.lock(); if (!s || !s->workspace) return;
  auto data = CefDictionaryValue::Create(); data->SetString("step", name); data->SetInt("code", code);
  int today = 0, archived = 0; std::string pinned_url, pinned_title;
  const auto snapshot = s->workspace->Snapshot();
  if (const auto nodes = snapshot->GetList("nodes")) for (size_t i = 0; i < nodes->GetSize(); ++i) {
    const auto n = nodes->GetDictionary(i); if (n->GetString("kind") != "tab") continue;
    if (n->GetBool("pinned")) { pinned_url = n->GetString("url"); pinned_title = n->GetString("title"); }
    else if (!n->GetBool("favorite") && !n->GetBool("closing")) ++today;
  }
  archived = s->workspace->ArchivedCount();  // Archived tabs are outside the snapshot.
  data->SetInt("today", today); data->SetInt("archived", archived); data->SetString("pinned_url", pinned_url); data->SetString("pinned_title", pinned_title);
  data->SetInt("spaces", snapshot->GetList("spaces") ? static_cast<int>(snapshot->GetList("spaces")->GetSize()) : 0);
  s->Record("tab_menu_step", data);
}
std::string NodeWithUrl(const std::shared_ptr<State>& s, const std::string& part) {
  const auto snapshot = s->workspace->Snapshot();  // Keeps the list alive.
  const auto nodes = snapshot->GetList("nodes");
  for (size_t i = 0; nodes && i < nodes->GetSize(); ++i) if (const auto n = nodes->GetDictionary(i);
      !n->GetBool("archived") && n->GetString("currentUrl").ToString().find(part) != std::string::npos) return n->GetString("id");
  return "";
}
void TabMenu(std::weak_ptr<State> weak, std::string base) {
  const auto at = [&](int ms, auto task) { CefPostDelayedTask(TID_UI, base::BindOnce(task, weak, base), ms); };
  at(800, [](std::weak_ptr<State> weak, std::string base) { if (auto s = weak.lock()) for (const char* t : {"/t1", "/t2", "/t3"}) s->workspace->OpenLink(base + t, false); });
  at(4000, [](std::weak_ptr<State> weak, std::string) { TabMenuStep(weak, "opened"); });
  at(4200, [](std::weak_ptr<State> weak, std::string) { auto s = weak.lock(); std::string e; TabMenuStep(weak, "below_call", s->workspace->ArchiveTodayTabs(NodeWithUrl(s, "/t1"), true, e)); });
  at(9000, [](std::weak_ptr<State> weak, std::string) { TabMenuStep(weak, "below"); auto s = weak.lock(); std::string e; s->workspace->ArchiveTodayTabs(NodeWithUrl(s, "/t1"), false, e); });
  at(11000, [](std::weak_ptr<State> weak, std::string) { TabMenuStep(weak, "others"); auto s = weak.lock(); std::string e;
    TabMenuStep(weak, "copy", s->workspace->CopyTabLink(NodeWithUrl(s, "/t1"), e)); s->workspace->TogglePin(); });
  at(12000, [](std::weak_ptr<State> weak, std::string base) { auto s = weak.lock(); std::string e;
    TabMenuStep(weak, "pinned"); if (auto page = s->content) page->GetMainFrame()->LoadURL(base + "/source?moved"); });
  at(13500, [](std::weak_ptr<State> weak, std::string) { auto s = weak.lock(); std::string e;
    TabMenuStep(weak, "home_call", s->workspace->SetPinnedPage(NodeWithUrl(s, "/source"), "", "", e)); });
  at(14500, [](std::weak_ptr<State> weak, std::string base) { auto s = weak.lock(); std::string e; TabMenuStep(weak, "home");
    TabMenuStep(weak, "edit_call", s->workspace->SetPinnedPage(NodeWithUrl(s, "/source"), base + "/edited", "Edited title", e)); });
  at(15500, [](std::weak_ptr<State> weak, std::string) { auto s = weak.lock(); std::string e; TabMenuStep(weak, "edit");
    TabMenuStep(weak, "space_call", s->workspace->MoveToNewSpace(NodeWithUrl(s, "/source"), e)); });
  at(17000, [](std::weak_ptr<State> weak, std::string) { TabMenuStep(weak, "space"); });
  at(17500, [](std::weak_ptr<State> weak, std::string) { if (auto s = weak.lock()) s->link_headers_done = true; });
}
// Inspect mode: docked DevTools whose frontend goes away on its own (its
// renderer crashes, then it closes itself) must not close the browser window.
CefRefPtr<CefBrowser> Frontend(const std::shared_ptr<State>& s, int page) {
  for (const auto& [id, target] : s->devtools_targets)
    if (target == page) if (auto at = s->browsers.find(id); at != s->browsers.end()) return at->second;
  return nullptr;
}
void InspectStep(std::weak_ptr<State> weak, CefRefPtr<CefBrowser> page, std::string step) {
  auto s = weak.lock(); if (!s) return;
  auto data = CefDictionaryValue::Create(); data->SetString("step", step);
  data->SetBool("window", s->window != nullptr); data->SetBool("devtools", page->IsValid() && page->GetHost()->HasDevTools());
  data->SetBool("frontend", Frontend(s, page->GetIdentifier()) != nullptr); s->Record("inspect_step", data);
}
void Inspect(std::weak_ptr<State> weak, CefRefPtr<CefBrowser> page) {
  auto at = [&](int ms, void (*step)(std::weak_ptr<State>, CefRefPtr<CefBrowser>)) {
    CefPostDelayedTask(TID_UI, base::BindOnce(step, weak, page), ms);
  };
  at(3000, [](std::weak_ptr<State> weak, CefRefPtr<CefBrowser> page) {
    if (auto s = weak.lock()) ShowPageDevTools(s, page); InspectStep(weak, page, "open_call"); });
  at(7000, [](std::weak_ptr<State> weak, CefRefPtr<CefBrowser> page) {
    InspectStep(weak, page, "opened");
    if (auto s = weak.lock()) if (auto tools = Frontend(s, page->GetIdentifier())) tools->GetMainFrame()->LoadURL("chrome://crash"); });
  at(11000, [](std::weak_ptr<State> weak, CefRefPtr<CefBrowser> page) {
    InspectStep(weak, page, "crashed");
    if (auto s = weak.lock()) ShowPageDevTools(s, page); });
  at(15000, [](std::weak_ptr<State> weak, CefRefPtr<CefBrowser> page) {
    InspectStep(weak, page, "reopened");
    // The frontend closing from its own side, as its Ctrl+W does.
    if (auto s = weak.lock()) ClosePageDevTools(s, page->GetIdentifier()); });
  at(19000, [](std::weak_ptr<State> weak, CefRefPtr<CefBrowser> page) {
    InspectStep(weak, page, "closed");
    if (auto s = weak.lock()) s->link_headers_done = true; });
}
// Zoom mode: Ctrl+wheel (native CEF wheel injection with Ctrl) on the page
// zooms it through Red's handler; on the sidebar it must not zoom Red's UI.
void CtrlWheel(CefRefPtr<CefBrowser> browser, int x, int y) {
  CefMouseEvent event; event.x = x; event.y = y; event.modifiers = EVENTFLAG_CONTROL_DOWN;
  browser->GetHost()->SendMouseMoveEvent(event, false);
  browser->GetHost()->SendMouseWheelEvent(event, 0, 120);
}
void CtrlKey(CefRefPtr<CefBrowser> browser, int key) {
  CefKeyEvent event; event.windows_key_code = key; event.modifiers = EVENTFLAG_CONTROL_DOWN;
  event.type = KEYEVENT_RAWKEYDOWN; browser->GetHost()->SendKeyEvent(event);
  event.type = KEYEVENT_KEYUP; browser->GetHost()->SendKeyEvent(event);
}
void ZoomStep(std::weak_ptr<State> weak, CefRefPtr<CefBrowser> page, std::string step) {
  auto s = weak.lock(); if (!s) return;
  auto data = CefDictionaryValue::Create(); data->SetString("step", step);
  data->SetDouble("page", page->GetHost()->GetZoomLevel());
  if (auto shell = s->browsers.find(s->shell_id); shell != s->browsers.end()) data->SetDouble("shell", shell->second->GetHost()->GetZoomLevel());
  s->Record("zoom_step", data);
}
void Zoom(std::weak_ptr<State> weak, CefRefPtr<CefBrowser> page) {
  auto at = [&](int ms, void (*step)(std::weak_ptr<State>, CefRefPtr<CefBrowser>)) {
    CefPostDelayedTask(TID_UI, base::BindOnce(step, weak, page), ms);
  };
  at(2000, [](std::weak_ptr<State> weak, CefRefPtr<CefBrowser> page) { ZoomStep(weak, page, "start"); CtrlWheel(page, 300, 300); });
  at(2600, [](std::weak_ptr<State>, CefRefPtr<CefBrowser> page) { CtrlWheel(page, 300, 300); });
  at(4000, [](std::weak_ptr<State> weak, CefRefPtr<CefBrowser> page) {
    ZoomStep(weak, page, "page_wheel");
    if (auto s = weak.lock()) if (auto shell = s->browsers.find(s->shell_id); shell != s->browsers.end()) {
      CtrlWheel(shell->second, 100, 300); CtrlWheel(shell->second, 100, 320);
    } });
  at(6000, [](std::weak_ptr<State> weak, CefRefPtr<CefBrowser> page) {
    ZoomStep(weak, page, "shell_wheel");
    CtrlKey(page, '0'); });
  at(8000, [](std::weak_ptr<State> weak, CefRefPtr<CefBrowser> page) {
    ZoomStep(weak, page, "reset");
    if (auto s = weak.lock()) s->link_headers_done = true; });
}
// Transfer mode: the tab's page changes its address in place (replaceState,
// no request) and scrolls, then moves to a new window. The server's request
// count (no second /source) and the browser ID prove it moved live.
void TreeRequest(const std::shared_ptr<State>& s, CefRefPtr<CefDictionaryValue> request) {
  request->SetDouble("expectedRevision", s->workspace->Snapshot()->GetDouble("treeRevision"));
  s->workspace->Handle(request, [](int, const std::string&, CefRefPtr<CefDictionaryValue>) {});
}
void TransferStep(std::weak_ptr<State> weak, CefRefPtr<CefBrowser> page, std::string step) {
  auto s = weak.lock(); if (!s || !s->session) return;
  auto data = CefDictionaryValue::Create(); data->SetString("step", step);
  data->SetInt("browser_id", page->GetIdentifier()); data->SetBool("valid", page->IsValid());
  data->SetString("url", page->IsValid() ? page->GetMainFrame()->GetURL().ToString() : "");
  int windows = 0, holder = -1, index = 0;
  for (const auto& window : s->session->Windows()) {
    if (!window->window) continue;
    if (window->browsers.contains(page->GetIdentifier())) {
      holder = index;
      const HWND handle = page->GetHost()->GetWindowHandle(); RECT r{};
      GetWindowRect(handle, &r);
      data->SetBool("active", window->content && window->content->IsSame(page)); data->SetBool("page_visible", IsWindowVisible(handle) != FALSE);
      data->SetBool("window_visible", IsWindowVisible(window->window->GetWindowHandle()) != FALSE);
      data->SetBool("shown", window->content && window->content->IsSame(page) && IsWindowVisible(handle) && IsWindowVisible(window->window->GetWindowHandle()));
      data->SetInt("left", r.left); data->SetInt("top", r.top);
      data->SetBool("parented", GetAncestor(handle, GA_ROOT) == window->window->GetWindowHandle());
      data->SetInt("width", r.right - r.left); data->SetInt("height", r.bottom - r.top);
    }
    ++windows; ++index;
  }
  data->SetInt("windows", windows); data->SetInt("holder", holder);
  data->SetBool("source_peek", s->workspace->HasLinkPeek());
  index = 0;
  for (const auto& window : s->session->Windows()) {
    if (!window->window) continue;
    if (index == holder && window->workspace) data->SetBool("holder_peek", window->workspace->HasLinkPeek());
    ++index;
  }
  data->SetBool("in_source", s->browsers.contains(page->GetIdentifier()));
  data->SetBool("source_entry", !NodeWithUrl(s, "/source").empty());
  data->SetBool("other_entry", !NodeWithUrl(s, "/other").empty());
  data->SetBool("source_content", s->content && s->content->IsSame(page));
  s->Record("transfer_step", data);
}
void Transfer(std::weak_ptr<State> weak, CefRefPtr<CefBrowser> page) {
  auto at = [&](int ms, void (*step)(std::weak_ptr<State>, CefRefPtr<CefBrowser>)) {
    CefPostDelayedTask(TID_UI, base::BindOnce(step, weak, page), ms);
  };
  at(1500, [](std::weak_ptr<State> weak, CefRefPtr<CefBrowser> page) {
    // A second tab so the source window keeps a page after the move.
    if (auto s = weak.lock()) {
      const auto url = page->GetMainFrame()->GetURL().ToString();
      auto open = CefDictionaryValue::Create(); open->SetString("method", "tree.createTab"); open->SetString("parentId", "");
      open->SetString("title", "Other"); open->SetString("url", url.substr(0, url.find("/source")) + "/other");
      TreeRequest(s, open);
    }
    page->GetMainFrame()->ExecuteJavaScript("history.replaceState(null,'','/source?token=red42');scrollTo(0,300)", "", 0); });
  at(4000, [](std::weak_ptr<State> weak, CefRefPtr<CefBrowser> page) {
    TransferStep(weak, page, "before");
    if (auto s = weak.lock()) {
      auto activate = CefDictionaryValue::Create(); activate->SetString("method", "tree.activate"); activate->SetString("id", NodeWithUrl(s, "/source"));
      TreeRequest(s, activate);
    } });
  at(4800, [](std::weak_ptr<State> weak, CefRefPtr<CefBrowser> page) {
    // A Link Peek open over the tab moves with it.
    if (auto s = weak.lock()) {
      const auto url = page->GetMainFrame()->GetURL().ToString();
      auto d = CefDictionaryValue::Create(); d->SetBool("ok", s->workspace->RouteLinkPeek(page, url.substr(0, url.find("/source")) + "/peek", true));
      s->Record("transfer_peek_call", d);
    } });
  at(7000, [](std::weak_ptr<State> weak, CefRefPtr<CefBrowser> page) {
    TransferStep(weak, page, "peeked");
    if (auto s = weak.lock()) { auto d = CefDictionaryValue::Create(); d->SetBool("ok", s->workspace->MoveToNewWindow(NodeWithUrl(s, "/source"))); s->Record("transfer_call", d); } });
  at(11000, [](std::weak_ptr<State> weak, CefRefPtr<CefBrowser> page) {
    TransferStep(weak, page, "after");
    // The page still runs its script in the new window.
    if (page->IsValid()) page->GetMainFrame()->ExecuteJavaScript("fetch('/alive?y='+scrollY)", "", 0); });
  at(13000, [](std::weak_ptr<State> weak, CefRefPtr<CefBrowser> page) {
    TransferStep(weak, page, "alive");
    if (auto s = weak.lock()) for (const auto& window : s->session->Windows())
      if (window != s && window->window) window->window->Close(); });
  at(16000, [](std::weak_ptr<State> weak, CefRefPtr<CefBrowser> page) {
    TransferStep(weak, page, "closed");
    // Next: an ordinary second window (shared tabs, as Ctrl+N) receives the
    // other tab live; its entry stays listed in both windows.
    if (auto s = weak.lock(); s && s->session->new_window) s->session->new_window(s, ""); });
  at(21000, [](std::weak_ptr<State> weak, CefRefPtr<CefBrowser>) {
    auto s = weak.lock(); if (!s) return;
    const auto id = NodeWithUrl(s, "/other");
    const auto other = s->workspace->LiveBrowser(id);
    std::shared_ptr<State> target;
    for (const auto& window : s->session->Windows()) if (window != s && window->window && window->workspace) target = window;
    auto d = CefDictionaryValue::Create(); d->SetBool("target", target != nullptr); d->SetInt("browser_id", other ? other->GetIdentifier() : -1);
    d->SetBool("ok", target && other && s->session->transfer_page(s, target, id, id, false));
    s->Record("shared_transfer_call", d);
    if (other) CefPostDelayedTask(TID_UI, base::BindOnce([](std::weak_ptr<State> weak, CefRefPtr<CefBrowser> other) {
      TransferStep(weak, other, "shared");
      if (auto s = weak.lock()) for (const auto& window : s->session->Windows()) if (window != s && window->window) window->window->Close();
    }, weak, other), 2500); });
  at(27000, [](std::weak_ptr<State> weak, CefRefPtr<CefBrowser> page) {
    if (auto s = weak.lock()) s->link_headers_done = true; });
}
}  // namespace
// Clicks the fixture's fixed link (at 60,60) with a real input gesture. What
// the target server receives is the evidence; nothing is read from the page.
void OnLinkHeadersProbeLoad(const std::shared_ptr<State>& state, CefRefPtr<CefBrowser> page, const std::string& url) {
  // The password-change fixture reuses one persistent profile. Session restore
  // may first open its previous login page; explicitly navigate that disposable
  // tab to this launch's requested stage before starting the input sequence.
  if (state->probe && state->passwords_probe && state->link_headers_phase == 0 &&
      state->initial_url.starts_with("http://127.0.0.1:") &&
      (state->initial_url.ends_with("/source?passwords-update") || state->initial_url.ends_with("/source?passwords-updated")) &&
      url != state->initial_url) {
    page->GetMainFrame()->LoadURL(state->initial_url); return;
  }
  if (state->link_headers_probe && state->link_headers_inspect && state->link_headers_phase == 0 && url.find("/source") != std::string::npos) {
    state->link_headers_phase = 1; Inspect(std::weak_ptr<State>(state), page); return;
  }
  if (!state->link_headers_probe || state->link_headers_phase != 0 || url.find("/source") == std::string::npos) return;
  state->link_headers_phase = 1;
  if (url.ends_with("/source?utility-windows")) { StartUtilityWindowsProbe(state); return; }
  if (url.ends_with("/source?passwords-update") || url.ends_with("/source?passwords-updated") || url.ends_with("/source?passwords-move") || url.ends_with("/source?passwords-link") || url.ends_with("/source?passwords-dismiss") || url.ends_with("/source?passwords-save") || url.ends_with("/source?passwords-check")) { StartPasswordsProbe(state, page, url); return; }
  if (url.ends_with("/source?profiles")) { StartProfilesProbe(state, url); return; }
  if (state->devtools_regression) { StartDevToolsProbe(state, page); return; }
  if (state->platform_codecs_probe) { StartPlatformCodecs(state, page); return; }
  if (url.ends_with("/source?status")) { LinkStatus(std::weak_ptr<State>(state),page); return; }
  if (url.ends_with("/source?motion")) { ProbePageContainerMotion(state,page); return; }
  if (url.ends_with("/source?background")) { ProbePageBackground(state, page); return; }
  if (state->link_headers_minimize) { Minimize(std::weak_ptr<State>(state)); return; }
  if (state->link_headers_print) { Print(std::weak_ptr<State>(state), page); return; }
  if (state->link_headers_zoom) { Zoom(std::weak_ptr<State>(state), page); return; }
  if (state->link_headers_transfer) { Transfer(std::weak_ptr<State>(state), page); return; }
  if (state->link_headers_tabmenu) { TabMenu(std::weak_ptr<State>(state), url.substr(0, url.find("/source"))); return; }
  if (state->link_headers_unload) { Unload(std::weak_ptr<State>(state), url.substr(0, url.find("/source")) + "/other"); return; }
  // Link Peek mode: links from a pinned tab to another site open the preview.
  if (url.ends_with("/source?peek") && state->workspace) state->workspace->TogglePin();
  if (state->link_headers_menu) { CefPostDelayedTask(TID_UI, base::BindOnce(&Click, page), 1200); PeekMenu(std::weak_ptr<State>(state), page); return; }
  CefPostDelayedTask(TID_UI, base::BindOnce(&Click, page), 1200);
  if (state->link_headers_park) {
    const auto end = url.find("/source"); Park(std::weak_ptr<State>(state), url.substr(0, end) + "/other"); return;
  }
  if (state->link_headers_cycles) {
    CefPostDelayedTask(TID_UI, base::BindOnce(&Cycle, std::weak_ptr<State>(state), page, state->link_headers_cycles), 1200);
    return;
  }
  CefPostDelayedTask(TID_UI, base::BindOnce([](std::weak_ptr<State> weak) {
    auto state = weak.lock(); if (!state) return;
    auto data = CefDictionaryValue::Create(); data->SetBool("link_peek", state->workspace && state->workspace->HasLinkPeek());
    state->Record("link_headers_opened", data);
  }, std::weak_ptr<State>(state)), 4000);
  CefPostDelayedTask(TID_UI, base::BindOnce([](std::weak_ptr<State> weak) {
    if (auto state = weak.lock()) state->link_headers_done = true;
  }, std::weak_ptr<State>(state)), 6000);
}
}  // namespace red
