#include "experiments/organization_probe.h"
#include "trust_policy.h"
#include "include/base/cef_callback.h"
#include "include/wrapper/cef_closure_task.h"
#include "include/views/cef_browser_view.h"
#include "workspace/controller.h"
namespace red {
void ObserveOrganizationProbe(const std::shared_ptr<State>& state, CefRefPtr<CefBrowser> browser, const std::string& message) {
  if (!state->organization_probe) return;
  if (browser->GetIdentifier() == state->shell_id && message == "RED_ORG_NAVIGATE_TODAY" && state->content) {
    state->content->GetMainFrame()->ExecuteJavaScript("document.querySelector('a').click();", state->content->GetMainFrame()->GetURL(), 0);
    return;
  }
  if (browser->GetIdentifier() == state->shell_id && message == "RED_ORG_ARM_UNLOAD" && state->content) {
    state->Record("organization_arm_requested");
    state->organization_browser = state->content->GetIdentifier();
    state->content->GetMainFrame()->ExecuteJavaScript(
        "document.querySelector('input').focus();console.info('RED_ORG_INPUT_READY');", state->content->GetMainFrame()->GetURL(), 0);
  } else if (browser->GetIdentifier() == state->organization_browser && message == "RED_ORG_INPUT_READY") {
    state->Record("organization_input_ready");
    const auto view = CefBrowserView::GetForBrowser(browser);
    if (!view || !view->GetWindow()) return;
    view->RequestFocus();
    // Local DevTools input produces a renderer-trusted key event independently
    // of OS foreground arbitration. No remote port or frontend is exposed.
    auto key = CefDictionaryValue::Create(); key->SetString("type", "keyDown");
    key->SetString("key", "a"); key->SetString("code", "KeyA"); key->SetString("text", "a");
    key->SetInt("windowsVirtualKeyCode", 'A');
    browser->GetHost()->ExecuteDevToolsMethod(0, "Input.dispatchKeyEvent", key);
    auto up = CefDictionaryValue::Create(); up->SetString("type", "keyUp"); up->SetString("key", "a");
    up->SetString("code", "KeyA"); up->SetInt("windowsVirtualKeyCode", 'A');
    browser->GetHost()->ExecuteDevToolsMethod(0, "Input.dispatchKeyEvent", up);
    CefPostDelayedTask(TID_UI, base::BindOnce([](CefRefPtr<CefBrowser> browser) {
      browser->GetMainFrame()->ExecuteJavaScript(
          "window.onbeforeunload=e=>{e.preventDefault();e.returnValue='';};"
          "console.info('RED_ORG_STICKY:'+navigator.userActivation.hasBeenActive);", browser->GetMainFrame()->GetURL(), 0);
    }, browser), 200);
  } else if (browser->GetIdentifier() == state->organization_browser && message == "RED_ORG_STICKY:true") {
    state->organization_armed = true; state->Record("organization_real_user_activation");
  } else if (browser->GetIdentifier() == state->shell_id && message == "RED_ORGANIZATION_CHECKS:pass") {
    state->organization_checks = true; state->Record("organization_checks_pass", state->workspace->Snapshot());
  }
}
std::optional<bool> OrganizationUnloadDecision(const std::shared_ptr<State>& state, int browser_id) {
  if (!state->organization_probe || !state->organization_armed || state->organization_browser != browser_id) return std::nullopt;
  const bool leave = ++state->organization_unload_decisions > 1;
  auto details = CefDictionaryValue::Create(); details->SetBool("leave", leave);
  state->Record("organization_beforeunload_decision", details);
  return leave;
}
}
