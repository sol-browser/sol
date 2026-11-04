#include "site/site_settings.h"
#include "include/cef_parser.h"
#include "include/cef_request_context.h"
#include "workspace/controller.h"
#include "windows/auto_pip.h"
#include "app/session.h"
#include "site/boost_store.h"
#include "windows/boosts.h"
namespace red {
namespace {
struct Permission { const char* key; cef_content_setting_types_t type; bool can_ask; };
// Fixed list; anything else is rejected. Pop-ups have no "ask" state in Chrome.
constexpr Permission kPermissions[] = {
  {"camera", CEF_CONTENT_SETTING_TYPE_MEDIASTREAM_CAMERA, true},
  {"microphone", CEF_CONTENT_SETTING_TYPE_MEDIASTREAM_MIC, true},
  {"location", CEF_CONTENT_SETTING_TYPE_GEOLOCATION, true},
  {"notifications", CEF_CONTENT_SETTING_TYPE_NOTIFICATIONS, true},
  {"clipboard", CEF_CONTENT_SETTING_TYPE_CLIPBOARD_READ_WRITE, true},
  {"popups", CEF_CONTENT_SETTING_TYPE_POPUPS, false},
};
const Permission* Find(const std::string& key) {
  for (const auto& permission : kPermissions) if (key == permission.key) return &permission;
  return nullptr;
}
std::string ValueName(const Permission& permission, cef_content_setting_values_t value) {
  if (value == CEF_CONTENT_SETTING_VALUE_ALLOW) return "allow";
  if (value == CEF_CONTENT_SETTING_VALUE_BLOCK || !permission.can_ask) return "block";
  return "ask";  // ASK, or DEFAULT (no site value: these types ask by default).
}
CefRefPtr<CefRequestContext> Context(const std::shared_ptr<State>& state) {
  return state->content ? state->content->GetHost()->GetRequestContext() : nullptr;
}
}  // namespace
std::string SiteOrigin(CefRefPtr<CefBrowser> browser) {
  if (!browser) return "";
  CefURLParts parts;
  if (!CefParseURL(browser->GetMainFrame()->GetURL(), parts)) return "";
  const auto scheme = CefString(&parts.scheme).ToString(), host = CefString(&parts.host).ToString();
  if ((scheme != "https" && scheme != "http") || host.empty()) return "";
  const auto port = CefString(&parts.port).ToString();
  return scheme + "://" + host + (port.empty() ? "" : ":" + port) + "/";
}
CefRefPtr<CefDictionaryValue> SiteInfo(const std::shared_ptr<State>& state) {
  auto info = CefDictionaryValue::Create();
  const auto origin = SiteOrigin(state->content);
  info->SetString("origin", origin);
  auto media = CefDictionaryValue::Create();
  const auto access = state->content ? state->media_access.find(state->content->GetIdentifier()) : state->media_access.end();
  media->SetBool("camera", access != state->media_access.end() && access->second.first);
  media->SetBool("microphone", access != state->media_access.end() && access->second.second);
  info->SetDictionary("media", media);
  auto list = CefListValue::Create();
  if (const auto context = Context(state); context && !origin.empty()) {
    for (const auto& permission : kPermissions) {
      auto row = CefDictionaryValue::Create();
      row->SetString("key", permission.key);
      row->SetString("value", ValueName(permission, context->GetContentSetting(origin, origin, permission.type)));
      list->SetDictionary(list->GetSize(), row);
    }
  }
  info->SetList("permissions", list);
  if (!origin.empty() && state->content) info->SetBool("autoPip", AutoPipEnabled(state, state->content));
  if (const auto key = BoostKeyFromOrigin(origin); !key.empty() && state->session && state->session->boosts) {
    // The Boost of this exact host (a parent domain's Boost also applies but is edited on that domain).
    const auto* boost = state->session->boosts->Find(key);
    auto row = CefDictionaryValue::Create();
    row->SetString("host", key); row->SetBool("enabled", boost ? boost->enabled : true); row->SetBool("active", boost && !boost->Empty());
    info->SetDictionary("boost", row);
  }
  return info;
}
int SetSitePermission(const std::shared_ptr<State>& state, const std::string& origin,
                      const std::string& key, const std::string& value, std::string& error) {
  const auto* permission = Find(key);
  if (!permission || (value != "allow" && value != "block" && value != "ask") || (value == "ask" && !permission->can_ask)) {
    error = "Unsupported site setting"; return 400;
  }
  const auto context = Context(state);
  if (!context || origin.empty() || origin != SiteOrigin(state->content)) { error = "The page changed; reopen site settings"; return 409; }
  // "ask" (and pop-up "block") return to Chrome's default for the site.
  const auto setting = value == "allow" ? CEF_CONTENT_SETTING_VALUE_ALLOW :
      value == "block" && permission->can_ask ? CEF_CONTENT_SETTING_VALUE_BLOCK : CEF_CONTENT_SETTING_VALUE_DEFAULT;
  context->SetContentSetting(origin, origin, permission->type, setting);
  auto data = CefDictionaryValue::Create(); data->SetString("key", key); data->SetString("value", value);
  state->Record("site_setting_changed", data);
  return 0;
}
int ResetSitePermissions(const std::shared_ptr<State>& state, const std::string& origin, std::string& error) {
  const auto context = Context(state);
  if (!context || origin.empty() || origin != SiteOrigin(state->content)) { error = "The page changed; reopen site settings"; return 409; }
  for (const auto& permission : kPermissions)
    context->SetContentSetting(origin, origin, permission.type, CEF_CONTENT_SETTING_VALUE_DEFAULT);
  state->Record("site_settings_reset");
  return 0;
}
bool DecideMediaAccess(CefRefPtr<CefBrowser> browser, const std::string& requesting_origin,
                       uint32_t requested, CefRefPtr<CefMediaAccessCallback> callback) {
  constexpr uint32_t kDevices = CEF_MEDIA_PERMISSION_DEVICE_AUDIO_CAPTURE | CEF_MEDIA_PERMISSION_DEVICE_VIDEO_CAPTURE;
  const auto top = SiteOrigin(browser);
  if (!requested || (requested & ~kDevices) || top.empty() || requesting_origin.empty()) return false;
  const auto context = browser->GetHost()->GetRequestContext();
  bool all_allowed = true;
  for (const auto [bit, type] : {std::pair{CEF_MEDIA_PERMISSION_DEVICE_VIDEO_CAPTURE, CEF_CONTENT_SETTING_TYPE_MEDIASTREAM_CAMERA},
                                 std::pair{CEF_MEDIA_PERMISSION_DEVICE_AUDIO_CAPTURE, CEF_CONTENT_SETTING_TYPE_MEDIASTREAM_MIC}}) {
    if (!(requested & bit)) continue;
    const auto value = context->GetContentSetting(requesting_origin, top, type);
    if (value == CEF_CONTENT_SETTING_VALUE_BLOCK) { callback->Continue(CEF_MEDIA_PERMISSION_NONE); return true; }
    all_allowed = all_allowed && value == CEF_CONTENT_SETTING_VALUE_ALLOW;
  }
  if (!all_allowed) return false;
  callback->Continue(requested);
  return true;
}
void SetFrameAudible(const std::shared_ptr<State>& state, int browser_id, const std::string& frame, bool audible) {
  auto& frames = state->audible_frames[browser_id];
  const bool before = !frames.empty();
  if (audible) { if (frames.size() < 64) frames.insert(frame.substr(0, 64)); } else frames.erase(frame.substr(0, 64));
  if (frames.empty()) state->audible_frames.erase(browser_id);
  const bool after = state->audible_frames.contains(browser_id);
  if (before == after) return;
  auto data = CefDictionaryValue::Create(); data->SetInt("browser_id", browser_id); data->SetBool("audible", after);
  state->Record("page_audible", data);
  if (state->workspace) state->workspace->Refresh();
}
void SetMediaAccess(const std::shared_ptr<State>& state, int browser_id, bool video, bool audio) {
  if (video || audio) state->media_access[browser_id] = {video, audio};
  else state->media_access.erase(browser_id);
  auto data = CefDictionaryValue::Create();
  data->SetInt("browser_id", browser_id); data->SetBool("camera", video); data->SetBool("microphone", audio);
  state->Record("media_access", data);
  if (state->workspace) state->workspace->Refresh();
}
}  // namespace red
