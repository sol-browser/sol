#include "app/state.h"
#include <windows.h>
#include "include/cef_parser.h"

namespace red {
void State::Record(const std::string& event, CefRefPtr<CefDictionaryValue> details) {
  const bool trace = tab_trace.is_open() && tab_trace_rows < 4096 &&
      (event == "embedded_activated" || event == "window_geometry" || event.starts_with("command_") || event.starts_with("sidebar_peek_") || event.starts_with("sidebar_menu_") || event.starts_with("popup_") || event.starts_with("settings_") || event.starts_with("external_launch_") || event.starts_with("page_transition_") || event.starts_with("tab_switch_") || event.starts_with("page_load_") || event.starts_with("tab_drag_preview_"));
  if (!evidence.is_open() && !trace) return;
  auto value = CefValue::Create();
  auto row = CefDictionaryValue::Create();
  row->SetString("event", event);
  row->SetDouble("tick_ms", static_cast<double>(GetTickCount64()));
  if (details) row->SetDictionary("details", details);
  value->SetDictionary(row);
  if (evidence.is_open()) {
    evidence << CefWriteJSON(value, JSON_WRITER_DEFAULT).ToString() << '\n';
    evidence.flush();
  }
  if (trace) {
    // Numeric/boolean metadata only, even if an event gains text fields later.
    auto metrics = CefDictionaryValue::Create();
    // SetDictionary above transfers ownership, invalidating the input wrapper.
    const auto recorded = row->GetDictionary("details");
    if (recorded) {
      CefDictionaryValue::KeyList keys; recorded->GetKeys(keys);
      for (const auto& key : keys) {
        const auto type = recorded->GetType(key);
        if (type == VTYPE_INT || type == VTYPE_DOUBLE || type == VTYPE_BOOL) metrics->SetValue(key, recorded->GetValue(key));
      }
    }
    row->SetDictionary("details", metrics);
    tab_trace << CefWriteJSON(value, JSON_WRITER_DEFAULT).ToString() << '\n';
    tab_trace.flush(); ++tab_trace_rows;
  }
}
}  // namespace red
