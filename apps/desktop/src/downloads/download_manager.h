#pragma once
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include "app/state.h"
#include "include/cef_download_handler.h"
#include "storage/history_store.h"

namespace red {
// Browser-owned downloads for workspace pages. Files go straight to one
// directory (the user's Downloads folder; probes may override it) under a
// sanitized, never-overwriting name. The trusted UI sees names, origins,
// progress and state by ID — never file system paths — and asks native code to
// cancel, pause, resume, open or reveal a download.
class DownloadManager : public std::enable_shared_from_this<DownloadManager> {
 public:
  DownloadManager(std::weak_ptr<State> state, std::filesystem::path directory);
  // Default directory: FOLDERID_Downloads. Empty if Windows cannot provide it.
  static std::filesystem::path DefaultDirectory();
  bool OnBeforeDownload(CefRefPtr<CefDownloadItem> item, const CefString& suggested_name,
                        CefRefPtr<CefBeforeDownloadCallback> callback);
  void OnUpdated(CefRefPtr<CefDownloadItem> item, CefRefPtr<CefDownloadItemCallback> callback);
  // This session's downloads, newest first, at most 30, for the workspace
  // snapshot (the popover). Earlier sessions live on the downloads page only.
  CefRefPtr<CefListValue> List() const;
  int started() const { return started_; }
  // action: cancel | pause | resume | open | show | copy | recycle | remove. Returns an HTTP-like
  // status (0 on success) and an error message for the trusted UI.
  int Control(uint32_t id, const std::string& action, std::string& error);
  // Drops finished downloads from this session's list (the popover). With
  // |saved|, also clears the downloads page's saved list (files stay).
  void ClearFinished(bool saved);
  // open | show | copy | recycle on a finished file (a person's explicit action).
  static int FileAction(const std::filesystem::path& path, const std::string& action, std::string& error);
  // A saved download was removed elsewhere (downloads page): drop it from the list.
  void ForgetRecord(int64_t record_id);
  // In-progress and paused downloads of this session, for the downloads page.
  CefRefPtr<CefListValue> ActiveList() const;
 private:
  struct Entry {
    uint32_t id = 0;
    std::string name, origin, state = "in_progress";
    std::filesystem::path path;
    int64_t received = 0, total = -1, speed = 0;
    CefRefPtr<CefDownloadItemCallback> control;
    int64_t record_id = 0;  // Row in history.sqlite3 once finished.
  };
  void Persist(Entry& entry);
  void Forget(const Entry& entry);
  void SchedulePublish();
  std::weak_ptr<State> state_;
  std::filesystem::path directory_;
  std::map<uint32_t, Entry> entries_;
  std::vector<uint32_t> order_;  // Oldest first.
  int started_ = 0;
  bool publish_pending_ = false;
};
}  // namespace red
