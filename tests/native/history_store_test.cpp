#include "storage/history_store.h"
#include "history/title_target.h"
#include <windows.h>
#include <cstdio>
#include <sqlite3.h>

namespace {
int failures = 0;
void Check(bool test, const char* name) { if (!test) { ++failures; std::fprintf(stderr, "FAIL: %s\n", name); } }
constexpr int64_t kMinute = 60 * 1000;
}
int wmain() {
  {
    using red::TitleBelongsToVisit;
    // Figma: the visit is recorded before Figma adds the file name and node to the address.
    Check(TitleBelongsToVisit("https://www.figma.com/design/KEY?fuid=1", "https://www.figma.com/design/KEY/Abyss-Cup?node-id=0-1") &&
          TitleBelongsToVisit("https://www.figma.com/design/KEY/Untitled?t=a", "https://www.figma.com/design/KEY/Untitled?node-id=0-1&t=a") &&
          TitleBelongsToVisit("https://mail.google.com/mail/u/0/", "https://mail.google.com/mail/u/0/#inbox/abc"),
          "in-place address change keeps the title on the page's visit");
    Check(!TitleBelongsToVisit("https://www.youtube.com/watch?v=A", "https://www.youtube.com/watch?v=B") &&
          !TitleBelongsToVisit("https://www.figma.com/design/KEY", "https://www.figma.com/design/KEY2/Other") &&
          !TitleBelongsToVisit("https://www.figma.com/design/KEY", "https://figma.com/design/KEY/Abyss") &&
          !TitleBelongsToVisit("https://x.com/home", "https://x.com/home/status/1") &&
          !TitleBelongsToVisit("", "https://www.figma.com/design/KEY") &&
          !TitleBelongsToVisit("https://www.figma.com/design/KEY", "https://www.figma.com/design/KEY"),
          "other pages, origins and one-segment paths keep their own titles");
  }
  const auto base = std::filesystem::temp_directory_path() / (L"red-history-" + std::to_wstring(GetCurrentProcessId()));
  const auto path = base.wstring() + L".sqlite3", foreign = base.wstring() + L"-foreign.sqlite3";
  {
    sqlite3* db = nullptr; sqlite3_open16(foreign.c_str(), &db);
    sqlite3_exec(db, "CREATE TABLE other(value);", nullptr, nullptr, nullptr); sqlite3_close(db);
    red::HistoryStore store; Check(!store.Open(foreign), "refuse an unrelated database");
  }
  {
    red::HistoryStore store;
    Check(store.Open(path), "open new history");
    const int64_t t = 1'800'000'000'000;
    const auto a = store.AddVisit("https://example.com/", "Example", t);
    Check(store.AddVisit("https://example.com/", "", t + kMinute) == a, "repeat within 30 minutes refreshes");
    Check(store.Visits(0, 10, "").size() == 1 && store.Visits(0, 10, "")[0].title == "Example", "refresh keeps title when new title is empty");
    store.AddVisit("https://news.test/a", "Breaking 100% news_", t + 2 * kMinute);
    store.AddVisit("https://example.com/", "Example again", t + 3 * kMinute);  // Not the latest URL: new row.
    Check(store.Visits(0, 10, "").size() == 3, "different URL in between adds a row");
    store.AddVisit("https://example.com/", "Later", t + 40 * kMinute);
    Check(store.Visits(0, 10, "").size() == 4, "after 30 minutes a new row");
    Check(store.Visits(0, 10, "")[0].url == "https://example.com/" && store.Visits(0, 10, "")[0].visited_at == t + 40 * kMinute, "newest first");
    Check(store.Visits(t + 3 * kMinute, 10, "").size() == 2, "paging before a time");
    Check(store.Visits(0, 10, "100%").size() == 1 && store.Visits(0, 10, "news_").size() == 1, "wildcards match literally");
    Check(store.Visits(0, 10, "%").size() == 1 && store.Visits(0, 10, "_").size() == 1, "bare wildcard is plain text");
    Check(store.Visits(0, 10, "NEWS.TEST").size() == 1, "case-insensitive ASCII match");
    store.AddVisit("https://old.test/", "Old", t - 2 * 24 * 60 * kMinute);  // Pruned below.
    const auto days = store.Days(10, "");
    Check(days.size() == 2 && days[0].count == 4 && days[0].latest == t + 40 * kMinute && days[1].count == 1 &&
          days[1].latest == t - 2 * 24 * 60 * kMinute && days[0].date > days[1].date && days[0].date.size() == 10, "days: newest first, counts, latest visit");
    Check(store.Days(10, "news").size() == 1 && store.Days(10, "news")[0].count == 1 && store.Days(1, "").size() == 1, "days follow the search and the limit");
    const auto suggestions = store.Suggest("example", 10);
    Check(suggestions.size() == 1 && suggestions[0].title == "Later", "one suggestion per URL, latest title");
    store.UpdateTitle("https://news.test/a", "Updated");
    Check(store.Visits(0, 10, "Updated").size() == 1, "title update");
    Check(store.DeleteVisit(store.Visits(0, 10, "Updated")[0].id) && store.Visits(0, 10, "news").empty(), "delete one visit");
    Check(!store.DeleteVisit(999999), "delete unknown visit");
    Check(store.DeleteRange(t + 30 * kMinute, t + 50 * kMinute) == 1, "delete a time range");
    store.Prune(t + 2 * kMinute);
    Check(store.Visits(0, 10, "").size() == 1, "prune old visits");

    red::DownloadRecord record{0, "report.pdf", "example.com", "complete", L"C:\\Downloads\\report.pdf", 10, 10, t};
    const auto id = store.SaveDownload(record);
    Check(id > 0, "insert download");
    record.id = id; record.state = "interrupted";
    Check(store.SaveDownload(record) == id && store.Downloads(10).size() == 1 && store.Downloads(10)[0].state == "interrupted", "update download in place");
    Check(store.Downloads(10)[0].path == std::filesystem::path(L"C:\\Downloads\\report.pdf"), "path round trip");
    red::DownloadRecord unicode{0, "отчёт.pdf", "пример.рф", "complete", L"C:\\Загрузки\\отчёт.pdf", 1, 1, t + 1};
    store.SaveDownload(unicode);
    Check(store.Downloads(10)[0].path == std::filesystem::path(L"C:\\Загрузки\\отчёт.pdf"), "non-ASCII path round trip");
    Check(store.SaveDownload({0, "bad", "", "in_progress", L"C:\\x", 0, 0, t}) == 0, "only finished states are stored");
    for (int i = 0; i < 40; ++i) store.SaveDownload({0, "f" + std::to_string(i), "", "complete", L"C:\\f", 0, 0, t + 10 + i});
    Check(store.Downloads(100).size() == 42 && store.Downloads(30).size() == 30, "page keeps all; popover takes 30");
    Check(store.DownloadsBefore(t + 20, 100).size() == 12 && store.Download(store.Downloads(1)[0].id).has_value() && !store.Download(999999), "paging and lookup");
    store.DeleteDownload(store.Downloads(1)[0].id);
    Check(store.Downloads(100).size() == 41, "delete one download");
    store.ClearDownloads();
    Check(store.Downloads(100).empty(), "clear downloads");
    store.SaveFavicon("example.com", std::string("PNG\0data", 9), t);
    store.SaveFavicon("example.com", std::string("PNG\0newer", 10), t + 1);
    store.SaveFavicon("other.test", "png", t + 2);
    store.SaveFavicon("", "png", t); store.SaveFavicon("big.test", std::string(17 * 1024, 'x'), t);
    auto icons = store.Favicons(10);
    Check(icons.size() == 2 && icons[0].first == "other.test" && icons[1].second == std::string("PNG\0newer", 10), "favicons by host, binary-safe, newest first, bounded size");
  }
  {
    red::HistoryStore again; Check(again.Open(path), "reopen existing history");
    Check(again.Visits(0, 10, "").size() == 1, "visits persist");
    Check(again.Favicons(10).size() == 2, "favicons persist");
  }
  std::error_code ignored;
  for (const auto& file : {path, path + L"-wal", path + L"-shm", foreign}) std::filesystem::remove(file, ignored);
  if (failures) return 1;
  std::puts("history store: ok");
  return 0;
}
