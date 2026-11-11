#include "experiments/browsing_probe.h"
#include "windows/embedded_content.h"
#include "include/base/cef_callback.h"
#include "include/wrapper/cef_closure_task.h"
namespace red {
void ObserveBrowsingProbe(const std::shared_ptr<State>& state, CefRefPtr<CefBrowser> browser, const std::string& message) {
  if (!state->browsing_probe) return;
  if (message.starts_with("RED_BROWSING")) {
    auto data = CefDictionaryValue::Create(); data->SetString("phase", message);
    state->Record("browsing_phase", data);
  }
  if (browser->GetIdentifier() == state->shell_id && message == "RED_BROWSING_CHECKS:pass") {
    state->browsing_checks = true; state->Record("browsing_checks_pass"); return;
  }
  if (message == "RED_BROWSING_PRESERVED:true" && state->content &&
      browser->GetIdentifier() == state->content->GetIdentifier()) {
    state->browsing_preserved = true; state->Record("browsing_source_form_preserved"); return;
  }
  if (state->content && browser->GetIdentifier() == state->content->GetIdentifier() &&
      (message == "RED_BROWSING_MOUSE_READY:middle:middle" || message == "RED_BROWSING_MOUSE_READY:ctrl:ctrl")) {
    const bool middle = message == "RED_BROWSING_MOUSE_READY:middle:middle";
    // Loading/bounds callbacks can precede the Chrome compositor's input data.
    // Wait for frames in that renderer and verify native input ownership too.
    CefPostDelayedTask(TID_UI, base::BindOnce([](std::weak_ptr<State> weak, CefRefPtr<CefBrowser> page, bool middle) {
      auto current = weak.lock();
      if (!current || !current->content || current->content->GetIdentifier() != page->GetIdentifier()) return;
      const bool ready = current->embedded && current->embedded->ActiveMatchesBounds() && current->embedded->ActiveOwnsInputArea();
      auto data = CefDictionaryValue::Create(); data->SetBool("pass", ready); current->Record("browsing_input_ready", data);
      if (!ready) { current->exit_code = 2; return; }
      for (const auto* type : {"mousePressed", "mouseReleased"}) {
        auto event = CefDictionaryValue::Create(); event->SetString("type", type);
        event->SetInt("x", 120); event->SetInt("y", middle ? 110 : 160);
        event->SetString("button", middle ? "middle" : "left"); event->SetInt("clickCount", 1);
        if (!middle) event->SetInt("modifiers", 2);
        page->GetHost()->ExecuteDevToolsMethod(0, "Input.dispatchMouseEvent", event);
      }
    }, std::weak_ptr<State>(state), browser, middle), 50);
    return;
  }
  if (browser->GetIdentifier() != state->shell_id || !state->content) return;
  const auto page = state->content;
  if (message == "RED_BROWSING:cancel-focus") {
    const auto handle = page->GetHost()->GetWindowHandle();
    const auto focus = ::GetFocus();
    auto result = CefDictionaryValue::Create(); const bool ok = focus == handle || IsChild(handle, focus);
    result->SetBool("pass", ok); state->Record("command_cancel_focus_checked", result);
    if (!ok) state->exit_code = 2;
    return;
  }
  if (message == "RED_BROWSING:copy-focus" || message == "RED_BROWSING:toast-refreshed" || message == "RED_BROWSING:toast-expired") {
    const auto handle = page->GetHost()->GetWindowHandle();
    const auto focus = ::GetFocus();
    const bool copy = message != "RED_BROWSING:toast-expired";
    const bool page_focus = focus == handle || IsChild(handle, focus);
    const bool visible = state->embedded && state->embedded->NotificationVisible();
    auto result = CefDictionaryValue::Create(); result->SetBool("page_focus", page_focus);
    result->SetBool("visible", visible); result->SetBool("pass", page_focus && visible == copy);
    state->Record(message == "RED_BROWSING:toast-refreshed" ? "notification_refresh_checked" : copy ? "copy_focus_checked" : "notification_expiry_checked", result);
    if (!page_focus || visible != copy) state->exit_code = 2;
    return;
  }
  if (message == "RED_BROWSING:background") {
    const bool visible = state->embedded && state->embedded->HasSingleVisibleActive() &&
        state->embedded->ActiveOwnsInputArea() && state->embedded->ActiveMatchesBounds();
    auto result = CefDictionaryValue::Create(); result->SetBool("pass", visible);
    state->Record("browsing_background_visibility", result);
    if (!visible) state->exit_code = 2;
    return;
  }
  if (message == "RED_BROWSING:mark") {
    page->GetMainFrame()->ExecuteJavaScript("window.__redPreserve=crypto.randomUUID();document.querySelector('input').value='preserve-source';", page->GetMainFrame()->GetURL(), 0);
  } else if (message == "RED_BROWSING:inspect") {
    page->GetMainFrame()->ExecuteJavaScript("console.info('RED_BROWSING_PRESERVED:'+(!!window.__redPreserve&&document.querySelector('input').value==='preserve-source'));", page->GetMainFrame()->GetURL(), 0);
  } else {
    const char* script = nullptr;
    if (message == "RED_BROWSING:middle" || message == "RED_BROWSING:ctrl") {
      const bool middle = message == "RED_BROWSING:middle";
      page->GetMainFrame()->ExecuteJavaScript(middle ?
          "requestAnimationFrame(()=>requestAnimationFrame(()=>console.info('RED_BROWSING_MOUSE_READY:middle:'+document.elementFromPoint(120,110)?.id)))" :
          "requestAnimationFrame(()=>requestAnimationFrame(()=>console.info('RED_BROWSING_MOUSE_READY:ctrl:'+document.elementFromPoint(120,160)?.id)))", page->GetMainFrame()->GetURL(), 0);
      return;
    }
    if (message == "RED_BROWSING:blank") script = "document.querySelector('#blank').click()";
    if (message == "RED_BROWSING:named") script = "document.querySelector('#named').click()";
    if (message == "RED_BROWSING:named-reuse") script = "document.querySelector('#named-reuse').click()";
    if (message == "RED_BROWSING:popup") script = "document.querySelector('#popup').click()";
    if (!script) return;
    auto evaluate = CefDictionaryValue::Create(); evaluate->SetString("expression", script);
    evaluate->SetBool("userGesture", true);
    page->GetHost()->ExecuteDevToolsMethod(0, "Runtime.evaluate", evaluate);
  }
}
}  // namespace red
