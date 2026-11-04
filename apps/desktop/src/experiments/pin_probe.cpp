#include "experiments/pin_probe.h"
#include <filesystem>
#include "app/session.h"
#include "extensions/extension_catalog.h"
#include "include/base/cef_callback.h"
#include "include/wrapper/cef_closure_task.h"
#include "windows/extension_popup.h"
#include "windows/main_menu.h"
#include "workspace/controller.h"
namespace red {
namespace {
CefRefPtr<CefDictionaryValue> PinnedRow(const std::shared_ptr<State>& state) {
  // Keep the snapshot alive: a child list is only valid while its parent is.
  const auto snapshot = state->workspace->Snapshot();
  const auto list = snapshot->GetList("pinnedExtensions");
  return list && list->GetSize() ? list->GetDictionary(0)->Copy(false) : nullptr;
}
}  // namespace
void OnPinProbeLoad(const std::shared_ptr<State>& state, const std::string& url) {
  if (!state->pin_probe || state->pin_started || !url.starts_with("http")) return;
  state->pin_started = true;
  CefPostDelayedTask(TID_UI, base::BindOnce([](std::weak_ptr<State> weak) {
    const auto state = weak.lock(); if (!state || !state->extensions || !state->workspace) return;
    std::string id;
    for (const auto& item : state->extensions->List(CefRequestContext::GetGlobalContext())) if (!item.popup.empty()) { id = item.id; break; }
    auto result = CefDictionaryValue::Create(); result->SetBool("found", !id.empty());
    if (id.empty()) { state->Record("pin_probe_result", result); state->pin_done = true; return; }
    PinExtension(state, id, true);
    const auto row = PinnedRow(state);
    result->SetBool("pinned_shown", row && row->GetString("id") == id);
    result->SetBool("icon", row && row->GetString("icon").ToString().starts_with("data:image/png"));
    state->pin_result = result; state->pin_id = id;
    // The button exists once React rendered the snapshot; click it as a person would.
    CefPostDelayedTask(TID_UI, base::BindOnce([](std::weak_ptr<State> weak) {
      const auto state = weak.lock(); if (!state) return;
      const auto shell = state->browsers.find(state->shell_id);
      if (shell != state->browsers.end())
        shell->second->GetMainFrame()->ExecuteJavaScript("document.querySelector('.toolbar-extensions .extension-button')?.click()", "sol://ui/index.html", 0);
      CefPostDelayedTask(TID_UI, base::BindOnce([](std::weak_ptr<State> weak) {
        const auto state = weak.lock(); if (!state) return;
        state->pin_result->SetBool("popup_opened", state->extension_popup && state->extension_popup->is_open() && state->extension_popup->id() == state->pin_id);
        if (state->extension_popup) state->extension_popup->Close();
        PinExtension(state, state->pin_id, false);
        state->pin_result->SetBool("unpinned_hidden", PinnedRow(state) == nullptr);
        state->pin_result->SetBool("file", std::filesystem::exists(state->profile / "BrowserData" / "pinned_extensions.json"));
        state->Record("pin_probe_result", state->pin_result); state->pin_done = true;
      }, weak), 2500);
    }, std::weak_ptr<State>(state)), 1200);
  }, std::weak_ptr<State>(state)), 1500);
}
}  // namespace red
