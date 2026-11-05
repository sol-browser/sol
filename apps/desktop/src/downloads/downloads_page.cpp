#include "downloads/downloads_page.h"
#include "downloads/download_manager.h"
#include "history/history_commands.h"
#include "history/history_service.h"
#include "include/cef_parser.h"

namespace red {
namespace {
bool WholeNumber(CefRefPtr<CefValue> v, int64_t& out) {
  const auto type = v ? v->GetType() : VTYPE_NULL;
  const double value = type == VTYPE_INT ? v->GetInt() : type == VTYPE_DOUBLE ? v->GetDouble() : -1;
  if (value < 0 || value > 9007199254740991.0 || value != static_cast<double>(static_cast<int64_t>(value))) return false;
  out = static_cast<int64_t>(value); return true;
}
std::string Json(CefRefPtr<CefDictionaryValue> d) { auto v = CefValue::Create(); v->SetDictionary(d); return CefWriteJSON(v, JSON_WRITER_DEFAULT); }
}  // namespace

bool HandleDownloadsPageCommand(const std::shared_ptr<State>& state, const std::string& method,
                                CefRefPtr<CefDictionaryValue> d, CefRefPtr<CefMessageRouterBrowserSide::Callback> cb) {
  if (!method.starts_with("downloads.")) return false;
  if (!state->history || !state->downloads) { cb->Failure(503, "Downloads unavailable"); return true; }
  int64_t number = 0;
  if (method == "downloads.page" && d->GetSize() == 2 && WholeNumber(d->GetValue("before"), number)) {
    constexpr size_t kPage = 60;
    const bool first = number == 0;
    state->history->DownloadsBefore(number, kPage + 1, [cb, first, hour12 = HourClock(state), weak = std::weak_ptr<State>(state)](std::vector<DownloadRecord> records) {
      auto state = weak.lock(); if (!state || !state->downloads) { cb->Failure(503, "Downloads unavailable"); return; }
      const bool more = records.size() > kPage; if (more) records.pop_back();
      auto items = CefListValue::Create();
      for (const auto& r : records) {
        std::error_code ignored;
        auto row = CefDictionaryValue::Create();
        row->SetDouble("id", static_cast<double>(r.id)); row->SetString("name", r.name); row->SetString("origin", r.origin);
        row->SetString("state", r.state); row->SetDouble("total", static_cast<double>(std::max<int64_t>(-1, r.total)));
        row->SetDouble("finishedAt", static_cast<double>(r.finished_at));
        // Whether the file is still where it was saved (deleted or moved otherwise).
        row->SetBool("exists", r.state == "complete" && std::filesystem::is_regular_file(r.path, ignored));
        items->SetDictionary(items->GetSize(), row);
      }
      auto result = CefDictionaryValue::Create();
      result->SetList("active", first ? state->downloads->ActiveList() : CefListValue::Create());
      result->SetList("items", items); result->SetBool("more", more); result->SetBool("hour12", hour12);
      cb->Success(Json(result));
    });
    return true;
  }
  const auto action = d->GetType("action") == VTYPE_STRING ? d->GetString("action").ToString() : "";
  if (method == "downloads.file" && d->GetSize() == 3 && WholeNumber(d->GetValue("id"), number) && number >= 1 &&
      (action == "open" || action == "show" || action == "copy" || action == "recycle" || action == "remove")) {
    state->history->Download(number, [cb, action, id = number, weak = std::weak_ptr<State>(state)](std::optional<DownloadRecord> record) {
      auto state = weak.lock(); if (!state || !state->history) { cb->Failure(503, "Downloads unavailable"); return; }
      if (!record) { cb->Failure(404, "Download is no longer in the list"); return; }
      std::string error;
      if (action != "remove") {
        if (record->state != "complete") { cb->Failure(409, "The file is not available"); return; }
        if (const int code = DownloadManager::FileAction(record->path, action, error)) { cb->Failure(code, error); return; }
      }
      // Removing (or recycling) also takes it off the popover list. Never deletes files for "remove".
      if (action == "remove" || action == "recycle") {
        state->history->DeleteDownload(id);
        if (state->downloads) state->downloads->ForgetRecord(id);
      }
      cb->Success("{}");
    });
    return true;
  }
  if (method == "downloads.control" && d->GetSize() == 3 && d->GetType("id") == VTYPE_INT && d->GetInt("id") >= 0 &&
      (action == "pause" || action == "resume" || action == "cancel")) {
    std::string error;
    if (const int code = state->downloads->Control(static_cast<uint32_t>(d->GetInt("id")), action, error)) { cb->Failure(code, error); return true; }
    cb->Success("{}");
    return true;
  }
  if (method == "downloads.clearAll" && d->GetSize() == 1) {
    state->downloads->ClearFinished(true);  // Also clears the saved list.
    cb->Success("{}");
    return true;
  }
  cb->Failure(400, "Invalid downloads command");
  return true;
}
}  // namespace red
