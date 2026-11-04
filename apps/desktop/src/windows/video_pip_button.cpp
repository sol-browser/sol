#include "windows/auto_pip.h"
#include "app/preferences.h"
#include "app/session.h"
#include "include/cef_devtools_message_observer.h"
#include "include/cef_parser.h"
#include "include/base/cef_callback.h"
#include "include/wrapper/cef_closure_task.h"

namespace red {
namespace {
// Runs in an isolated world ("red-pip"): it shares the page's DOM but not its
// JavaScript, so the page cannot call into it or see its variables. A small
// button in a closed shadow root appears at the right edge of a video under
// the pointer (Firefox-style) and toggles Picture-in-Picture; the click is a
// real user gesture. Nothing is read from or sent to the page or to Red.
constexpr char kButton[] = R"JS((()=>{
if (window.top !== window || window.__redPip) return; window.__redPip = true;
const host = document.createElement('red-pip-button');
const root = host.attachShadow({mode:'closed'});
// Built with DOM calls, not innerHTML: sites enforcing Trusted Types
// (YouTube) reject HTML strings, which stopped the script before.
const style = document.createElement('style');
style.textContent = `button{all:unset;position:fixed;z-index:2147483647;display:flex;align-items:center;justify-content:center;width:32px;height:32px;
border-radius:8px;background:rgba(20,20,24,.78);color:#fff;cursor:pointer;opacity:0;transform:scale(.92);pointer-events:none;
transition:opacity .16s ease,transform .2s cubic-bezier(.22,1,.36,1),background .12s ease;box-shadow:0 2px 8px rgba(0,0,0,.35);
backdrop-filter:blur(8px)}
button.on{opacity:1;transform:none;pointer-events:auto}button:hover{background:rgba(48,48,56,.92)}
svg{width:18px;height:18px}`;
const button = document.createElement('button');
button.title = 'Picture-in-picture (Alt+P)'; button.setAttribute('aria-label', 'Picture-in-picture');
const ns = 'http://www.w3.org/2000/svg', icon = document.createElementNS(ns, 'svg');
for (const [k, v] of [['viewBox','0 0 24 24'],['fill','none'],['stroke','currentColor'],['stroke-width','2'],['stroke-linecap','round'],['stroke-linejoin','round']]) icon.setAttribute(k, v);
for (const d of ['M21 10V6a2 2 0 0 0-2-2H5a2 2 0 0 0-2 2v12a2 2 0 0 0 2 2h5', 'm4 8 5 5', 'M9 9v4H5']) {
  const path = document.createElementNS(ns, 'path'); path.setAttribute('d', d); icon.append(path); }
const rect = document.createElementNS(ns, 'rect');
for (const [k, v] of [['x','13'],['y','13'],['width','9'],['height','7'],['rx','1.5'],['fill','currentColor']]) rect.setAttribute(k, v);
icon.append(rect); button.append(icon); root.append(style, button);
let target = null, timer = 0;
const usable = v => v.videoWidth > 0 && !v.disablePictureInPicture && document.pictureInPictureEnabled;
const hide = () => { button.classList.remove('on'); target = null; };
const place = v => { const r = v.getBoundingClientRect();
  button.style.left = Math.round(Math.min(r.right, innerWidth) - 44) + 'px';
  button.style.top = Math.round(Math.max(r.top, 0) + Math.min(r.height, innerHeight) / 2 - 16) + 'px'; };
addEventListener('pointermove', e => {
  if (document.fullscreenElement) return hide();
  if (e.composedPath().includes(button)) { clearTimeout(timer); return; }
  const v = [...document.querySelectorAll('video')].find(v => { const r = v.getBoundingClientRect();
    return r.width >= 200 && r.height >= 120 && e.clientX >= r.left && e.clientX <= r.right && e.clientY >= r.top && e.clientY <= r.bottom && usable(v); });
  if (!v) { if (target) { clearTimeout(timer); timer = setTimeout(hide, 600); } return; }
  target = v; place(v); button.classList.add('on'); clearTimeout(timer); timer = setTimeout(hide, 2500);
}, {passive:true, capture:true});
addEventListener('scroll', hide, {passive:true, capture:true});
button.addEventListener('click', e => { e.stopPropagation(); e.preventDefault(); const v = target; hide();
  if (document.pictureInPictureElement) document.exitPictureInPicture().catch(() => {});
  else if (v) v.requestPictureInPicture().catch(() => {}); }, true);
const attach = () => document.documentElement && !host.isConnected && document.documentElement.append(host);
attach(); new MutationObserver(attach).observe(document, {childList:true});
})())JS";
// Remembers the script's DevTools identifier, so a later registration can
// replace it instead of piling up.
class Observer final : public CefDevToolsMessageObserver {
 public:
  explicit Observer(std::weak_ptr<State> state) : state_(std::move(state)) {}
  void OnDevToolsMethodResult(CefRefPtr<CefBrowser> browser, int id, bool success, const void* data, size_t size) override {
    std::string json; if (success && size && size <= 4096) json.assign(static_cast<const char*>(data), size);
    CefPostTask(TID_UI, base::BindOnce([](std::weak_ptr<State> weak, int browser, int id, std::string json) {
      const auto state = weak.lock(); if (!state || json.empty()) return;
      const auto entry = state->pip_buttons.find(browser);
      if (entry == state->pip_buttons.end() || entry->second.pending != id) return;
      const auto value = CefParseJSON(json, JSON_PARSER_RFC);
      if (value && value->GetType() == VTYPE_DICTIONARY) entry->second.identifier = value->GetDictionary()->GetString("identifier").ToString().substr(0, 64);
    }, state_, browser->GetIdentifier(), id, std::move(json)));
  }
 private:
  std::weak_ptr<State> state_;
  IMPLEMENT_REFCOUNTING(Observer);
};
}  // namespace

// Called when a website page is created and after each of its main-frame
// loads: a registration made before a cross-process navigation (YouTube) is
// lost with the old process, so it is renewed (the previous one removed).
void InstallVideoPipButton(const std::shared_ptr<State>& state, CefRefPtr<CefBrowser> page) {
  if (!state->session || !state->session->preferences || !state->session->preferences->Get().pip_button) return;
  auto& entry = state->pip_buttons[page->GetIdentifier()];
  if (!entry.observer) entry.observer = page->GetHost()->AddDevToolsMessageObserver(new Observer(state));
  if (!entry.identifier.empty()) {
    auto remove = CefDictionaryValue::Create(); remove->SetString("identifier", entry.identifier);
    page->GetHost()->ExecuteDevToolsMethod(0, "Page.removeScriptToEvaluateOnNewDocument", remove);
    entry.identifier.clear();
  }
  auto params = CefDictionaryValue::Create();
  params->SetString("source", kButton); params->SetString("worldName", "red-pip"); params->SetBool("runImmediately", true);
  entry.pending = page->GetHost()->ExecuteDevToolsMethod(0, "Page.addScriptToEvaluateOnNewDocument", params);
}
}  // namespace red
