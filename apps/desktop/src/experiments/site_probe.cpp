#include "experiments/site_probe.h"
#include <functional>
#include "include/base/cef_callback.h"
#include "include/cef_request_context.h"
#include "include/wrapper/cef_closure_task.h"
#include "site/site_settings.h"
#include "windows/panel_popup.h"
#include "workspace/controller.h"
namespace red {
namespace {
void Fail(const std::shared_ptr<State>& state, const char* step) {
  auto data = CefDictionaryValue::Create(); data->SetString("step", step);
  state->Record("site_probe_failed", data); state->exit_code = 2;
}
// Open (if needed) and click a popover control by accessible name.
void Click(const std::shared_ptr<State>& state, const std::string& label) {
  if (!state->panel_popup) return;
  if (!state->panel_popup->is_open()) {
    // Permissions sit behind the popover's connection ("Secure") button.
    state->panel_popup->Toggle("site", CefRect(500, 8, 28, 28));
    state->panel_popup->ProbeClick("Connection and permissions");
  }
  state->panel_popup->ProbeClick(label);
}
// Poll Chromium's stored value for the visible page until it matches.
// After a reset Chromium may report ASK or DEFAULT; |also| accepts the second.
void WaitFor(std::weak_ptr<State> weak, cef_content_setting_types_t type, cef_content_setting_values_t value,
             const char* step, std::function<void(const std::shared_ptr<State>&)> next, int attempts = 50,
             cef_content_setting_values_t also = CEF_CONTENT_SETTING_VALUE_NUM_VALUES) {
  auto state = weak.lock();
  if (!state || !state->content) return;
  const auto origin = SiteOrigin(state->content);
  const auto current = state->content->GetHost()->GetRequestContext()->GetContentSetting(origin, origin, type);
  if (current == value || current == also) { next(state); return; }
  if (attempts == 0) { Fail(state, step); return; }
  CefPostDelayedTask(TID_UI, base::BindOnce([](std::weak_ptr<State> w, cef_content_setting_types_t t, cef_content_setting_values_t v,
      const char* s, std::function<void(const std::shared_ptr<State>&)> n, int a, cef_content_setting_values_t o) {
        WaitFor(w, t, v, s, std::move(n), a, o); }, weak, type, value, step, std::move(next), attempts - 1, also), 100);
}
void Page(const std::shared_ptr<State>& state, const std::string& script);
// The indicator and the page's stream can report in either order.
void StopCameraWhenReady(const std::shared_ptr<State>& state) {
  if (state->site_phase == 3 && state->site_stream_ready)
    Page(state, "window.__redStream.getTracks().forEach(t=>t.stop())");
}
void Page(const std::shared_ptr<State>& state, const std::string& script) {
  if (state->content) state->content->GetMainFrame()->ExecuteJavaScript(script, state->content->GetMainFrame()->GetURL(), 0);
}
constexpr char kQueryLocation[] =
    "navigator.permissions.query({name:'geolocation'}).then(s=>console.info('RED_SITE:location:'+s.state))";
bool ActiveNodeUsesCamera(const std::shared_ptr<State>& state) {
  if (!state->workspace) return false;
  const auto snapshot = state->workspace->Snapshot();
  const auto nodes = snapshot->GetList("nodes");
  for (size_t i = 0; i < nodes->GetSize(); ++i) {
    const auto node = nodes->GetDictionary(i);
    if (node->GetString("id") == snapshot->GetString("activeId")) return node->GetBool("mediaCamera");
  }
  return false;
}
}  // namespace
void StartSiteProbe(const std::shared_ptr<State>& state, CefRefPtr<CefBrowser>) {
  if (!state->site_probe || state->site_phase != 0) return;
  state->site_phase = 1;
  CefPostDelayedTask(TID_UI, base::BindOnce([](std::weak_ptr<State> weak) {
    auto state = weak.lock(); if (!state) return;
    Click(state, "Location: Block");
    WaitFor(weak, CEF_CONTENT_SETTING_TYPE_GEOLOCATION, CEF_CONTENT_SETTING_VALUE_BLOCK, "location_block",
            [](const std::shared_ptr<State>& s) { s->Record("site_location_blocked"); Page(s, kQueryLocation); });
  }, std::weak_ptr<State>(state)), 1500);
}
void ObserveSiteProbe(const std::shared_ptr<State>& state, CefRefPtr<CefBrowser>, const std::string& message) {
  if (!state->site_probe || !message.starts_with("RED_SITE:")) return;
  auto data = CefDictionaryValue::Create(); data->SetString("message", message.substr(0, 200));
  state->Record("site_page", data);
  if (state->site_phase == 1) {
    if (message != "RED_SITE:location:denied") { Fail(state, "page_location_denied"); return; }
    state->site_phase = 2;
    Click(state, "Camera: Allow");
    WaitFor(state, CEF_CONTENT_SETTING_TYPE_MEDIASTREAM_CAMERA, CEF_CONTENT_SETTING_VALUE_ALLOW, "camera_allow",
            [](const std::shared_ptr<State>& s) {
              // Allowed: no prompt. The device is Chromium's fake camera (probe switch).
              Page(s, "navigator.mediaDevices.getUserMedia({video:true}).then(m=>{window.__redStream=m;console.info('RED_SITE:camera:on')},"
                      "e=>console.info('RED_SITE:camera:error:'+e.name))");
            });
  } else if (state->site_phase == 4) {
    if (message != "RED_SITE:location:prompt") { Fail(state, "page_location_reset"); return; }
    state->site_checks = true; state->Record("site_checks_pass");
  } else if (message == "RED_SITE:camera:on") {
    state->site_stream_ready = true; StopCameraWhenReady(state);
  } else if (message.starts_with("RED_SITE:camera:error")) {
    Fail(state, "camera_stream");
  }
}
void OnSiteProbeMedia(const std::shared_ptr<State>& state, bool video, bool) {
  if (!state->site_probe) return;
  if (state->site_phase == 2 && video) {
    if (!ActiveNodeUsesCamera(state)) { Fail(state, "camera_indicator"); return; }
    state->site_phase = 3; state->Record("site_camera_indicator");
    StopCameraWhenReady(state);
  } else if (state->site_phase == 3 && !video) {
    if (ActiveNodeUsesCamera(state)) { Fail(state, "camera_indicator_cleared"); return; }
    state->site_phase = 4;
    Click(state, "Reset permissions");
    WaitFor(state, CEF_CONTENT_SETTING_TYPE_GEOLOCATION, CEF_CONTENT_SETTING_VALUE_ASK, "reset", [](const std::shared_ptr<State>& s) {
      Page(s, kQueryLocation);
    }, 50, CEF_CONTENT_SETTING_VALUE_DEFAULT);
  }
}
}  // namespace red
