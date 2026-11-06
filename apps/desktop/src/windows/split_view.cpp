#include "windows/embedded_content.h"
#include "windows/devtools_dock.h"
#include "windows/sidebar_peek.h"
#include "include/views/cef_display.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace red {
COLORREF EmbeddedContent::SplitBorder(bool second) const {
  const auto state = state_.lock(); if (!state || !state->window) return RGB(128,128,128);
  const auto cell = SplitCell(second); RECT client{}; GetClientRect(state->window->GetWindowHandle(),&client);
  const auto background = WorkspaceBackgroundAt(cell.x+cell.width/2.,cell.y+cell.height/2.,client.right,client.bottom,state->window->GetWindowHandle());
  const bool focused = state->content && state->content->GetIdentifier() == (second ? split_second_ : split_first_);
  const auto ink = Themed(focused ? Rgb{133,94,153} : Rgb{61,53,69},state->theme,state->theme_dark);
  const double opacity = focused ? .65 : .12;
  auto mix = [opacity](BYTE back, BYTE front) { return static_cast<BYTE>(std::lround(back+(front-back)*opacity)); };
  return RGB(mix(GetRValue(background),ink.r),mix(GetGValue(background),ink.g),mix(GetBValue(background),ink.b));
}
CefRect EmbeddedContent::SplitCell(bool second) const {
  auto area = browser_fullscreen_ ? FullClientBounds() : WorkspacePhysicalBounds();
  if (split_expanded_ != -1) return area;
  const auto state = state_.lock();
  const double scale = state && state->window ? state->window->GetDisplay()->GetDeviceScaleFactor() : 1.;
  const int gap = static_cast<int>(std::lround(8 * scale));
  const int available = std::max(2,(split_vertical_ ? area.height : area.width) - gap);
  const int minimum = std::min(available / 2,static_cast<int>(std::lround((split_vertical_ ? 160 : 220) * scale)));
  const int leading = std::clamp(static_cast<int>(available * (split_ratio_ / 10000.)),minimum,available-minimum);
  const bool trailing = second != split_reversed_;
  if (split_vertical_) { area.y += trailing ? leading+gap : 0; area.height = trailing ? available-leading : leading; }
  else { area.x += trailing ? leading+gap : 0; area.width = trailing ? available-leading : leading; }
  return area;
}
CefRect EmbeddedContent::LayoutArea(int id) const {
  if (fullscreen_id_ == id && id != -1) return FullClientBounds();
  if (IsLinkPeek(id)) return LinkPeekViewport();
  if (!IsSplitPage(id)) return browser_fullscreen_ ? FullClientBounds() : WorkspacePhysicalBounds();
  auto area = SplitCell(id == split_second_);
  const auto state = state_.lock();
  const double scale = state && state->window ? state->window->GetDisplay()->GetDeviceScaleFactor() : 1.;
  const int border = std::max(1,static_cast<int>(std::lround(scale))), header = static_cast<int>(std::lround(30*scale));
  area.x += border; area.y += header; area.width = std::max(1,area.width-2*border); area.height = std::max(1,area.height-header-border);
  return area;
}
void EmbeddedContent::SetSplitPages(int first, int second, bool vertical, int ratio, bool reversed, int expanded, const std::set<int>& crashed) {
  const bool enabled = first != -1 || second != -1;
  const bool changed = enabled != split_enabled_ || first != split_first_ || second != split_second_;
  if (changed) FinishPresentation(presentation_generation_,false);
  // Leaving a split: its visible panes keep their place and size until the page
  // shown next has painted over them (Activate); they are not stretched first.
  split_leaving_.clear();
  if (changed && !enabled && split_enabled_)
    for (const int pane : {split_first_, split_second_})
      if (const auto found = browsers_.find(pane); found != browsers_.end() && IsWindowVisible(found->second->GetHost()->GetWindowHandle()))
        split_leaving_.insert(pane);
  split_enabled_ = enabled; split_first_ = first; split_second_ = second; split_vertical_ = vertical;
  split_ratio_ = std::clamp(ratio,1000,9000); split_reversed_ = reversed; split_expanded_ = expanded;
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
  for (const auto& [id,browser] : browsers_) {
    const bool shown = IsSplitPage(id) && !split_crashed_.contains(id) &&
        (fullscreen_id_ == -1 || fullscreen_id_ == id) && (split_expanded_ == -1 || split_expanded_ == id);
    const auto handle = browser->GetHost()->GetWindowHandle();
    if (!shown) { if (IsWindowVisible(handle)) ShowWindow(handle,SW_HIDE); continue; }
    const auto target = PagePhysicalBounds(id);
    RECT rect{}; POINT origin{}; GetWindowRect(handle,&rect); origin = {rect.left,rect.top}; ScreenToClient(root,&origin);
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
  for (const auto& [id,browser] : browsers_) {
    if (!IsSplitPage(id) || !IsWindowVisible(browser->GetHost()->GetWindowHandle())) continue;
    const auto cell = SplitCell(id == split_second_);
    frames.push_back({cell.x,cell.y,cell.x+cell.width,cell.y+cell.height});
    borders.push_back(SplitBorder(id == split_second_));
  }
  const int radius = static_cast<int>(std::lround(8*state->window->GetDisplay()->GetDeviceScaleFactor()));
  const float width = state->window->GetDisplay()->GetDeviceScaleFactor();
  if (corners_composited_ && !corners_.ShowFrames(state->window->GetWindowHandle(),frames,radius,borders,width)) {
    corners_.Hide(); corners_composited_ = false;
    for (const auto& [id,browser] : browsers_) Shape(browser->GetHost()->GetWindowHandle());
  }
}
}  // namespace red
