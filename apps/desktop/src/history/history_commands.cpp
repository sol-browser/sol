#include "history/history_commands.h"
#include <chrono>
#include "history/favicon_cache.h"
#include "history/history_service.h"
#include "app/preferences.h"
#include "app/session.h"
#include "app/browsing_profiles.h"
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
std::string VisitsJson(const std::vector<Visit>& visits, bool more, bool hour12, const std::shared_ptr<FaviconCache>& favicons) {
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
  auto result = CefDictionaryValue::Create(); result->SetList("visits", list); result->SetBool("more", more); result->SetBool("hour12", hour12);
  auto value = CefValue::Create(); value->SetDictionary(result); return CefWriteJSON(value, JSON_WRITER_DEFAULT);
}
}  // namespace
bool HourClock(const std::shared_ptr<State>& state) {
  const auto prefs = state->session ? state->session->preferences : nullptr;
  return TwelveHourClock(prefs ? prefs->Get().clock : "system");
}

bool HandleHistoryCommand(const std::shared_ptr<State>& state, const std::string& method,
                          CefRefPtr<CefDictionaryValue> d, CefRefPtr<CefMessageRouterBrowserSide::Callback> cb,
                          std::function<void()> opened, std::shared_ptr<BrowsingProfile> profile) {
  if (!method.starts_with("history.")) return false;
  const auto history = profile ? profile->history : state->history;
  const auto favicons = profile ? profile->favicons : state->favicons;
  if (!history || !state->workspace) { cb->Failure(503, "History unavailable"); return true; }
  int64_t number = 0;
  if (method == "history.list" && d->GetSize() == 3 && WholeNumber(d->GetValue("before"), number) &&
      d->GetType("query") == VTYPE_STRING && d->GetString("query").length() <= 200) {
    constexpr size_t kPage = 60;
    const bool hour12 = HourClock(state);
    history->Visits(number, kPage + 1, d->GetString("query"), [cb, hour12, favicons](std::vector<Visit> visits) {
      const bool more = visits.size() > kPage; if (more) visits.pop_back();
      cb->Success(VisitsJson(visits, more, hour12, favicons));
    });
    return true;
  }
  // The page's day rail: every day with history, not only the loaded pages.
  if (method == "history.days" && d->GetSize() == 2 && d->GetType("query") == VTYPE_STRING && d->GetString("query").length() <= 200) {
    history->Days(400, d->GetString("query"), [cb](std::vector<VisitDay> days) {
      auto list = CefListValue::Create();
      for (const auto& day : days) {
        auto row = CefDictionaryValue::Create(); row->SetString("date", day.date);
        row->SetDouble("count", static_cast<double>(day.count)); row->SetDouble("latest", static_cast<double>(day.latest));
        list->SetDictionary(list->GetSize(), row);
      }
      auto result = CefDictionaryValue::Create(); result->SetList("days", list);
      auto value = CefValue::Create(); value->SetDictionary(result); cb->Success(CefWriteJSON(value, JSON_WRITER_DEFAULT));
    });
    return true;
  }
  if (method == "history.delete" && d->GetSize() == 2 && WholeNumber(d->GetValue("id"), number) && number >= 1) {
    history->DeleteVisit(number, [cb](bool removed) { if (removed) cb->Success("{}"); else cb->Failure(404, "Visit is no longer in history"); });
    return true;
  }
  if (method == "history.deleteMany" && d->GetSize() == 2 && d->GetType("ids") == VTYPE_LIST) {
    const auto list = d->GetList("ids");
    std::vector<int64_t> ids;
    for (size_t i = 0; i < list->GetSize() && list->GetSize() <= 500; ++i)
      if (WholeNumber(list->GetValue(i), number) && number >= 1) ids.push_back(number);
    if (ids.empty() || ids.size() != list->GetSize()) { cb->Failure(400, "Invalid history selection"); return true; }
    auto remaining = std::make_shared<size_t>(ids.size()); auto removed = std::make_shared<int>(0);
    for (const auto id : ids) history->DeleteVisit(id, [cb, remaining, removed](bool ok) {
      if (ok) ++*removed;
      if (--*remaining == 0) cb->Success("{\"removed\":" + std::to_string(*removed) + "}");
    });
    return true;
  }
  const auto range = d->GetType("range") == VTYPE_STRING ? d->GetString("range").ToString() : "";
  if (method == "history.clear" && d->GetSize() == 2 && (range == "hour" || range == "day" || range == "week" || range == "all")) {
    const int64_t now = NowMs(), hour = 60LL * 60 * 1000;
    const int64_t from = range == "all" ? 0 : now - (range == "hour" ? hour : range == "day" ? 24 * hour : 7 * 24 * hour);
    history->DeleteRange(from, now + 1, [cb, weak = std::weak_ptr<State>(state)](int removed) {
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
