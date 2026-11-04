#include "experiments/input_probe.h"
#include <windows.h>
#include "include/base/cef_callback.h"
#include "include/wrapper/cef_closure_task.h"
#include "include/views/cef_browser_view.h"
#include "windows/embedded_content.h"
namespace red {
namespace {
CefRefPtr<CefDictionaryValue> Describe(HWND handle) {
  auto info = CefDictionaryValue::Create();
  info->SetString("handle", std::to_string(reinterpret_cast<uintptr_t>(handle)));
  wchar_t name[256]{}; GetClassNameW(handle, name, 256); info->SetString("class", name);
  info->SetBool("visible", IsWindowVisible(handle) != FALSE);
  info->SetBool("enabled", IsWindowEnabled(handle) != FALSE);
  RECT rect{}; GetWindowRect(handle, &rect);
  info->SetInt("x", rect.left); info->SetInt("y", rect.top);
  info->SetInt("width", rect.right - rect.left); info->SetInt("height", rect.bottom - rect.top);
  info->SetString("parent", std::to_string(reinterpret_cast<uintptr_t>(GetParent(handle))));
  return info;
}
}
void ObserveNativeInput(std::shared_ptr<State> state) {
  if (!state->input_probe || !state->window) return;
  auto data = CefDictionaryValue::Create();
  if (state->embedded) data->SetBool("native_input_area", state->embedded->ActiveOwnsInputArea());
  POINT pointer{}; GetCursorPos(&pointer);
  HWND hit = WindowFromPoint(pointer); DWORD process = 0; GetWindowThreadProcessId(hit, &process);
  // Never inspect other apps: only record handles belonging to this process.
  if (process == GetCurrentProcessId()) data->SetDictionary("hit", Describe(hit));
  GUITHREADINFO gui{}; gui.cbSize = sizeof(gui);
  if (GetGUIThreadInfo(GetCurrentThreadId(), &gui)) {
    data->SetDictionary("focus", Describe(gui.hwndFocus));
    data->SetDictionary("capture", Describe(gui.hwndCapture));
  }
  data->SetDictionary("root", Describe(state->window->GetWindowHandle()));
  auto browsers = CefListValue::Create(); size_t index = 0;
  for (const auto& [id, browser] : state->browsers) {
    auto row = Describe(browser->GetHost()->GetWindowHandle()); row->SetInt("browser_id", id);
    const auto view = CefBrowserView::GetForBrowser(browser);
    if (view) { row->SetBool("view_focus", view->HasFocus()); row->SetBool("view_enabled", view->IsEnabled()); }
    browsers->SetDictionary(index++, row);
  }
  data->SetList("browsers", browsers); state->Record("native_input_observation", data);
  CefPostDelayedTask(TID_UI, base::BindOnce(&ObserveNativeInput, state), 1000);
}
}
