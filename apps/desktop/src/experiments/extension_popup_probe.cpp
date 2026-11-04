#include "experiments/extension_popup_probe.h"
#include "include/base/cef_callback.h"
#include "include/wrapper/cef_closure_task.h"
#include "windows/panel_popup.h"
#include "workspace/controller.h"
namespace red {
namespace {
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
      auto state = weak.lock(); if (!state || !state->panel_popup) return;
      state->panel_popup->Toggle("site", CefRect(500, 8, 28, 28));
      state->panel_popup->ProbeClick("Open Popup Probe");
    }, std::weak_ptr<State>(state)), 2200);
  }
}
void OnExtensionPopupProbeClosed(const std::shared_ptr<State>& state) {
  if (!state->extension_popup_probe || state->extension_popup_phase != 3) return;
  state->extension_popup_phase = 4;
  CefPostDelayedTask(TID_UI, base::BindOnce([](std::weak_ptr<State> weak) {
    auto state = weak.lock(); if (!state || !state->panel_popup) return;
    state->panel_popup->Toggle("site", CefRect(500, 8, 28, 28));
    state->panel_popup->ProbeClick("Open Action Probe");
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
