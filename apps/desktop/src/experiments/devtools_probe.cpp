#include "experiments/devtools_probe.h"
#include "windows/devtools.h"
#include "windows/devtools_dock.h"
#include "windows/devtools_window.h"
#include "windows/embedded_content.h"
#include "experiments/shell_capture.h"
#include "workspace/controller.h"
#include "windows/focus.h"
#include "history/favicon_cache.h"
#include "include/cef_devtools_message_observer.h"
#include "include/cef_parser.h"
#include "include/base/cef_callback.h"
#include "include/wrapper/cef_closure_task.h"
namespace red {
namespace {
class Observer final : public CefDevToolsMessageObserver {
 public:
  explicit Observer(std::weak_ptr<State> state) : state_(std::move(state)) {}
  void OnDevToolsEvent(CefRefPtr<CefBrowser>, const CefString& method, const void* data, size_t size) override {
    if (method != "Network.requestWillBeSent" || size > 65536 || count_ >= 256) return;
    const auto parsed = CefParseJSON(static_cast<const char*>(data), size, JSON_PARSER_RFC);
    const auto state = state_.lock(); if (!state || !parsed || parsed->GetType() != VTYPE_DICTIONARY) return;
    const auto request = parsed->GetDictionary()->GetDictionary("request"); if (!request) return;
    const auto url = request->GetString("url").ToString();
    const auto slash = url.find('/', url.find("://") + 3);
    const auto path = slash == std::string::npos ? "" : url.substr(slash);
    if (!path.starts_with("/icon") && !path.starts_with("/source")) return;
    ++count_; auto out = CefDictionaryValue::Create(); out->SetString("path", path);
    state->Record("devtools_probe_request", out);
  }
  void OnDevToolsMethodResult(CefRefPtr<CefBrowser>, int, bool ok, const void* data, size_t size) override {
    if (size > 16384) return;
    if (!ok) { if(auto s=state_.lock()){auto d=CefDictionaryValue::Create();d->SetString("error",std::string(static_cast<const char*>(data),size));s->Record("devtools_probe_protocol_error",d);}return; }
    const auto parsed = CefParseJSON(static_cast<const char*>(data), size, JSON_PARSER_RFC);
    if (!parsed || parsed->GetType() != VTYPE_DICTIONARY) return;
    const auto result = parsed->GetDictionary()->GetDictionary("result");
    const auto value = result ? result->GetDictionary("value") : nullptr;
    if (value && value->GetBool("red_devtools_probe")) if (auto state = state_.lock()) {
      const auto kind = value->GetString("kind");
      state->Record(kind == "shell" ? "devtools_probe_shell" : kind == "frontend" ? "devtools_probe_frontend" :
                    kind == "window" ? "devtools_probe_window" : "devtools_probe_page", value->Copy(false));
    }
  }
 private:
  int count_ = 0;
  std::weak_ptr<State> state_;
  IMPLEMENT_REFCOUNTING(Observer);
};
void Evaluate(CefRefPtr<CefBrowser> page, const std::string& expression, bool await_promise = false) {
  if (!page || !page->IsValid()) return;
  auto p = CefDictionaryValue::Create(); p->SetString("expression", expression);
  p->SetBool("returnByValue", true); p->SetBool("userGesture", true); p->SetBool("awaitPromise", await_promise);
  page->GetHost()->ExecuteDevToolsMethod(0, "Runtime.evaluate", p);
}
CefRefPtr<CefBrowser> Shell(const std::shared_ptr<State>& s) {
  const auto it=s->browsers.find(s->shell_id);
  return it==s->browsers.end() ? nullptr : it->second;
}
void Mouse(CefRefPtr<CefBrowser> shell,const char* type,double x,double y,bool held=false) {
  if (!shell) return;
  auto p=CefDictionaryValue::Create();p->SetString("type",type);p->SetDouble("x",x);p->SetDouble("y",y);
  p->SetString("button",std::string(type)=="mouseMoved" && !held ? "none" : "left");
  p->SetInt("buttons",std::string(type)=="mousePressed" || held ? 1 : 0);p->SetInt("clickCount",1);
  shell->GetHost()->ExecuteDevToolsMethod(0,"Input.dispatchMouseEvent",p);
}
void ResizeRoot(const std::shared_ptr<State>& state, int width, int height) {
  auto bounds = state->window->GetBounds();
  bounds.width = width; bounds.height = height;
  state->window->SetBounds(bounds);
}
void TestDockInput(const std::shared_ptr<State>& state, std::string phase) {
  const auto dock = state->devtools_dock->Describe();
  if (!dock) return;
  const int id = dock->GetInt("id");
  const auto found = state->browsers.find(id);
  if (found == state->browsers.end()) return;
  const auto frontend = found->second;
  const HWND root = state->window->GetWindowHandle();
  const HWND handle = frontend->GetHost()->GetWindowHandle();
  RECT rect{};
  if (!GetWindowRect(handle, &rect)) return;
  const POINT point{(rect.left + rect.right) / 2, rect.top + (rect.bottom - rect.top) / 3};
  const auto hit = WindowFromPoint(point);
  const bool same_root = GetAncestor(hit, GA_ROOT) == root;
  auto details = CefDictionaryValue::Create();
  details->SetString("phase", phase);
  details->SetInt("frontend", id);
  details->SetBool("frontend_hit", hit == handle || IsChild(handle, hit));
  details->SetBool("inside_root", same_root);
  details->SetBool("enabled", IsWindowEnabled(handle));
  if (same_root) {
    wchar_t name[128]{};
    GetClassNameW(hit, name, 128);
    details->SetString("hit_class", name);
  }
  state->Record("devtools_probe_hit", details);
  const auto observer = frontend->GetHost()->AddDevToolsMessageObserver(new Observer(state));
  Evaluate(frontend, R"JS((() => {
    const seen = window.solTestPointer = { moves: 0, clicks: 0 };
    addEventListener('pointermove', e => { if (e.isTrusted) ++seen.moves; }, { capture: true, once: true });
    addEventListener('pointerdown', e => { if (e.isTrusted) ++seen.clicks; }, { capture: true, once: true });
  })())JS");
  CefPostDelayedTask(TID_UI, base::BindOnce([](std::weak_ptr<State> weak, CefRefPtr<CefBrowser> frontend,
      CefRefPtr<CefRegistration> observer, HWND root, POINT point, std::string phase) {
    const auto state = weak.lock();
    if (!state || !state->window || GetAncestor(WindowFromPoint(point), GA_ROOT) != root ||
        (GetAsyncKeyState(VK_LBUTTON) & 0x8000)) return;
    POINT previous{};
    if (!GetCursorPos(&previous) || !SetCursorPos(point.x, point.y)) return;
    state->window->SendMouseEvents(MBT_LEFT, true, true);
    CefPostDelayedTask(TID_UI, base::BindOnce([](CefRefPtr<CefBrowser> frontend, CefRefPtr<CefRegistration> observer,
        POINT point, POINT previous, std::string phase) {
      Evaluate(frontend, "({red_devtools_probe:true,kind:'frontend',phase:'" + phase + "',...window.solTestPointer})");
      POINT cursor{};
      if (GetCursorPos(&cursor) && cursor.x == point.x && cursor.y == point.y) SetCursorPos(previous.x, previous.y);
      CefPostDelayedTask(TID_UI, base::BindOnce([](CefRefPtr<CefRegistration>) {}, observer), 350);
    }, frontend, observer, point, previous, std::move(phase)), 130);
  }, std::weak_ptr<State>(state), frontend, observer, root, point, std::move(phase)), 80);
}
void Sample(const std::shared_ptr<State>& s, CefRefPtr<CefBrowser> page, const std::string& phase) {
  auto out = CefDictionaryValue::Create(); out->SetString("phase", phase);
  if (s->favicons)
    if (const auto url = s->favicons->PageUrl(page->GetIdentifier())) out->SetString("favicon", *url);
  if (s->devtools_dock) {
    if (auto dock = s->devtools_dock->Describe()) out->SetDictionary("dock", dock);
    if (auto device = s->devtools_dock->DescribeDevice()) out->SetDictionary("device", device);
  }
  const auto own = s->devtools_dock ? s->devtools_dock->SeparateWindow(page->GetIdentifier()) : nullptr;
  if (own) { out->SetBool("own_window", true); out->SetBool("window_visible", own->hwnd() && IsWindowVisible(own->hwnd())); }
  for (const auto& [id, target] : s->devtools_targets) if (target == page->GetIdentifier()) {
    out->SetInt("frontend", id);
    if (const auto at = s->browsers.find(id); at != s->browsers.end()) {
      const HWND parent = GetParent(at->second->GetHost()->GetWindowHandle());
      out->SetBool("child", parent == s->window->GetWindowHandle());
      if (own) out->SetBool("in_own_window", parent == own->hwnd());
    }
  }
  s->Record("devtools_probe_step", out);
  if (phase=="device" || phase=="detached" || phase=="rotated") {
    auto capture=CaptureShell(Shell(s),s->evidence_directory/("devtools-"+phase+".png"));
    CefPostDelayedTask(TID_UI,base::BindOnce([](CefRefPtr<CefRegistration>) {},capture),700);
  }
  Evaluate(page, "({red_devtools_probe:true,phase:'" + phase + "',width:innerWidth,height:innerHeight,dpr:devicePixelRatio,touch:navigator.maxTouchPoints,mobile:navigator.userAgentData?.mobile,token:window.pageToken})");
  Evaluate(page, "fetch('/mark?phase=" + phase + "')");
}
}
CefRefPtr<CefRegistration> TraceDevToolsProbe(const std::shared_ptr<State>& state, CefRefPtr<CefBrowser> page) {
  auto registration = page->GetHost()->AddDevToolsMessageObserver(new Observer(state));
  page->GetHost()->ExecuteDevToolsMethod(0, "Network.enable", nullptr);
  auto p = CefDictionaryValue::Create(); p->SetBool("cacheDisabled", true);
  page->GetHost()->ExecuteDevToolsMethod(0, "Network.setCacheDisabled", p);
  return registration;
}
void StartDevToolsProbe(const std::shared_ptr<State>& state, CefRefPtr<CefBrowser> page) {
  using Step=std::function<void(std::shared_ptr<State>,CefRefPtr<CefBrowser>)>;
  auto at = [&](int ms, Step fn) {
    CefPostDelayedTask(TID_UI, base::BindOnce([](Step fn, std::weak_ptr<State> weak, CefRefPtr<CefBrowser> page) {
      if (auto s = weak.lock(); s && s->window && page->IsValid()) fn(s, page);
    }, std::move(fn), std::weak_ptr<State>(state), page), ms);
  };
  at(100, [](auto s, auto page) { ShowPageDevTools(s, page); });
  const auto shell_registration=Shell(state)->GetHost()->AddDevToolsMessageObserver(new Observer(state));
  at(25000,[shell_registration](auto,auto) {});
  at(1450, [](auto s, auto) { TestDockInput(s, "initial"); });
  at(1600, [](auto s, auto page) { Sample(s, page, "before_resize"); });
  for (int i = 0; i < 8; ++i) at(1800 + i * 100, [i](auto s, auto page) {
    ResizeRoot(s,1120+(i%2)*60,800+(i%2)*20);
    const auto url=page->GetMainFrame()->GetURL().ToString();
    s->workspace->OnFaviconChanged(page,{url.substr(0,url.rfind('/'))+"/icon.svg"});
  });
  at(3100, [](auto s, auto page) { Sample(s,page,"after_resize"); TestDockInput(s,"resized"); });
  at(3400, [](auto, auto page) { Evaluate(page,"document.querySelector('link[rel=icon]').href='/icon-b.svg'"); });
  at(3510, [](auto s, auto) { s->window->Minimize(); });
  at(3690, [](auto s, auto) {
    auto details = CefDictionaryValue::Create(); details->SetBool("minimized", s->window->IsMinimized());
    s->Record("devtools_probe_minimize", details);
    s->window->Restore(); s->window->Activate();
  });
  at(3940, [](auto s, auto) { TestDockInput(s,"restored"); });
  at(4450, [](auto s, auto page) { Sample(s,page,"icon_changed"); s->devtools_dock->SetSide(page->GetIdentifier(),DevToolsSide::Window); });
  at(5600, [](auto s, auto page) {
    Sample(s,page,"detached");
    // Sol's own DevTools window carries the controls; the browser window has none.
    Evaluate(Shell(s),"({red_devtools_probe:true,kind:'shell',phase:'detached_shell',header:!!document.querySelector('.devtools-header')})");
    const auto own = s->devtools_dock ? s->devtools_dock->SeparateWindow(page->GetIdentifier()) : nullptr;
    if (const auto header = own ? own->header() : nullptr) {
      static CefRefPtr<CefRegistration> window_observer;
      window_observer = header->GetHost()->AddDevToolsMessageObserver(new Observer(s));
      Evaluate(header,"({red_devtools_probe:true,kind:'window',phase:'detached',"
        "buttons:[...document.querySelectorAll('.devtools-window-header button')].map(b=>b.getAttribute('aria-label')),"
        "dockEnabled:document.querySelector('[aria-label=\"Dock right\"]')?.disabled===false,"
        "height:Math.round(document.querySelector('.devtools-window-header')?.getBoundingClientRect().height||0),"
        "href:location.href,ready:document.readyState,body:(document.body?.innerHTML||'').slice(0,300)})");
      CefPostDelayedTask(TID_UI, base::BindOnce([](CefRefPtr<CefBrowser> header) {
        Evaluate(header,"document.querySelector('[aria-label=\"Dock right\"]')?.click()");
      }, header), 250);
    }
  });
  at(6900, [](auto s, auto page) { Sample(s,page,"reattach_button");
    // Continue testing emulation even when the baseline has no button.
    s->devtools_dock->SetSide(page->GetIdentifier(),DevToolsSide::Right);
  });
  at(8000, [](auto s, auto page) { s->devtools_dock->ToggleDevice(page->GetIdentifier()); });
  at(9300, [](auto s, auto page) { Sample(s,page,"device"); });
  for (int i=0;i<6;++i) at(9600+i*120,[i](auto s,auto) { ResizeRoot(s,1100+(i%2)*100,780+(i%2)*40); });
  at(11400, [](auto s,auto page) { Sample(s,page,"device_resized"); });
  at(12000, [](auto s,auto page) {
    Evaluate(Shell(s),"document.querySelector('[aria-label=\"Rotate device\"]')?.click()");
  });
  at(13600, [](auto s,auto page) { Sample(s,page,"rotated"); });
  at(13900, [](auto s,auto) {
    s->window->Activate();FocusShell(s);
    Evaluate(Shell(s),"window.devtoolsEvents=[];['pointerdown','pointermove','pointerup','gotpointercapture','lostpointercapture','blur','focus','error'].forEach(t=>window.addEventListener(t,e=>window.devtoolsEvents.push({type:e.type,x:e.clientX,y:e.clientY,button:e.button,buttons:e.buttons,target:e.target?.getAttribute?.('aria-label'),message:e.message}),true))");
  });
  auto pointer=std::make_shared<std::pair<double,double>>();
  at(14100, [pointer](auto s,auto) {
    const auto d=s->devtools_dock->DescribeDevice();if(!d)return;
    const auto v=d->GetDictionary("viewport");
    pointer->first=v->GetDouble("x")+v->GetDouble("width")+4;
    pointer->second=v->GetDouble("y")+v->GetDouble("height")/2;
    Evaluate(Shell(s),R"JS((() => {
      const handles = [...document.querySelectorAll('.device-resizer')];
      return {red_devtools_probe:true,kind:'shell',phase:'resize_handles',
        accessible:handles.length === 4 && handles.every(handle => handle.tabIndex === 0 && handle.getAttribute('aria-label')),
        tooltips:handles.filter(handle => handle.closest('[title]')).length};
    })())JS");
    Evaluate(Shell(s),"({red_devtools_probe:true,kind:'shell',phase:'pointer',hit:document.elementFromPoint("+std::to_string(pointer->first)+","+std::to_string(pointer->second)+")?.outerHTML,handle:document.querySelector('[aria-label=\"Resize viewport width\"]')?.getBoundingClientRect().toJSON(),size:[innerWidth,innerHeight],errors:document.querySelector('[role=alert]')?.textContent})");
    Mouse(Shell(s),"mouseMoved",pointer->first,pointer->second);
    Mouse(Shell(s),"mousePressed",pointer->first,pointer->second);
  });
  at(14300, [pointer](auto s,auto) { Mouse(Shell(s),"mouseMoved",pointer->first+16,pointer->second,true); });
  at(14500, [pointer](auto s,auto) { Mouse(Shell(s),"mouseReleased",pointer->first+16,pointer->second); });
  at(15100, [](auto s,auto) { Evaluate(Shell(s),"({red_devtools_probe:true,kind:'shell',phase:'events',events:window.devtoolsEvents,dragging:document.querySelector('[aria-label=\"Resize viewport width\"]')?.dataset.dragging})"); });
  at(15900, [](auto s,auto page) { Sample(s,page,"dragged"); });
  at(16200, [](auto s,auto) { Evaluate(Shell(s),"{let e=document.querySelector('[aria-label=\"Preview scale\"]');e.value='0.25';e.dispatchEvent(new Event('change',{bubbles:true}))}"); });
  at(17300, [](auto s,auto page) { Sample(s,page,"scaled"); });
  at(17600, [](auto s,auto) { Evaluate(Shell(s),"{let e=document.querySelector('[aria-label=\"Device pixel ratio\"]');e.value='2';e.dispatchEvent(new Event('change',{bubbles:true}))}"); });
  at(18900, [](auto s,auto page) { Sample(s,page,"dpr_changed"); });
  at(19500, [](auto s,auto page) { s->devtools_dock->ToggleDevice(page->GetIdentifier()); });
  at(20800, [](auto s,auto page) { Sample(s,page,"desktop"); });
  // Divider drag cost: Shell round trip per pointer move, full snapshot path
  // (devtools.resize) vs the live path (devtools.resizeLive), 30 moves each.
  at(21100, [](auto s,auto) {
    const auto dock=s->devtools_dock->Describe(); if(!dock) return;
    const auto id=std::to_string(dock->GetInt("id"));
    Evaluate(Shell(s),"(async()=>{const q=(m,e)=>new Promise((r,j)=>cefQuery({request:JSON.stringify({method:m,id:"+id+",extent:e}),persistent:false,onSuccess:r,onFailure:(c,x)=>j(new Error(c+': '+x))}));"
      "const t=async m=>{try{const s=performance.now();let bytes=0;for(let i=0;i<30;i++)bytes+=(await q(m,400+(i%2)*80)).length;return {ms:(performance.now()-s)/30,bytes:bytes/30}}catch(e){return {error:String(e)}}};"
      "const full=await t('devtools.resize'),live=await t('devtools.resizeLive');await q('devtools.resize',420).catch(()=>{});"
      "return {red_devtools_probe:true,kind:'shell',phase:'resize_cost',full,live}})()",true);
  });
  at(23400, [](auto s,auto page) { ClosePageDevTools(s,page->GetIdentifier()); });
  at(24800, [](auto s,auto page) { Sample(s,page,"closed"); s->link_headers_done=true; });
}
}
