#include "windows/tab_history_menu.h"
#include "web_url.h"
#include <algorithm>
#include <string>
#include <vector>
#include "include/cef_navigation_entry.h"
#include "include/cef_parser.h"
#include "history/favicon_cache.h"
#include "windows/menu_popup.h"
#include "workspace/controller.h"

namespace red {
namespace {
struct Entry { std::string url, title; int index = 0; bool current = false; };
class Collect final : public CefNavigationEntryVisitor {
 public:
  explicit Collect(std::vector<Entry>& out) : out_(out) {}
  bool Visit(CefRefPtr<CefNavigationEntry> entry, bool current, int index, int) override {
    if (entry && entry->IsValid()) out_.push_back({entry->GetURL().ToString(), entry->GetTitle().ToString(), index, current});
    return true;
  }
 private:
  std::vector<Entry>& out_;
  IMPLEMENT_REFCOUNTING(Collect);
};
std::vector<Entry> Entries(CefRefPtr<CefBrowser> page) {
  std::vector<Entry> entries;
  if (page && page->IsValid()) page->GetHost()->GetNavigationEntries(new Collect(entries), false);
  return entries;
}
bool Website(const std::string& url) { return HasWebScheme(url); }
// A short, single-line label: the title, else the address without its scheme.
std::string Label(const Entry& entry) {
  std::string label = entry.title.empty() ? entry.url : entry.title;
  label.erase(0, WebSchemeLength(label));  // The scheme, when the label is an address.
  for (auto& c : label) if (c == '\n' || c == '\r' || c == '\t') c = ' ';
  if (label.size() > 70) {
    size_t cut = 67;
    while (cut > 0 && (static_cast<unsigned char>(label[cut]) & 0xC0) == 0x80) --cut;  // Whole UTF-8 characters.
    label = label.substr(0, cut) + "...";
  }
  return label.empty() ? "Untitled" : label;
}
}  // namespace

void ShowTabHistoryMenu(const std::shared_ptr<State>& state, CefRefPtr<CefBrowser> page, const CefRect& anchor) {
  if (!state->window || !state->workspace) return;
  auto entries = Entries(page);
  if (entries.size() < 2) return;
  int current = 0;
  for (const auto& entry : entries) if (entry.current) current = entry.index;
  if (!state->menu_popup) state->menu_popup = std::make_shared<MenuPopup>(state);
  const std::weak_ptr<State> weak = state;
  const int page_id = page->GetIdentifier();
  MenuPopup::Builder menu;
  // Newest at the top, at most 24 rows, centred on the current entry when longer.
  std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) { return a.index > b.index; });
  constexpr size_t kRows = 24;
  size_t first = 0;
  if (entries.size() > kRows) {
    size_t at = 0; while (at < entries.size() && entries[at].index != current) ++at;
    first = std::min(entries.size() - kRows, at > kRows / 2 ? at - kRows / 2 : 0);
  }
  for (size_t i = first; i < entries.size() && i < first + kRows; ++i) {
    const auto& entry = entries[i];
    std::string image;
    CefURLParts parts;
    if (state->favicons && Website(entry.url) && CefParseURL(entry.url, parts) && parts.host.length)
      if (const auto icon = state->favicons->Url(FaviconHost(CefString(&parts.host).ToString()))) image = *icon;
    const int offset = entry.index - current;
    menu.Add(Label(entry), "tabs", [weak, page_id, offset] {
      const auto s = weak.lock();
      if (!s || !s->content || s->content->GetIdentifier() != page_id || offset == 0) return;
      // CEF has no "go to index": the page's own history does it in one step.
      s->content->GetMainFrame()->ExecuteJavaScript("history.go(" + std::to_string(offset) + ")", s->content->GetMainFrame()->GetURL(), 0);
    }, "", true, entry.current, entry.current ? "" : image);
  }
  menu.Separator();
  menu.Add("Show full history", "history", [weak] { if (const auto s = weak.lock(); s && s->workspace) s->workspace->OpenHistoryPage(); });
  const auto origin = state->window->GetClientAreaBoundsInScreen();
  state->menu_popup->Open(menu, CefPoint(origin.x + anchor.x, origin.y + anchor.y + anchor.height + 6), "tab-history");
}

bool OpenHistoryEntryInNewTab(const std::shared_ptr<State>& state, CefRefPtr<CefBrowser> page, int offset) {
  if (!state->workspace || (offset != -1 && offset != 1)) return false;
  const auto entries = Entries(page);
  int current = -1;
  for (const auto& entry : entries) if (entry.current) current = entry.index;
  for (const auto& entry : entries)
    if (current >= 0 && entry.index == current + offset && Website(entry.url)) {
      state->workspace->OpenLink(entry.url, false, {}, "", page);
      return true;
    }
  return false;
}
}  // namespace red
