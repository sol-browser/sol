#pragma once
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <vector>
#include "app/state.h"
#include "storage/tree_store.h"
#include "app/preferences.h"
#include "include/cef_browser.h"
#include "include/cef_devtools_message_observer.h"
#include "include/cef_thread.h"
#include "extensions/pinned_extensions.h"
namespace red {
class AutoArchive;
// One application/profile lifetime; shared workspace data plus isolated tab
// data for standalone windows. Website contexts are shared within each profile.
// Runtime methods run on TID_UI. Window enumeration also serves UI resources
// on the IO thread, so that weak registry has its own lock.
class BoostStore;
class BrowsingProfiles;
// One website page's Boost loader registration (windows/boosts.cpp), UI thread only.
struct BoostSlot {
  CefRefPtr<CefBrowser> page;
  CefRefPtr<CefRegistration> observer;
  int pending = 0;
  std::string identifier;
  // Element picker (Zap/Focus) while the Boost editor waits for a pick; empty when not picking.
  std::string pick;
  bool binding = false;
  std::function<void(CefRefPtr<CefDictionaryValue>)> picked;
};
struct WorkspaceData {
  std::filesystem::path path;
  CefRefPtr<CefThread> worker;
  std::unique_ptr<TreeStore> store;  // Only accessed/destroyed on worker.
  TreeSnapshot tree;               // Only accessed on UI.
  bool ready = false;
};
class AppSession {
 public:
  void Register(const std::shared_ptr<State>& window);
  std::vector<std::shared_ptr<State>> Windows() const;
  bool HasOpenWindows() const;
  void RegisterUi(int browser, const std::shared_ptr<State>& state);
  void UnregisterUi(int browser);
  std::shared_ptr<State> UiOwner(int browser) const;
  void PublishTree(const std::shared_ptr<WorkspaceData>& data, const TreeSnapshot& tree, WorkspaceController* origin);
  void AddWorkspace(const std::shared_ptr<WorkspaceData>& data);
  void StopWorkspaces();
  void ReleaseUnusedWorkspaces();
  std::function<bool(const std::shared_ptr<State>&, const std::string&)> new_window;
  std::function<bool(const std::shared_ptr<State>&)> new_private_window;
  // "Open link in new window" / "... in private window" (page menu): a new window that opens |url|.
  std::function<bool(const std::shared_ptr<State>&, const std::string& url, bool private_window)> new_window_with_link;
  // Moves the live page of |from|'s entry into |to| for |node_id| without
  // reloading it (docs/multi-window.md); false when it cannot move live.
  std::function<bool(const std::shared_ptr<State>& from, const std::shared_ptr<State>& to, const std::string& source_id,
                     const std::string& node_id, bool remove_entry)> transfer_page;
  std::weak_ptr<State> active_window;
  std::shared_ptr<WorkspaceData> workspace = std::make_shared<WorkspaceData>();
  std::shared_ptr<BrowserPreferences> preferences;
  std::shared_ptr<BrowsingProfiles> profiles;
  std::shared_ptr<BoostStore> boosts;  // Per-site CSS (docs/boosts.md).
  std::filesystem::path boosts_path;
  std::map<int, std::shared_ptr<BoostSlot>> boost_slots;
  std::shared_ptr<PinnedExtensions> pinned_extensions;  // Top-bar extension pins (all windows).  // By page browser ID.
  std::shared_ptr<AutoArchive> auto_archive;
 private:
  mutable std::mutex mutex_;
  std::vector<std::weak_ptr<State>> windows_;
  std::map<int, std::weak_ptr<State>> ui_owners_;
  std::vector<std::shared_ptr<WorkspaceData>> data_scopes_{workspace};
};
void QuitIfLastWindow(const std::shared_ptr<State>& state);
bool BrowserIntegrationEnabled(const std::shared_ptr<State>& state, std::string_view id);
void SaveIntegrationPreference(const std::shared_ptr<State>& state, std::string id, bool enabled, BrowserPreferences::Reply done);
void RegisterSessionUi(const std::shared_ptr<State>& state, int browser);
void UnregisterSessionUi(const std::shared_ptr<State>& state, int browser);
}
