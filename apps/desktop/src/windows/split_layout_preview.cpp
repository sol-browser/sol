#include "windows/split_layout_preview.h"
#include <algorithm>
#include <cmath>
#include "history/favicon_cache.h"
#include "windows/page_corners.h"
#include "windows/sidebar_peek.h"
#include "workspace/controller.h"
#include "include/cef_parser.h"

namespace red {
namespace {
COLORREF Mix(COLORREF back, COLORREF front, double amount) {
  auto channel = [amount](BYTE b, BYTE f) { return static_cast<BYTE>(std::lround(b + (f - b) * amount)); };
  return RGB(channel(GetRValue(back), GetRValue(front)), channel(GetGValue(back), GetGValue(front)), channel(GetBValue(back), GetBValue(front)));
}
COLORREF Ref(Rgb color) { return RGB(color.r, color.g, color.b); }
}  // namespace

std::map<std::string, SplitPageLabel> SplitPageLabels(const std::shared_ptr<State>& state) {
  std::map<std::string, SplitPageLabel> labels;
  if (!state || !state->workspace) return labels;
  // The snapshot is held while its rows are read (child values belong to it).
  const auto snapshot = state->workspace->Snapshot();
  const auto rows = snapshot->GetList("nodes");
  for (size_t i = 0; rows && i < rows->GetSize(); ++i) {
    const auto row = rows->GetDictionary(i); if (row->GetString("kind") != "tab") continue;
    SplitPageLabel label{row->GetString("title").ToWString(), ""};
    if (!row->GetBool("customTitle") && !row->GetString("pageTitle").empty()) label.title = row->GetString("pageTitle").ToWString();
    CefURLParts parts;
    if (state->favicons && CefParseURL(row->GetString("favicon"), parts))
      label.icon = state->favicons->PngForPath(CefString(&parts.path).ToString()).value_or("");
    labels[row->GetString("id")] = std::move(label);
  }
  return labels;
}

void SplitLayoutPreview::Hide() {
  if (const auto state = state_.lock(); state && state->probe && overlay_.visible()) state->Record("split_preview_hidden");
  overlay_.Hide(); shown_.clear(); key_.clear(); painted_.clear();
}

void SplitLayoutPreview::Update(const std::shared_ptr<State>& state, const std::string& key, const CefRect& area,
                                const std::vector<SplitPreviewCard>& cards, const std::map<std::string, SplitPageLabel>& labels, float scale) {
  if (!state || !state->window) return;
  state_ = state;
  if (state->probe && !overlay_.visible()) state->Record("split_preview_shown");
  const auto now = GetTickCount64();
  if (key != key_) {
    key_ = key; started_ = now; from_.clear(); painted_.clear();
    for (const auto& card : cards) {
      const auto shown = shown_.find(card.id);
      from_[card.id] = shown != shown_.end() ? shown->second : card.from;
    }
    shown_.clear();
  }
  const double progress = std::min(1.0, (now - started_) / 170.0), eased = 1 - std::pow(1 - progress, 3);
  const bool peek_open = state->sidebar_peek && state->sidebar_peek->window_visible();
  if (progress >= 1 && painted_ == key_ && overlay_.visible() && peek_open == peek_shown_) return;
  peek_shown_ = peek_open;
  const HWND root = state->window->GetWindowHandle();
  RECT client{}; GetClientRect(root, &client);
  POINT origin{0, 0}; ClientToScreen(root, &origin);
  const RECT bounds{area.x, area.y, area.x + area.width, area.y + area.height};
  const COLORREF background = WorkspaceBackgroundAt(area.x - origin.x + area.width / 2., area.y - origin.y + area.height / 2.,
                                                    client.right, client.bottom, root);
  const COLORREF card = Ref(Themed({251, 249, 252}, state->theme, state->theme_dark));
  const COLORREF header = Ref(Themed({241, 236, 245}, state->theme, state->theme_dark));
  const COLORREF ink = Ref(Themed({38, 32, 44}, state->theme, state->theme_dark));
  const COLORREF line = Ref(Themed({61, 53, 69}, state->theme, state->theme_dark));
  const COLORREF accent = Ref(Themed({133, 94, 153}, state->theme, state->theme_dark));
  std::vector<OverlayCard> painted;
  for (const auto& item : cards) {
    const auto& a = from_[item.id]; const auto& b = item.to;
    auto lerp = [eased](int x, int y) { return static_cast<int>(std::lround(x + (y - x) * eased)); };
    const CefRect rect(lerp(a.x, b.x), lerp(a.y, b.y), lerp(a.width, b.width), lerp(a.height, b.height));
    shown_[item.id] = rect;
    OverlayCard paint;
    paint.rect = {rect.x, rect.y, rect.x + rect.width, rect.y + rect.height};
    paint.pane = true; paint.accent = item.accent; paint.radius = 8; paint.text = ink;
    paint.fill = item.accent ? Mix(card, accent, .16) : card;
    paint.header = item.accent ? Mix(header, accent, .28) : header;
    paint.border = item.accent ? accent : line; paint.border_alpha = item.accent ? .95f : .16f; paint.border_width = item.accent ? 2.f : 1.f;
    if (const auto label = labels.find(item.id); label != labels.end()) { paint.title = label->second.title; paint.icon = label->second.icon; }
    painted.push_back(std::move(paint));
  }
  // The moving card is drawn last, above any card it slides over.
  std::stable_partition(painted.begin(), painted.end(), [](const auto& item) { return !item.accent; });
  // The floating sidebar stays visible above the preview (it can be the target).
  RECT peek{}; const bool hole = state->sidebar_peek && state->sidebar_peek->window_visible() && GetWindowRect(state->sidebar_peek->host(), &peek);
  if (overlay_.Show(root, bounds, background, 1.f, painted, scale, hole ? &peek : nullptr)) painted_ = progress >= 1 ? key_ : "";
}
}  // namespace red
