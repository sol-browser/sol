#include "experiments/lifecycle_probe.h"
#include "workspace/controller.h"
#include "include/base/cef_callback.h"
#include "include/wrapper/cef_closure_task.h"
namespace red {
void ObserveLifecycleProbe(const std::shared_ptr<State>& state, CefRefPtr<CefBrowser> browser, const std::string& message) {
  if (!state->lifecycle_probe) return;
  if (message.starts_with("RED_LIFE")) {
    auto data = CefDictionaryValue::Create(); data->SetString("phase", message);
    data->SetInt("browser_id", browser->GetIdentifier()); state->Record("lifecycle_phase", data);
  }
  if (browser->GetIdentifier() != state->shell_id) {
    if (state->lifecycle_mode == "manual" && state->content && browser->GetIdentifier() == state->content->GetIdentifier() && message == "RED_LIFE:manual-crash") {
      browser->GetHost()->ExecuteDevToolsMethod(0, "Page.crash", CefDictionaryValue::Create()); return;
    }
    if (message == "RED_LIFE:armed:true") state->lifecycle_armed.insert(browser->GetIdentifier());
    if (message == "RED_LIFE:preserved:true") ++state->lifecycle_inspections;
    if (message == "RED_LIFE:input-ready") {
      auto key = CefDictionaryValue::Create(); key->SetString("type", "keyDown");
      key->SetString("key", "a"); key->SetString("code", "KeyA"); key->SetString("text", "a"); key->SetInt("windowsVirtualKeyCode", 'A');
      browser->GetHost()->ExecuteDevToolsMethod(0, "Input.dispatchKeyEvent", key);
      auto up = CefDictionaryValue::Create(); up->SetString("type", "keyUp"); up->SetString("key", "a"); up->SetInt("windowsVirtualKeyCode", 'A');
      browser->GetHost()->ExecuteDevToolsMethod(0, "Input.dispatchKeyEvent", up);
      CefPostDelayedTask(TID_UI, base::BindOnce([](CefRefPtr<CefBrowser> page) {
        page->GetMainFrame()->ExecuteJavaScript(
            "window.__redLife=crypto.randomUUID();document.querySelector('input').value='retained-'+location.pathname;"
            "window.onbeforeunload=e=>{e.preventDefault();e.returnValue='';};"
            "console.info('RED_LIFE:armed:'+navigator.userActivation.hasBeenActive);", page->GetMainFrame()->GetURL(), 0);
      }, browser), 180);
    }
    return;
  }
  if (message == "RED_LIFE:checks-pass") { state->lifecycle_checks = true; state->Record("lifecycle_checks_pass", state->workspace->Snapshot()); return; }
  if (message == "RED_LIFE:hold-ready") { state->Record("lifecycle_hold_ready", state->workspace->Snapshot()); return; }
  if (!state->content) return;
  auto page = state->content;
  if (message == "RED_LIFE:arm") {
    page->GetMainFrame()->ExecuteJavaScript("document.querySelector('input').focus();console.info('RED_LIFE:input-ready');", page->GetMainFrame()->GetURL(), 0);
  } else if (message == "RED_LIFE:inspect") {
    page->GetMainFrame()->ExecuteJavaScript("console.info('RED_LIFE:preserved:'+(!!window.__redLife&&document.querySelector('input').value==='retained-'+location.pathname));", page->GetMainFrame()->GetURL(), 0);
  } else if (message == "RED_LIFE:crash") {
    // Actual owned renderer crash through local CEF DevTools, not a simulated
    // callback or an IPC-supplied script. No production command exposes this.
    page->GetHost()->ExecuteDevToolsMethod(0, "Page.crash", CefDictionaryValue::Create());
  }
}
std::optional<bool> LifecycleUnloadDecision(const std::shared_ptr<State>& state, int browser) {
  if (!state->lifecycle_probe || !state->lifecycle_armed.contains(browser)) return std::nullopt;
  // The full fixture vetoes its single-live-page window close exactly once.
  const bool leave = ++state->lifecycle_unload_decisions > 1;
  auto data = CefDictionaryValue::Create(); data->SetBool("leave", leave);
  state->Record("lifecycle_real_unload_decision", data); return leave;
}
}  // namespace red
