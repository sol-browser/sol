#include "experiments/extension_popup_probe.h"
#include "include/base/cef_callback.h"
#include "include/cef_command_line.h"
#include "include/wrapper/cef_closure_task.h"
#include "windows/main_menu.h"
#include "windows/menu_popup.h"
#include "windows/panel_popup.h"
#include "workspace/controller.h"
namespace red {
namespace {
void ClickExtension(const std::shared_ptr<State>& state, const char* name) {
  if (CefCommandLine::GetGlobalCommandLine()->GetSwitchValue("probe-extension-popup") != "menu") {
    if (state->panel_popup) {
      state->panel_popup->Toggle("site", CefRect(500, 8, 28, 28));
      state->panel_popup->ProbeClickSelector(std::string(".site-extension[title=\"") + name + "\"]");
    }
    return;
  }
  // At the left screen edge the extension popup and the closing main menu
  // occupy the same part of the screen.
  state->window->Maximize();
  state->window->Activate();
  ShowMainMenu(state, kMainMenuAnchor);
  if (!state->menu_popup) return;
  CefPostDelayedTask(TID_UI, base::BindOnce([](std::weak_ptr<State> weak) {
    if (const auto s = weak.lock(); s && s->menu_popup)
      s->menu_popup->ProbeRun(0, R"JS(window.cefQuery({request: '{"method":"menu.probe:move:1:1"}', persistent: false, onSuccess() {}, onFailure() {}}))JS");
  }, std::weak_ptr<State>(state)), 320);
  const auto mouse = [weak = std::weak_ptr<State>(state)](int level, std::string row, const char* action, int delay) {
    const auto script = "(() => { const row = " + row + R"JS(;
      if (!row) return;
      const bounds = row.getBoundingClientRect();
      const x = Math.round(bounds.left + bounds.width / 2), y = Math.round(bounds.top + bounds.height / 2);
      window.cefQuery({ request: JSON.stringify({ method: 'menu.probe:)JS" + action + R"JS(:' + x + ':' + y }),
        persistent: false, onSuccess() {}, onFailure() {} }); })())JS";
    CefPostDelayedTask(TID_UI, base::BindOnce([](std::weak_ptr<State> weak, int level, std::string script) {
      if (const auto s = weak.lock(); s && s->menu_popup) s->menu_popup->ProbeRun(level, script);
    }, weak, level, script), delay);
  };
  mouse(0, "document.querySelector('[data-menu-key=extensions]')", "move", 450);
  mouse(1, "[...document.querySelectorAll('.menu-row')].find(row => row.textContent === '" + std::string(name) + "')", "click", 1150);
}

void Activate(std::weak_ptr<State> weak, std::string url) {
  auto state = weak.lock(); if (!state || !state->workspace) return;
  const auto snapshot = state->workspace->Snapshot();
  const auto nodes = snapshot->GetList("nodes");
  for (size_t i = 0; nodes && i < nodes->GetSize(); ++i) {
    const auto node = nodes->GetDictionary(i);
    if (node->GetString("url").ToString() != url || node->GetBool("archived")) continue;
    auto command = CefDictionaryValue::Create(); command->SetString("method", "tree.activate");
    command->SetString("id", node->GetString("id")); command->SetDouble("expectedRevision", snapshot->GetDouble("treeRevision"));
    state->workspace->Handle(command, [weak](int code, const std::string&, CefRefPtr<CefDictionaryValue>) {
      auto state = weak.lock(); if (!state) return;
      auto data = CefDictionaryValue::Create(); data->SetInt("code", code); state->Record("ext_popup_probe_activated", data);
    });
    return;
  }
  state->Record("ext_popup_probe_missing_tab");
}
}  // namespace
void OnExtensionPopupProbeLoad(const std::shared_ptr<State>& state, const std::string& url) {
  if (!state->extension_popup_probe || !url.starts_with("http")) return;
  // Three tabs (/first, /second, /third), then back to the middle one: the
  // popup must follow the visible page, not the first or the newest one.
  const auto base = url.substr(0, url.rfind('/'));
  if (state->extension_popup_phase == 0 && url.ends_with("/first")) {
    state->extension_popup_phase = 1; state->workspace->OpenLink(base + "/second", true);
  } else if (state->extension_popup_phase == 1 && url.ends_with("/second")) {
    state->extension_popup_phase = 2; state->workspace->OpenLink(base + "/third", true);
  } else if (state->extension_popup_phase == 2 && url.ends_with("/third")) {
    state->extension_popup_phase = 3;
    CefPostDelayedTask(TID_UI, base::BindOnce(&Activate, std::weak_ptr<State>(state), base + "/second"), 800);
    CefPostDelayedTask(TID_UI, base::BindOnce([](std::weak_ptr<State> weak) {
      if (const auto state = weak.lock()) ClickExtension(state, "Popup Probe");
    }, std::weak_ptr<State>(state)), 2200);
  }
}
void OnExtensionPopupProbeClosed(const std::shared_ptr<State>& state) {
  if (!state->extension_popup_probe || state->extension_popup_phase != 3) return;
  state->extension_popup_phase = 4;
  CefPostDelayedTask(TID_UI, base::BindOnce([](std::weak_ptr<State> weak) {
    if (const auto state = weak.lock()) ClickExtension(state, "Action Probe");
  }, std::weak_ptr<State>(state)), 600);
  // Without engine patch 02 nothing dispatches onClicked: end anyway.
  CefPostDelayedTask(TID_UI, base::BindOnce([](std::weak_ptr<State> weak) {
    if (auto state = weak.lock()) state->extension_popup_done = true;
  }, std::weak_ptr<State>(state)), 6000);
}
void ObserveExtensionActionProbe(const std::shared_ptr<State>& state, const std::string& message) {
  if (!state->extension_popup_probe || !message.starts_with("RED_ACTION_CLICKED:")) return;
  auto data = CefDictionaryValue::Create(); data->SetString("path", message.substr(19, 200));
  state->Record("ext_action_clicked", data);
  state->extension_popup_done = true;
}
}  // namespace red
