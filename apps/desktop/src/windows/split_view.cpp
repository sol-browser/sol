#include "windows/embedded_content.h"
#include "windows/devtools_dock.h"
#include "windows/sidebar_peek.h"
#include "include/views/cef_display.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace red {
namespace {
SplitRect ToSplitRect(const CefRect& rect) { return {rect.x, rect.y, rect.width, rect.height}; }
}  // namespace
int EmbeddedContent::SplitSlot(int id) const {
  const auto found = std::find(split_pages_.begin(), split_pages_.end(), id);
  return id == -1 || found == split_pages_.end() ? -1 : static_cast<int>(found - split_pages_.begin());
}
SplitMetrics EmbeddedContent::SplitPaneMetrics() const {
  const auto state = state_.lock();
  const double scale = state && state->window ? state->window->GetDisplay()->GetDeviceScaleFactor() : 1.;
  return {static_cast<int>(std::lround(8 * scale)), static_cast<int>(std::lround(220 * scale)), static_cast<int>(std::lround(160 * scale))};
}
SplitRect EmbeddedContent::SplitPaneArea() const {
  return ToSplitRect(browser_fullscreen_ ? FullClientBounds() : WorkspacePhysicalBounds());
}
SplitGeometry EmbeddedContent::SplitPaneGeometry(const SplitNode& layout, int panes, bool linked) const {
  return LayoutSplitPanes(layout, panes, SplitPaneArea(), SplitPaneMetrics(), linked);
}
SplitGeometry EmbeddedContent::SplitPaneGeometry() const {
  return SplitPaneGeometry(split_layout_, static_cast<int>(split_pages_.size()), split_linked_);
}
COLORREF EmbeddedContent::SplitBorder(int slot) const {
  const auto state = state_.lock(); if (!state || !state->window) return RGB(128,128,128);
  const auto cell = SplitCell(slot); RECT client{}; GetClientRect(state->window->GetWindowHandle(),&client);
  const auto background = WorkspaceBackgroundAt(cell.x+cell.width/2.,cell.y+cell.height/2.,client.right,client.bottom,state->window->GetWindowHandle());
  const bool focused = state->content && slot >= 0 && slot < static_cast<int>(split_pages_.size()) &&
                       state->content->GetIdentifier() == split_pages_[slot];
  const auto ink = Themed(focused ? Rgb{133,94,153} : Rgb{61,53,69},state->theme,state->theme_dark);
  const double opacity = focused ? .65 : .12;
  auto mix = [opacity](BYTE back, BYTE front) { return static_cast<BYTE>(std::lround(back+(front-back)*opacity)); };
  return RGB(mix(GetRValue(background),ink.r),mix(GetGValue(background),ink.g),mix(GetBValue(background),ink.b));
}
CefRect EmbeddedContent::SplitCell(int slot) const {
  const auto area = browser_fullscreen_ ? FullClientBounds() : WorkspacePhysicalBounds();
  if (split_expanded_ != -1) return area;
  const auto geometry = SplitPaneGeometry();
  if (slot < 0 || slot >= static_cast<int>(geometry.panes.size())) return area;
  const auto& cell = geometry.panes[slot];
  return {cell.x, cell.y, cell.width, cell.height};
}
CefRect EmbeddedContent::LayoutArea(int id) const {
  if (fullscreen_id_ == id && id != -1) return FullClientBounds();
  if (IsLinkPeek(id)) return LinkPeekViewport();
  if (!IsSplitPage(id)) return browser_fullscreen_ ? FullClientBounds() : WorkspacePhysicalBounds();
  auto area = SplitCell(SplitSlot(id));
  const auto state = state_.lock();
  const double scale = state && state->window ? state->window->GetDisplay()->GetDeviceScaleFactor() : 1.;
  const int border = std::max(1,static_cast<int>(std::lround(scale))), header = static_cast<int>(std::lround(30*scale));
  area.x += border; area.y += header; area.width = std::max(1,area.width-2*border); area.height = std::max(1,area.height-header-border);
  return area;
}
void EmbeddedContent::SetSplitPages(std::vector<int> pages, const SplitNode& layout, bool linked, int expanded, const std::set<int>& crashed) {
  const bool enabled = static_cast<int>(pages.size()) >= 2 && ValidSplitLayout(layout, static_cast<int>(pages.size())) &&
                       std::any_of(pages.begin(), pages.end(), [](int id) { return id != -1; });
  if (!enabled) pages.clear();
  const bool changed = enabled != split_enabled_ || pages != split_pages_;
  if (changed) {
    // Returning to a split cancels the pending single-page reveal.
    FinishPresentation(presentation_generation_, false, !enabled);
    if (enabled) split_leaving_.clear();
  }
  // Leaving a split: its visible panes keep their place and size until the page
  // shown next has painted over them (Activate); they are not stretched first.
  if (changed && !enabled && split_enabled_)
    for (const int pane : split_pages_)
      if (const auto found = browsers_.find(pane); found != browsers_.end() && IsWindowVisible(found->second->GetHost()->GetWindowHandle()))
        split_leaving_.insert(pane);
  if (!enabled) {
    for (const int id : split_clipped_) if (const auto found = browsers_.find(id); found != browsers_.end())
      SetWindowRgn(found->second->GetHost()->GetWindowHandle(),nullptr,TRUE);
    split_clipped_.clear(); split_live_ = false;
  }
  split_enabled_ = enabled; split_pages_ = std::move(pages);
  split_layout_ = enabled ? layout : SplitNode{}; split_linked_ = enabled && linked && LinkableSplitGrid(layout);
  split_expanded_ = enabled ? expanded : -1;
  split_crashed_ = crashed;
  if (enabled) LayoutSplit();
  else if (changed) { Resize(bounds_); RaiseActive(); }
}
void EmbeddedContent::LayoutSplit() {
  if (HasLinkPeek()) { LayoutLinkPeek(); return; }
  const auto state = state_.lock(); if (!state || !state->window || !split_enabled_) return;
  const int active = state->content ? state->content->GetIdentifier() : -1;
  if (state->devtools_dock) state->devtools_dock->Layout(LayoutArea(active),active,IsFullscreen());
  const HWND root = state->window->GetWindowHandle();
  HWND above = InlineSidebar();
  if (state->devtools_dock) above = state->devtools_dock->StackAfter(above);
  for (const auto& [id,browser] : browsers_) {
    const bool shown = IsSplitPage(id) && !split_crashed_.contains(id) &&
        (fullscreen_id_ == -1 || fullscreen_id_ == id) && (split_expanded_ == -1 || split_expanded_ == id);
    const auto handle = browser->GetHost()->GetWindowHandle();
    if (!shown) { if (IsWindowVisible(handle)) ShowWindow(handle,SW_HIDE); continue; }
    const auto target = PagePhysicalBounds(id);
    RECT rect{}; POINT origin{}; GetWindowRect(handle,&rect); origin = {rect.left,rect.top}; ScreenToClient(root,&origin);
    const int width = rect.right-rect.left, height = rect.bottom-rect.top;
    if (split_live_ && IsWindowVisible(handle) && width > 0 && height > 0 && (width != target.width || height != target.height)) {
      // Live divider drag: move with the card, clip to it, keep the renderer's size.
      if (origin.x != target.x || origin.y != target.y)
        SetWindowPos(handle,nullptr,target.x,target.y,0,0,SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOSIZE);
      SetWindowRgn(handle,CreateRectRgn(0,0,std::min(width,target.width),std::min(height,target.height)),TRUE);
      split_clipped_.insert(id);
      if (GetWindow(handle,GW_HWNDPREV) != above)
        SetWindowPos(handle,above ? above : HWND_TOP,0,0,0,0,SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOSIZE);
      above = handle; continue;
    }
    if (split_clipped_.erase(id)) SetWindowRgn(handle,nullptr,TRUE);
    if (!IsWindowVisible(handle) || origin.x != target.x || origin.y != target.y || rect.right-rect.left != target.width || rect.bottom-rect.top != target.height)
      SetWindowPos(handle,nullptr,target.x,target.y,target.width,target.height,SWP_NOACTIVATE | SWP_NOZORDER | SWP_SHOWWINDOW);
    if (GetWindow(handle,GW_HWNDPREV) != above)
      SetWindowPos(handle,above ? above : HWND_TOP,0,0,0,0,SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOSIZE);
    above = handle; Shape(handle);
  }
  UpdateSplitCorners();
}
void EmbeddedContent::UpdateSplitCorners() {
  UpdateLinkStatus();
  const auto state = state_.lock();
  if (!state || !state->window || !split_enabled_ || fullscreen_id_ != -1) { corners_.Hide(); return; }
  std::vector<RECT> frames;
  std::vector<COLORREF> borders;
  for (int slot = 0; slot < static_cast<int>(split_pages_.size()); ++slot) {
    const auto browser = browsers_.find(split_pages_[slot]);
    if (browser == browsers_.end() || !IsWindowVisible(browser->second->GetHost()->GetWindowHandle())) continue;
    const auto cell = SplitCell(slot);
    frames.push_back({cell.x,cell.y,cell.x+cell.width,cell.y+cell.height});
    borders.push_back(SplitBorder(slot));
  }
  const int radius = static_cast<int>(std::lround(8*state->window->GetDisplay()->GetDeviceScaleFactor()));
  const float width = state->window->GetDisplay()->GetDeviceScaleFactor();
  if (corners_composited_ && !corners_.ShowFrames(state->window->GetWindowHandle(),frames,radius,borders,width)) {
    corners_.Hide(); corners_composited_ = false;
    for (const auto& [id,browser] : browsers_) Shape(browser->GetHost()->GetWindowHandle());
  }
}
}  // namespace red
