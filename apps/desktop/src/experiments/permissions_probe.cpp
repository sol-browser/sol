#include "experiments/permissions_probe.h"
#include <map>
#include "include/base/cef_callback.h"
#include "include/wrapper/cef_closure_task.h"
namespace red {
namespace {
struct Request { const char* script; const char* query; };
// Fixed scripts only; the type comes from a validated probe switch.
const std::map<std::string, Request>& Requests() {
  static const std::map<std::string, Request> requests = {
    {"geolocation", {"navigator.geolocation.getCurrentPosition(()=>r('granted'),e=>r('error:'+e.code))", "geolocation"}},
    {"notifications", {"Notification.requestPermission().then(v=>r(v))", "notifications"}},
    {"camera", {"navigator.mediaDevices.getUserMedia({video:true}).then(s=>{s.getTracks().forEach(t=>t.stop());r('granted')},e=>r('error:'+e.name))", "camera"}},
    {"microphone", {"navigator.mediaDevices.getUserMedia({audio:true}).then(s=>{s.getTracks().forEach(t=>t.stop());r('granted')},e=>r('error:'+e.name))", "microphone"}},
    {"clipboard", {"navigator.clipboard.readText().then(()=>r('granted'),e=>r('error:'+e.name))", "clipboard-read"}},
  };
  return requests;
}
void Evaluate(CefRefPtr<CefBrowser> page, const std::string& script, bool gesture) {
  auto evaluate = CefDictionaryValue::Create();
  evaluate->SetString("expression", script); evaluate->SetBool("userGesture", gesture);
  page->GetHost()->ExecuteDevToolsMethod(0, "Runtime.evaluate", evaluate);
}
}  // namespace
bool IsPermissionsProbeType(const std::string& type) { return Requests().contains(type); }
void RecordPermissionRequest(const std::shared_ptr<State>& state, CefRefPtr<CefBrowser> browser,
                             const char* kind, const std::string& origin, uint32_t permissions) {
  if (!state->probe) return;
  auto data = CefDictionaryValue::Create();
  data->SetString("kind", kind); data->SetString("origin", origin.substr(0, 200));
  data->SetInt("permissions", static_cast<int>(permissions)); data->SetInt("browser_id", browser->GetIdentifier());
  state->Record("permission_callback", data);
}
void StartPermissionsProbe(const std::shared_ptr<State>& state, CefRefPtr<CefBrowser> page) {
  if (state->permissions_probe.empty() || state->permissions_started) return;
  state->permissions_started = true;
  const auto& request = Requests().at(state->permissions_probe);
  CefPostDelayedTask(TID_UI, base::BindOnce([](std::weak_ptr<State> weak, CefRefPtr<CefBrowser> page, std::string script) {
    if (auto state = weak.lock()) state->Record("permission_requested");
    Evaluate(page, "{const r=v=>console.info('RED_PERMISSION_RESULT:'+v);" + script + "}", true);
  }, std::weak_ptr<State>(state), page, std::string(request.script)), 1500);
  // Leave time for a prompt to appear (nobody answers it), then read the state.
  CefPostDelayedTask(TID_UI, base::BindOnce([](CefRefPtr<CefBrowser> page, std::string name) {
    Evaluate(page, "navigator.permissions.query({name:'" + name + "'}).then(s=>console.info('RED_PERMISSION_STATE:'+s.state),"
                   "e=>console.info('RED_PERMISSION_STATE:error:'+e.name))", false);
  }, page, std::string(request.query)), 7000);
}
void ObservePermissionsProbe(const std::shared_ptr<State>& state, CefRefPtr<CefBrowser>, const std::string& message) {
  if (state->permissions_probe.empty()) return;
  if (message.starts_with("RED_PERMISSION_RESULT:")) {
    auto data = CefDictionaryValue::Create(); data->SetString("result", message.substr(22, 100));
    state->Record("permission_page_result", data);
  } else if (message.starts_with("RED_PERMISSION_STATE:")) {
    auto data = CefDictionaryValue::Create(); data->SetString("state", message.substr(21, 100));
    state->Record("permission_page_state", data);
    state->permissions_observed = true;
  }
}
}  // namespace red
