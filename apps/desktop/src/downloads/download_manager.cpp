#include "downloads/download_manager.h"
#include "app/session.h"
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include "downloads/download_names.h"
#include <chrono>
#include "history/history_service.h"
#include "include/base/cef_callback.h"
#include "include/cef_parser.h"
#include "include/wrapper/cef_closure_task.h"
#include "workspace/controller.h"

namespace red {
namespace {
std::string Origin(const std::string& url) {
  CefURLParts parts;
  if (!CefParseURL(url, parts)) return "";
  std::string host = CefString(&parts.host).ToString();
  if (host.starts_with("www.")) host.erase(0, 4);
  return host.substr(0, 120);
}
}  // namespace

DownloadManager::DownloadManager(std::weak_ptr<State> state, std::filesystem::path directory)
    : state_(std::move(state)), directory_(std::move(directory)) {
  if (const auto owner=state_.lock()) session_=owner->session;
}
std::shared_ptr<State> DownloadManager::Owner() const {
  if (const auto state=state_.lock()) return state;
  if (const auto session=session_.lock()) for (const auto& window : session->Windows())
    if (!window->private_window && window->workspace && window->window) return window;
  return nullptr;
}

std::filesystem::path DownloadManager::DefaultDirectory() {
  PWSTR raw = nullptr;
  std::filesystem::path result;
  if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Downloads, KF_FLAG_DEFAULT, nullptr, &raw))) result = raw;
  CoTaskMemFree(raw);
  return result;
}

bool DownloadManager::OnBeforeDownload(CefRefPtr<CefDownloadItem> item, const CefString& suggested_name,
                                       CefRefPtr<CefBeforeDownloadCallback> callback) {
  auto directory = directory_;
  bool ask = false;
  if (const auto state = Owner(); state && !state->probe && state->session && state->session->preferences) {
    const auto& prefs = state->session->preferences->Get();
    if (!prefs.download_directory.empty()) directory = prefs.download_directory;
    ask = prefs.ask_download;
  }
  std::error_code error;
  if (directory.empty() || (!std::filesystem::is_directory(directory, error) &&
                             !std::filesystem::create_directories(directory, error))) {
    // Returning true without continuing cancels; never fall back to Chrome's own UI.
    if (auto state = Owner(); state && state->workspace) state->workspace->Notify("The download could not start: the Downloads folder is unavailable.");
    return true;
  }
  const auto name = SanitizeDownloadName(suggested_name.ToWString());
  const auto path = UniqueDownloadPath(directory, name, [](const std::filesystem::path& p) {
    std::error_code ignored; return std::filesystem::exists(p, ignored);
  });
  auto& entry = entries_[item->GetId()];
  if (!entry.id) order_.push_back(item->GetId());
  entry.id = item->GetId();
  entry.name = CefString(path.filename().wstring()).ToString();
  entry.path = path;
  entry.origin = Origin(item->GetURL().ToString());
  entry.total = item->GetTotalBytes();
  ++started_;
  callback->Continue(path.wstring(), ask);
  if (auto state = Owner()) {
    auto details = CefDictionaryValue::Create(); details->SetInt("id", static_cast<int>(entry.id));
    if (state->probe) details->SetString("file", entry.name);
    state->Record("download_started", details);
  }
  SchedulePublish();
  return true;
}

void DownloadManager::OnUpdated(CefRefPtr<CefDownloadItem> item, CefRefPtr<CefDownloadItemCallback> callback) {
  const auto found = entries_.find(item->GetId());
  // Updates can precede OnBeforeDownload; only accepted downloads are shown.
  if (found == entries_.end()) return;
  auto& entry = found->second;
  entry.received = item->GetReceivedBytes();
  entry.total = item->GetTotalBytes();
  entry.speed = item->GetCurrentSpeed();
  const std::string previous = entry.state;
  entry.state = item->IsComplete() ? "complete" : item->IsCanceled() ? "canceled" : item->IsInterrupted() ? "interrupted" :
      item->IsPaused() ? "paused" : "in_progress";
  entry.control = entry.state == "in_progress" || entry.state == "paused" ? callback : nullptr;
  if (const auto full = item->GetFullPath().ToWString(); !full.empty()) {
    entry.path = full; entry.name = CefString(entry.path.filename().wstring()).ToString();
  }
  if (entry.state != previous) if (auto state = Owner()) {
    auto details = CefDictionaryValue::Create(); details->SetInt("id", static_cast<int>(entry.id));
    details->SetString("state", entry.state); details->SetDouble("received", static_cast<double>(entry.received));
    if (entry.state == "interrupted") details->SetInt("reason", item->GetInterruptReason());
    state->Record("download_state", details);
  }
  const bool finished = entry.state == "complete" || entry.state == "canceled" || entry.state == "interrupted";
  if (finished && entry.state != previous) Persist(entry);
  SchedulePublish();
}

CefRefPtr<CefListValue> DownloadManager::List() const {
  auto list = CefListValue::Create();
  size_t index = 0;
  for (auto it = order_.rbegin(); it != order_.rend() && index < 30; ++it) {
    const auto found = entries_.find(*it);
    if (found == entries_.end()) continue;
    const auto& entry = found->second;
    auto row = CefDictionaryValue::Create();
    row->SetInt("id", static_cast<int>(entry.id));
    row->SetString("name", entry.name); row->SetString("origin", entry.origin); row->SetString("state", entry.state);
    // Doubles: byte counts exceed 32-bit ints.
    row->SetDouble("received", static_cast<double>(std::max<int64_t>(0, entry.received)));
    row->SetDouble("total", static_cast<double>(std::max<int64_t>(-1, entry.total)));
    row->SetDouble("speed", static_cast<double>(std::max<int64_t>(0, entry.speed)));
    list->SetDictionary(index++, row);
  }
  return list;
}

int DownloadManager::Control(uint32_t id, const std::string& action, std::string& error) {
  const auto found = entries_.find(id);
  if (found == entries_.end()) { error = "Unknown download"; return 404; }
  auto& entry = found->second;
  const bool active = entry.state == "in_progress" || entry.state == "paused";
  if (action == "cancel" || action == "pause" || action == "resume") {
    if (!active || !entry.control) { error = "Download is not active"; return 409; }
    if (action == "cancel") entry.control->Cancel();
    else if (action == "pause") entry.control->Pause();
    else entry.control->Resume();
    return 0;
  }
  if (action == "open" || action == "show" || action == "copy" || action == "recycle") {
    if (entry.state != "complete") { error = "The file is not available"; return 409; }
    if (const int code = FileAction(entry.path, action, error)) return code;
    if (action == "recycle") { Forget(entry); entries_.erase(found); std::erase(order_, id); SchedulePublish(); }
    return 0;
  }
  if (action == "remove") {
    if (active) { error = "Cancel the download first"; return 409; }
    Forget(entry); entries_.erase(found); std::erase(order_, id); SchedulePublish();
    return 0;
  }
  error = "Unknown download action"; return 400;
}

int DownloadManager::FileAction(const std::filesystem::path& path, const std::string& action, std::string& error) {
  std::error_code ignored;
  if (!std::filesystem::is_regular_file(path, ignored)) { error = "The file is not available"; return 409; }
  if (action == "open" || action == "show") {
    // A person's explicit click. Windows applies its own Mark-of-the-Web checks
    // (SmartScreen) when opening files downloaded from the internet.
    const auto result = action == "open"
        ? ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, path.parent_path().c_str(), SW_SHOWNORMAL)
        : ShellExecuteW(nullptr, L"open", L"explorer.exe", (L"/select,\"" + path.wstring() + L"\"").c_str(), nullptr, SW_SHOWNORMAL);
    if (reinterpret_cast<INT_PTR>(result) <= 32) { error = "Windows could not open the file"; return 500; }
    return 0;
  }
  const std::wstring wide = path.wstring();
  if (action == "copy") {
    // The file itself, as Ctrl+C in File Explorer: CF_HDROP with a double-NUL list.
    const size_t bytes = sizeof(DROPFILES) + (wide.size() + 2) * sizeof(wchar_t);
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, bytes);
    if (!memory) { error = "Could not copy the file"; return 500; }
    auto* drop = static_cast<DROPFILES*>(GlobalLock(memory));
    drop->pFiles = sizeof(DROPFILES); drop->fWide = TRUE;
    std::copy(wide.begin(), wide.end(), reinterpret_cast<wchar_t*>(reinterpret_cast<char*>(drop) + sizeof(DROPFILES)));
    GlobalUnlock(memory);
    if (!OpenClipboard(nullptr)) { GlobalFree(memory); error = "The clipboard is busy"; return 409; }
    EmptyClipboard();
    if (!SetClipboardData(CF_HDROP, memory)) { GlobalFree(memory); CloseClipboard(); error = "Could not copy the file"; return 500; }
    CloseClipboard();
    return 0;
  }
  if (action == "recycle") {
    // Reversible: the Recycle Bin, never a permanent delete.
    std::wstring from = wide; from.push_back(L'\0');
    SHFILEOPSTRUCTW operation{}; operation.wFunc = FO_DELETE; operation.pFrom = from.c_str();
    operation.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI;
    if (SHFileOperationW(&operation) != 0 || operation.fAnyOperationsAborted) { error = "Could not move the file to the Recycle Bin"; return 500; }
    return 0;
  }
  error = "Unknown file action"; return 400;
}
void DownloadManager::ForgetRecord(int64_t record_id) {
  for (auto it = entries_.begin(); it != entries_.end(); ++it) {
    if (it->second.record_id != record_id) continue;
    std::erase(order_, it->first); entries_.erase(it); SchedulePublish(); return;
  }
}
CefRefPtr<CefListValue> DownloadManager::ActiveList() const {
  auto list = CefListValue::Create();
  for (auto it = order_.rbegin(); it != order_.rend(); ++it) {
    const auto found = entries_.find(*it);
    if (found == entries_.end() || (found->second.state != "in_progress" && found->second.state != "paused")) continue;
    const auto& entry = found->second;
    auto row = CefDictionaryValue::Create();
    row->SetInt("id", static_cast<int>(entry.id)); row->SetString("name", entry.name); row->SetString("origin", entry.origin);
    row->SetString("state", entry.state);
    row->SetDouble("received", static_cast<double>(std::max<int64_t>(0, entry.received)));
    row->SetDouble("total", static_cast<double>(std::max<int64_t>(-1, entry.total)));
    row->SetDouble("speed", static_cast<double>(std::max<int64_t>(0, entry.speed)));
    list->SetDictionary(list->GetSize(), row);
  }
  return list;
}
void DownloadManager::ClearFinished(bool saved) {
  std::erase_if(order_, [&](uint32_t id) {
    const auto found = entries_.find(id);
    const bool done = found == entries_.end() || (found->second.state != "in_progress" && found->second.state != "paused");
    if (done && found != entries_.end()) entries_.erase(found);
    return done;
  });
  if (!saved) { SchedulePublish(); return; }
  if (history_) history_->ClearDownloads();
  SchedulePublish();
}

void DownloadManager::Persist(Entry& entry) {
  if (!history_) return;  // Only persistent profiles bind a history service.
  const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
  DownloadRecord record{entry.record_id, entry.name, entry.origin, entry.state, entry.path, entry.received, entry.total, now};
  history_->SaveDownload(record, [weak = weak_from_this(), id = entry.id](int64_t stored) {
    auto self = weak.lock(); if (!self || !stored) return;
    if (const auto found = self->entries_.find(id); found != self->entries_.end()) found->second.record_id = stored;
  });
}
void DownloadManager::Forget(const Entry& entry) {
  if (!entry.record_id) return;
  if (history_) history_->DeleteDownload(entry.record_id);
}

void DownloadManager::SchedulePublish() {
  if (publish_pending_) return;
  publish_pending_ = true;
  // Progress callbacks are frequent; four snapshots per second keep the UI live.
  CefPostDelayedTask(TID_UI, base::BindOnce([](std::weak_ptr<DownloadManager> weak) {
    auto self = weak.lock();
    if (!self) return;
    self->publish_pending_ = false;
    if (auto state = self->Owner(); state && state->workspace) {
      if (state->session) {
        for (const auto& window : state->session->Windows())
          if (window->workspace && window->downloads.get()==self.get()) window->workspace->Refresh();
      } else state->workspace->Refresh();
    }
  }, weak_from_this()), 250);
}
}  // namespace red
