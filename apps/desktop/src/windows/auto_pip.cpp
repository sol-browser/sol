#include "windows/auto_pip.h"
#include "app/preferences.h"
#include "app/session.h"
#include "site/site_settings.h"
#include "include/base/cef_callback.h"
#include "include/wrapper/cef_closure_task.h"

namespace red {
namespace {
// The page's largest playing, audible, visible-track video enters PiP. The
// element is remembered under a registry symbol so only this PiP is closed on
// return (a PiP the user opened stays).
constexpr char kEnter[] =
    "(()=>{const key=Symbol.for('red.autoPip');"
    "if(document.pictureInPictureElement||!document.pictureInPictureEnabled)return;"
    "const area=v=>{const r=v.getBoundingClientRect();return r.width*r.height;};"
    "const v=[...document.querySelectorAll('video')].filter(v=>!v.paused&&!v.ended&&v.readyState>=2&&"
    "v.videoWidth>0&&!v.muted&&v.volume>0&&!v.disablePictureInPicture).sort((a,b)=>area(b)-area(a))[0];"
    "if(!v)return;v.requestPictureInPicture().then(()=>{window[key]=v;"
    "console.info('RED_AUTO_PIP:entered');}).catch(()=>{});})()";
constexpr char kExit[] =
    "(()=>{const key=Symbol.for('red.autoPip');const v=window[key];delete window[key];"
    "if(v&&document.pictureInPictureElement===v)document.exitPictureInPicture().catch(()=>{});})()";

constexpr char kToggle[] =
    "(()=>{if(document.pictureInPictureElement){document.exitPictureInPicture().catch(()=>{});return;}"
    "if(!document.pictureInPictureEnabled)return;"
    "const area=v=>{const r=v.getBoundingClientRect();return r.width*r.height;};"
    "const v=[...document.querySelectorAll('video')].filter(v=>v.readyState>=1&&v.videoWidth>0&&!v.disablePictureInPicture)"
    ".sort((a,b)=>(a.paused-b.paused)||area(b)-area(a))[0];"
    "if(v)v.requestPictureInPicture().catch(()=>{});})()";

void Evaluate(CefRefPtr<CefBrowser> page, const char* script) {
  auto params = CefDictionaryValue::Create();
  params->SetString("expression", script); params->SetBool("userGesture", true);
  page->GetHost()->ExecuteDevToolsMethod(0, "Runtime.evaluate", params);
}
}  // namespace
// Off by default; a site's own switch (site controls) overrides the default.
bool AutoPipEnabled(const std::shared_ptr<State>& state, CefRefPtr<CefBrowser> page) {
  if (!state->session || !state->session->preferences || !SitePipIntegrationEnabled(state, page)) return false;
  const auto& prefs = state->session->preferences->Get();
  const auto site = prefs.auto_pip_sites.find(SiteOrigin(page));
  return site != prefs.auto_pip_sites.end() ? site->second : prefs.auto_pip;
}
bool SitePipIntegrationEnabled(const std::shared_ptr<State>& state, CefRefPtr<CefBrowser> page) {
  const auto frame = page && page->IsValid() ? page->GetMainFrame() : nullptr;
  const auto provider = frame ? IntegrationForUrl(frame->GetURL().ToString()) : std::string{};
  return (provider != "youtube" && provider != "googleMeet") || BrowserIntegrationEnabled(state, provider);
}
void RefreshPipIntegrations(const std::shared_ptr<State>& state) {
  for (const auto& [id, page] : state->browsers) {
    if (!state->pip_buttons.contains(id) && !state->browser_profiles.contains(id)) continue;
    if (state->internal_pages.contains(id) || !page->IsValid()) continue;
    if (!SitePipIntegrationEnabled(state, page) && state->auto_pip_pages.erase(id)) Evaluate(page, kExit);
    InstallVideoPipButton(state, page);
  }
}

void TogglePictureInPicture(CefRefPtr<CefBrowser> page) { if (page && page->IsValid()) Evaluate(page, kToggle); }

void OnPageSwitched(const std::shared_ptr<State>& state, CefRefPtr<CefBrowser> left, CefRefPtr<CefBrowser> shown) {
  if (shown && state->auto_pip_pages.erase(shown->GetIdentifier())) Evaluate(shown, kExit);
  if (!left || left == shown || !left->IsValid() || !AutoPipEnabled(state, left)) return;
  // Only a page that is actually playing sound (Chromium's audible state);
  // muted autoplay previews never pop out.
  if (!state->audible_frames.contains(left->GetIdentifier())) return;
  // After the new layout: a page still on screen (split pane, Link Peek
  // source, standalone window) keeps its video in place.
  // (The outgoing page stays up during a tab transition, at most 400 ms.)
  CefPostDelayedTask(TID_UI, base::BindOnce([](std::weak_ptr<State> weak, CefRefPtr<CefBrowser> page) {
    const auto state = weak.lock();
    if (!state || !page->IsValid() || !AutoPipEnabled(state, page) || state->content == page || IsWindowVisible(page->GetHost()->GetWindowHandle())) return;
    state->auto_pip_pages.insert(page->GetIdentifier());
    Evaluate(page, kEnter);
    state->Record("auto_pip_requested");
  }, std::weak_ptr<State>(state), left), 600);
}
}  // namespace red
