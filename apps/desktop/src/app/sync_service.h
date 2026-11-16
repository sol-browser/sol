#pragma once
#include "include/cef_thread.h"
#include "include/cef_values.h"
#include "sync/sync_folder.h"
#include "sync/sync_pass.h"
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>

namespace red {
class AppSession;
struct State;
// Cloud-folder sync inside Sol (docs/sync.md, "In Sol"). One per app
// session, UI thread only; file IO, crypto and reading passwords run on its
// own worker thread. A pass reads the workspace and every browsing
// profile's passwords, merges with the other PCs' files, applies the result
// through the workspace controller and the password manager page, then
// writes this PC's file when something changed.
class SyncService : public std::enable_shared_from_this<SyncService> {
 public:
  static std::shared_ptr<SyncService> Start(const std::shared_ptr<AppSession>& session, std::filesystem::path profile_root);
  void Stop();
  // The shared workspace tree was published; a pass follows 30 s after the
  // last change of synced content.
  void TreeChanged();
  // Settings → Sync.
  using Done = std::function<void(const std::string& error)>;
  CefRefPtr<CefDictionaryValue> Status() const;
  // Whether |folder| already holds a Sol vault (join) or not (create).
  bool HasVault(const std::filesystem::path& folder) const;
  void SetUp(std::filesystem::path folder, std::string passphrase, sync::JoinMode mode, Done done);
  void TurnOff(Done done);
  void SyncNow();
  // Settings refreshes when status changes.
  std::function<void()> on_status;

 private:
  SyncService(std::weak_ptr<AppSession> session, std::filesystem::path root);
  void Schedule(int64_t delay_ms);
  void Poll();
  void RunPass();
  void Applied(std::shared_ptr<struct PassJob> job);
  void ApplyIcons(const struct PassJob& job, const std::shared_ptr<State>& window);
  void Finish(std::shared_ptr<struct PassJob> job, sync::PassStatus status, const std::string& error);
  void Notify();
  std::shared_ptr<State> MainWindow() const;
  std::weak_ptr<AppSession> session_;
  std::filesystem::path root_, dir_;
  CefRefPtr<CefThread> worker_;
  // Worker thread only.
  std::shared_ptr<struct SyncWorkerState> worker_state_;
  // UI thread.
  bool configured_ = false, running_ = false, again_ = false, stopped_ = false;
  std::filesystem::path folder_;
  std::string device_, error_, join_;
  int64_t last_success_ms_ = 0, schedule_generation_ = 0, scheduled_at_ = 0;
  int devices_ = 0, unreadable_ = 0, passwords_ = 0, records_ = 0;
  size_t tree_hash_ = 0;
  std::string poll_signature_;
};
}  // namespace red
