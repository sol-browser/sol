#include "app/memory_report.h"

#include <windows.h>
#include <fstream>
#include <set>

#include "app/session.h"
#include "app/snapshot_counters.h"
#include "app/state.h"
#include "include/cef_parser.h"
#include "integrations/integration_previews.h"
#include "integrations/whatsapp_unread.h"
#include "workspace/controller.h"

namespace red {
namespace {
// Scheme and host only: never the path, query, fragment or credentials of a page.
std::string SiteOf(const std::string& url) {
  const auto scheme = url.find("://");
  if (scheme == std::string::npos) return url.substr(0, url.find(':'));
  auto start = scheme + 3;
  const auto end = url.find_first_of("/?#", start);
  auto host = url.substr(start, end == std::string::npos ? std::string::npos : end - start);
  if (const auto at = host.rfind('@'); at != std::string::npos) host = host.substr(at + 1);
  return url.substr(0, scheme) + "://" + host;
}

std::string RoleOf(const std::shared_ptr<State>& s, int id, const std::map<int, CefRefPtr<CefDictionaryValue>>& tabs) {
  if (id == s->shell_id) return "sol:sidebar-and-shell";
  if (id == s->command_id) return "sol:command-bar";
  if (id == s->panel_id) return "sol:panel";
  if (id == s->integration_panel_id) return "sol:integration-card";
  if (id == s->peek_id) return "sol:floating-sidebar";
  for (int level = 0; level < 3; ++level) if (id == s->menu_id[level]) return "sol:menu-" + std::to_string(level + 1);
  if (id == s->settings_id) return "sol:settings";
  if (id == s->welcome_id) return "sol:welcome";
  if (id == s->boost_id) return "sol:boost-editor";
  if (s->devtools_targets.contains(id)) return "devtools";
  if (s->internal_pages.contains(id)) return "sol:internal-page";
  if (s->popup_browsers.contains(id)) return "website-popup";
  if (s->workspace && s->workspace->IsLinkPeekBrowser(id)) return "link-peek";
  if (s->workspace) if (const auto check = s->workspace->whatsapp_unread(); check && check->worker() && check->worker()->GetIdentifier() == id)
    return "sol:whatsapp-check";
  if (const auto tab = tabs.find(id); tab != tabs.end())
    return tab->second->GetBool("favorite") ? "tab:favorite" : tab->second->GetBool("pinned") ? "tab:pinned" : "tab";
  return "other";
}
}  // namespace

bool WriteMemoryReport(const std::shared_ptr<State>& state, const std::filesystem::path& path) {
  std::error_code error;
  if (!state || !path.is_absolute() || path.extension() != L".json" || std::filesystem::exists(path, error) ||
      !std::filesystem::is_directory(path.parent_path(), error)) return false;
  std::vector<std::shared_ptr<State>> windows;
  if (state->session) windows = state->session->Windows();
  if (windows.empty()) windows.push_back(state);
  auto rows = CefListValue::Create();
  std::set<int> seen;
  const auto add = [&](int window, int id, CefRefPtr<CefBrowser> browser, const std::string& role, bool active, bool audible) {
    if (!browser || !browser->IsValid() || !seen.insert(id).second) return;
    const auto frame = browser->GetMainFrame();
    const auto host = browser->GetHost()->GetWindowHandle();
    auto row = CefDictionaryValue::Create();
    row->SetInt("window", window);
    row->SetInt("browser", id);
    row->SetString("role", role);
    row->SetString("site", frame ? SiteOf(frame->GetURL().ToString()) : "");
    row->SetInt("rendererHostId", frame ? RendererHostId(frame->GetIdentifier().ToString()) : -1);
    row->SetBool("shown", host && IsWindowVisible(host));
    row->SetBool("active", active);
    row->SetBool("audible", audible);
    rows->SetDictionary(rows->GetSize(), row);
  };
  int node_count = 0, live_count = 0;
  auto counters = CefListValue::Create();  // Snapshot traffic since each window opened.
  for (size_t w = 0; w < windows.size(); ++w) {
    const auto& s = windows[w];
    counters->SetDictionary(w, SnapshotCountersDictionary(*s));
    std::map<int, CefRefPtr<CefDictionaryValue>> tabs;
    if (s->workspace) {
      // Keep the snapshot alive: a CEF list read from a dictionary is invalid once its owner is gone.
      const auto snapshot = s->workspace->Snapshot();
      const auto nodes = snapshot->GetList("nodes");
      for (size_t i = 0; nodes && i < nodes->GetSize(); ++i) {
        const auto node = nodes->GetDictionary(i);
        ++node_count;
        if (const auto browser = s->workspace->LiveBrowser(node->GetString("id").ToString())) {
          tabs[browser->GetIdentifier()] = node; ++live_count;
        }
      }
    }
    for (const auto& [id, browser] : s->browsers)
      add(static_cast<int>(w), id, browser, RoleOf(s, id, tabs), s->content == browser, s->audible_frames.contains(id));
    // The background mail and calendar reader is not one of the window's browsers.
    if (s->workspace) if (const auto previews = s->workspace->integration_previews(); previews && previews->worker())
      add(static_cast<int>(w), previews->worker()->GetIdentifier(), previews->worker(), "sol:integration-worker", false, false);
  }
  auto report = CefDictionaryValue::Create();
  report->SetInt("browserProcessId", static_cast<int>(GetCurrentProcessId()));
  report->SetList("browsers", rows);
  report->SetInt("windows", static_cast<int>(windows.size()));
  report->SetInt("tabEntries", node_count);
  report->SetInt("loadedTabs", live_count);
  report->SetList("snapshotCounters", counters);
  auto value = CefValue::Create(); value->SetDictionary(report);
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out << CefWriteJSON(value, JSON_WRITER_PRETTY_PRINT).ToString();
  return static_cast<bool>(out);
}
}  // namespace red
