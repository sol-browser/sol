#pragma once
// Workspace snapshot traffic of one window (State::snapshot_counters) as a
// dictionary for the memory report and the sidebar performance probe.
#include "app/state.h"
#include "include/cef_values.h"

namespace red {
inline CefRefPtr<CefDictionaryValue> SnapshotCountersDictionary(const State& state) {
  const auto& c = state.snapshot_counters;
  auto result = CefDictionaryValue::Create();
  // Doubles: CEF integers are 32-bit and these only grow.
  result->SetDouble("publishRequests", static_cast<double>(c.publish_requests));
  result->SetDouble("publishes", static_cast<double>(c.publishes));
  result->SetDouble("layoutPublishes", static_cast<double>(c.layout_publishes));
  result->SetDouble("themePublishes", static_cast<double>(c.theme_publishes));
  result->SetDouble("snapshots", static_cast<double>(c.snapshots));
  result->SetDouble("snapshotMicroseconds", static_cast<double>(c.snapshot_microseconds));
  result->SetDouble("shellMessages", static_cast<double>(c.shell_messages));
  result->SetDouble("shellBytes", static_cast<double>(c.shell_bytes));
  result->SetDouble("peekMessages", static_cast<double>(c.peek_messages));
  result->SetDouble("peekBytes", static_cast<double>(c.peek_bytes));
  result->SetDouble("lastBytes", static_cast<double>(c.last_bytes));
  return result;
}
}  // namespace red
