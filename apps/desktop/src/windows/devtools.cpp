#include "app/session.h"
#include "windows/devtools.h"
#include "windows/devtools_dock.h"
#include "navigation/shortcuts.h"
#include "workspace/controller.h"
#include "windows/window_close.h"
#include "include/wrapper/cef_helpers.h"
#include "include/cef_app.h"
#include <vector>
namespace red {
namespace {
// No Shell router, workspace registration, navigation rewriting or browser
// workspace shortcuts: the frontend owns editing, with only inspector close
// chords and Ctrl+Shift+D to return a detached inspector to the browser.
class InspectorClient final : public CefClient, public CefLifeSpanHandler, public CefLoadHandler, public CefKeyboardHandler {
 public:
  InspectorClient(std::weak_ptr<State> state, int target) : state_(state), target_(target) {}
  CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return this; }
  CefRefPtr<CefLoadHandler> GetLoadHandler() override { return this; }
  CefRefPtr<CefKeyboardHandler> GetKeyboardHandler() override { return this; }
  bool OnPreKeyEvent(CefRefPtr<CefBrowser>, const CefKeyEvent& event, CefEventHandle, bool*) override {
    if (event.type != KEYEVENT_RAWKEYDOWN || (event.modifiers & EVENTFLAG_ALT_DOWN)) return false;
    const bool ctrl = (event.modifiers & EVENTFLAG_CONTROL_DOWN) != 0;
    const bool shift = (event.modifiers & EVENTFLAG_SHIFT_DOWN) != 0;
    if (event.windows_key_code == VK_F11 && !ctrl && !shift) {
      if (auto s = state_.lock()) ExecuteShortcut(s, Shortcut::Fullscreen);
      return true;
    }
    if (event.windows_key_code == 'M' && ctrl && shift) {
      if (auto s = state_.lock(); s && s->devtools_dock) s->devtools_dock->ToggleDevice(target_);
      return true;
    }
    if (event.windows_key_code == 'D' && ctrl && shift) {
      if (auto s = state_.lock(); s && s->devtools_dock) s->devtools_dock->ToggleDock(target_);
      return true;
    }
    if ((event.windows_key_code == VK_F12 && !ctrl && !shift) ||
        (event.windows_key_code == 'I' && ctrl && shift) || (event.windows_key_code == 'W' && ctrl && !shift)) {
      if (auto s = state_.lock()) ClosePageDevTools(s, target_);
      return true;
    }
    return false;
  }
  void OnAfterCreated(CefRefPtr<CefBrowser> browser) override {
    if (auto s = state_.lock()) {
      s->browsers[browser->GetIdentifier()] = browser;
      s->devtools_targets[browser->GetIdentifier()] = target_;
      auto d = CefDictionaryValue::Create(); d->SetInt("browser_id", browser->GetIdentifier()); d->SetInt("target_id", target_);
      const auto hwnd = browser->GetHost()->GetWindowHandle();
      docked_ = s->window && GetParent(hwnd) == s->window->GetWindowHandle();
      d->SetBool("child", docked_);
      s->Record("devtools_created", d);
      if (s->devtools_dock) s->devtools_dock->Attached(target_, browser);
      if (!s->browsers.contains(target_) || !s->window || (s->workspace && s->workspace->WindowClosing()))
        browser->GetHost()->CloseBrowser(false);
    }
  }
  void OnLoadEnd(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, int status) override {
    if (frame->IsMain()) if (auto s = state_.lock()) {
      auto d = CefDictionaryValue::Create(); d->SetInt("browser_id", browser->GetIdentifier());
      d->SetInt("target_id", target_); d->SetInt("http_status", status); s->Record("devtools_loaded", d);
    }
  }
  void OnBeforeClose(CefRefPtr<CefBrowser> browser) override {
    if (auto s = state_.lock()) {
      const auto id = browser->GetIdentifier(); s->devtools_targets.erase(id); s->browsers.erase(id);
      if (docked_) s->docked_devtools_closed_ms = GetTickCount64();
      if (s->devtools_dock) s->devtools_dock->Detached(target_, id);
      auto d = CefDictionaryValue::Create(); d->SetInt("browser_id", id); d->SetInt("target_id", target_);
      s->Record("devtools_closed", d);
      if (s->window_close) s->window_close->OnClosed(id);
      if (s->browsers.empty() && !s->window) QuitIfLastWindow(s);
    }
  }
  bool OnBeforePopup(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, int, const CefString&, const CefString&,
      cef_window_open_disposition_t, bool, const CefPopupFeatures&, CefWindowInfo&, CefRefPtr<CefClient>&,
      CefBrowserSettings&, CefRefPtr<CefDictionaryValue>&, bool*) override { return true; }
 private:
  std::weak_ptr<State> state_; int target_; bool docked_ = false;
  IMPLEMENT_REFCOUNTING(InspectorClient);
};
}
CefRefPtr<CefClient> DevToolsClient(const std::shared_ptr<State>& state, int target) {
  return new InspectorClient(state, target);
}
void ConfigurePageDevTools(const std::shared_ptr<State>& state, int target, CefWindowInfo& info) {
  if (!state->devtools_dock) state->devtools_dock = std::make_shared<DevToolsDock>(state);
  state->devtools_dock->Configure(target, info);
}
bool ShowPageDevTools(const std::shared_ptr<State>& state, CefRefPtr<CefBrowser> page, const CefPoint& inspect, bool toggle) {
  CEF_REQUIRE_UI_THREAD();
  if (!state->workspace || state->workspace->WindowClosing() || !page || !page->IsValid() ||
      page->GetIdentifier() == state->shell_id || page->GetIdentifier() == state->command_id ||
      state->internal_pages.contains(page->GetIdentifier()) ||  // Trusted UI is never inspected.
      state->devtools_targets.contains(page->GetIdentifier())) return false;
  const bool existing = page->GetHost()->HasDevTools();
  if (existing && toggle) { page->GetHost()->CloseDevTools(); return true; }
  CefWindowInfo info; ConfigurePageDevTools(state, page->GetIdentifier(), info);
  CefBrowserSettings settings;
  page->GetHost()->ShowDevTools(info, DevToolsClient(state, page->GetIdentifier()), settings, inspect);
  auto d = CefDictionaryValue::Create(); d->SetInt("target_id", page->GetIdentifier()); d->SetBool("existing", existing);
  d->SetBool("inspect", inspect.x != 0 || inspect.y != 0); state->Record("devtools_requested", d);
  return true;
}
void ClosePageDevTools(const std::shared_ptr<State>& state, int target) {
  // CEF may close the frontend itself with its target. Copy references before
  // asking remaining frontends to close, since callbacks can change the maps.
  std::vector<CefRefPtr<CefBrowser>> tools;
  for (const auto& [id, source] : state->devtools_targets)
    if (source == target) if (auto at = state->browsers.find(id); at != state->browsers.end()) tools.push_back(at->second);
  for (const auto& browser : tools) browser->GetHost()->CloseBrowser(false);
}
}  // namespace red
