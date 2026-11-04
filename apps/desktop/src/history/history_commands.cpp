#include "history/history_commands.h"
#include <chrono>
#include "history/favicon_cache.h"
#include "history/history_service.h"
#include "include/cef_parser.h"
#include "workspace/controller.h"

namespace red {
namespace {
int64_t NowMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}
// JSON numbers arrive as int or double; accept non-negative whole values only.
bool WholeNumber(CefRefPtr<CefValue> v, int64_t& out) {
  const auto type = v ? v->GetType() : VTYPE_NULL;
  const double value = type == VTYPE_INT ? v->GetInt() : type == VTYPE_DOUBLE ? v->GetDouble() : -1;
  if (value < 0 || value > 9007199254740991.0 || value != static_cast<double>(static_cast<int64_t>(value))) return false;
  out = static_cast<int64_t>(value); return true;
}
std::string VisitsJson(const std::vector<Visit>& visits, bool more, const std::shared_ptr<FaviconCache>& favicons) {
  auto list = CefListValue::Create();
  for (const auto& visit : visits) {
    auto row = CefDictionaryValue::Create();
    row->SetDouble("id", static_cast<double>(visit.id)); row->SetString("url", visit.url);
    row->SetString("title", visit.title.substr(0, 400)); row->SetDouble("visitedAt", static_cast<double>(visit.visited_at));
    // Cached site icon (sol://ui/favicon/...), when one was seen for the host.
    CefURLParts parts;
    if (favicons && CefParseURL(visit.url, parts) && parts.host.length)
      if (const auto icon = favicons->Url(FaviconHost(CefString(&parts.host).ToString()))) row->SetString("favicon", *icon);
    list->SetDictionary(list->GetSize(), row);
  }
  auto result = CefDictionaryValue::Create(); result->SetList("visits", list); result->SetBool("more", more);
  auto value = CefValue::Create(); value->SetDictionary(result); return CefWriteJSON(value, JSON_WRITER_DEFAULT);
}
}  // namespace

bool HandleHistoryCommand(const std::shared_ptr<State>& state, const std::string& method,
                          CefRefPtr<CefDictionaryValue> d, CefRefPtr<CefMessageRouterBrowserSide::Callback> cb,
                          std::function<void()> opened) {
  if (!method.starts_with("history.")) return false;
  if (!state->history || !state->workspace) { cb->Failure(503, "History unavailable"); return true; }
  int64_t number = 0;
  if (method == "history.list" && d->GetSize() == 3 && WholeNumber(d->GetValue("before"), number) &&
      d->GetType("query") == VTYPE_STRING && d->GetString("query").length() <= 200) {
    constexpr size_t kPage = 60;
    state->history->Visits(number, kPage + 1, d->GetString("query"), [cb, favicons = state->favicons](std::vector<Visit> visits) {
      const bool more = visits.size() > kPage; if (more) visits.pop_back();
      cb->Success(VisitsJson(visits, more, favicons));
    });
    return true;
  }
  if (method == "history.delete" && d->GetSize() == 2 && WholeNumber(d->GetValue("id"), number) && number >= 1) {
    state->history->DeleteVisit(number, [cb](bool removed) { if (removed) cb->Success("{}"); else cb->Failure(404, "Visit is no longer in history"); });
    return true;
  }
  if (method == "history.deleteMany" && d->GetSize() == 2 && d->GetType("ids") == VTYPE_LIST) {
    const auto list = d->GetList("ids");
    std::vector<int64_t> ids;
    for (size_t i = 0; i < list->GetSize() && list->GetSize() <= 500; ++i)
      if (WholeNumber(list->GetValue(i), number) && number >= 1) ids.push_back(number);
    if (ids.empty() || ids.size() != list->GetSize()) { cb->Failure(400, "Invalid history selection"); return true; }
    auto remaining = std::make_shared<size_t>(ids.size()); auto removed = std::make_shared<int>(0);
    for (const auto id : ids) state->history->DeleteVisit(id, [cb, remaining, removed](bool ok) {
      if (ok) ++*removed;
      if (--*remaining == 0) cb->Success("{\"removed\":" + std::to_string(*removed) + "}");
    });
    return true;
  }
  const auto range = d->GetType("range") == VTYPE_STRING ? d->GetString("range").ToString() : "";
  if (method == "history.clear" && d->GetSize() == 2 && (range == "hour" || range == "day" || range == "week" || range == "all")) {
    const int64_t now = NowMs(), hour = 60LL * 60 * 1000;
    const int64_t from = range == "all" ? 0 : now - (range == "hour" ? hour : range == "day" ? 24 * hour : 7 * 24 * hour);
    state->history->DeleteRange(from, now + 1, [cb, weak = std::weak_ptr<State>(state)](int removed) {
      if (auto s = weak.lock()) { auto data = CefDictionaryValue::Create(); data->SetInt("removed", removed); s->Record("history_cleared", data); }
      cb->Success("{}");
    });
    return true;
  }
  if (method == "history.open" && d->GetSize() == 2 && d->GetType("url") == VTYPE_STRING && HistoryService::Recordable(d->GetString("url"))) {
    // A new Today tab; the controller applies its URL policy too.
    state->workspace->OpenLink(d->GetString("url"), true, [cb, opened](int code, const std::string& error, CefRefPtr<CefDictionaryValue>) {
      if (code) { cb->Failure(code, error); return; }
      cb->Success("{}"); if (opened) opened();
    });
    return true;
  }
  if (method == "history.openPage" && d->GetSize() == 1) {
    state->workspace->OpenHistoryPage(); cb->Success("{}"); if (opened) opened();
    return true;
  }
  cb->Failure(400, "Invalid history command");
  return true;
}
}  // namespace red
