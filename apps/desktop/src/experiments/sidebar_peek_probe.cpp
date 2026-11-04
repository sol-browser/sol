#include "experiments/sidebar_peek_probe.h"
#include <windows.h>
#include <cstdio>
#include "include/base/cef_callback.h"
#include "include/wrapper/cef_closure_task.h"
#include "windows/sidebar_menu.h"
#include "windows/sidebar_peek.h"
#include "workspace/controller.h"
namespace red {
namespace {
constexpr char kScript[] = R"JS((async()=>{
const q=r=>new Promise((ok,no)=>window.cefQuery({request:JSON.stringify(r),persistent:false,onSuccess:ok,onFailure:c=>no(c)}));
let refused=0;try{await q({method:'navigation.load',url:'https://example.com/'});}catch(c){refused=c;}
const rows=[...document.querySelectorAll('.tree-row .node-title')].map(n=>n.textContent);
const row=[...document.querySelectorAll('.tree-row')].find(r=>r.querySelector('.node-title')?.textContent==='a');
row?.querySelector('.node-name')?.click();
console.info('RED_PEEK:'+JSON.stringify({rows,refused,width:innerWidth,right:document.querySelector('.sidebar')?.getBoundingClientRect().right}));
// The row menu must open from the floating sidebar (the docked one is hidden).
const menu=await q({method:'workspace.contextMenu',id:'',x:20,y:60}).then(()=>'shown',c=>c);
console.info('RED_PEEK:'+JSON.stringify({menu}));
})())JS";
using Step = void (*)(const std::shared_ptr<State>&);
void Later(const std::shared_ptr<State>& state, int delay, Step step) {
  CefPostDelayedTask(TID_UI, base::BindOnce([](std::weak_ptr<State> weak, Step step) {
    if (auto state = weak.lock()) step(state);
  }, std::weak_ptr<State>(state), step), delay);
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
    if (!state->window->IsMaximized()) state->window->SetBounds(CefRect(240, 80, 1100, 820));
    state->workspace->SetSidebarVisible(false);
    Later(state, 700, [](const std::shared_ptr<State>& s) { if (s->sidebar_peek) s->sidebar_peek->Show(); });
    Later(state, 1700, [](const std::shared_ptr<State>& s) {
      Record(s, "peek_state_open");
      if (auto browser = s->sidebar_peek ? s->sidebar_peek->browser() : nullptr)
        browser->GetMainFrame()->ExecuteJavaScript(kScript, browser->GetMainFrame()->GetURL(), 0);
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
            "document.body.appendChild(i);i.focus();i.select();}", browser->GetMainFrame()->GetURL(), 0);
      }
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
  }
}
}  // namespace red
