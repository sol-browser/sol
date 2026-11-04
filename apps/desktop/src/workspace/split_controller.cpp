#include "workspace/controller.h"
#include "windows/pointer.h"
#include "windows/embedded_content.h"
#include "windows/focus.h"
#include "windows/devtools_dock.h"
#include "windows/command_bar.h"
#include "windows/clipboard.h"
#include "windows/panel_popup.h"
#include "workspace/tab_order.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include "include/views/cef_display.h"
#include "include/base/cef_callback.h"
#include "include/wrapper/cef_closure_task.h"

namespace red {
std::optional<WorkspaceController::SplitDrop> WorkspaceController::SplitDropAt(const std::string& id, POINT screen) const {
  const auto state = state_.lock();
  if (!state || !state->window || !state->embedded || !ready_ || shutting_down_ || !closing_.empty() || closing_commits_ ||
      SelectedSplit() || tree_.active_id.empty() || tree_.active_id == id || state->embedded->IsPageFullscreen()) return {};
  const auto root = state->window->GetWindowHandle();
  if (IsIconic(root) || GetForegroundWindow() != root) return {};
  const auto current = std::find_if(tree_.nodes.begin(),tree_.nodes.end(),[&](const auto& n) { return n.id == tree_.active_id; });
  const auto dragged = std::find_if(tree_.nodes.begin(),tree_.nodes.end(),[&](const auto& n) { return n.id == id; });
  if (current == tree_.nodes.end() || dragged == tree_.nodes.end() || current->folder || dragged->folder || current->archived || dragged->archived ||
      (current->favorite && dragged->favorite)) return {};
  const auto visible = OrderedTabs(tree_.nodes,tree_.active_space);
  if (std::find(visible.begin(),visible.end(),id) == visible.end()) return {};
  for (const auto& split : tree_.splits) if (split.anchor == id || split.other == id || split.anchor == current->id || split.other == current->id) return {};
  const auto area = state->embedded->SplitDropArea();
  POINT point = screen; if (!ScreenToClient(root,&point) || point.x < area.x || point.y < area.y || point.x >= area.x+area.width || point.y >= area.y+area.height) return {};
  const double x = (point.x-area.x) / static_cast<double>(std::max(1,area.width));
  const double y = (point.y-area.y) / static_cast<double>(std::max(1,area.height));
  const double edge = std::min({x,1-x,y,1-y}); if (edge > .28) return {};
  const bool vertical = std::min(y,1-y) < std::min(x,1-x), leading = vertical ? y < .5 : x < .5;
  const bool anchor_current = !current->favorite;
  TreeCommand create{TreeAction::CreateSplit}; create.id = anchor_current ? current->id : id;
  create.parent = anchor_current ? id : current->id; create.value = vertical; create.reversed = leading == anchor_current;
  auto preview = area; const int gap = std::lround(8 * state->window->GetDisplay()->GetDeviceScaleFactor());
  if (vertical) { preview.height = std::max(1,(area.height-gap)/2); if (!leading) preview.y = area.y+area.height-preview.height; }
  else { preview.width = std::max(1,(area.width-gap)/2); if (!leading) preview.x = area.x+area.width-preview.width; }
  POINT origin{preview.x,preview.y}; if (!ClientToScreen(root,&origin)) return {};
  preview.x = origin.x; preview.y = origin.y;
  return SplitDrop{std::move(create),preview};
}
CefRefPtr<CefDictionaryValue> WorkspaceController::DescribeSplitView() const {
  const auto* split = SelectedSplit(); const auto state = state_.lock();
  if (!split || !state || !state->window || !state->embedded || state->embedded->IsPageFullscreen()) return nullptr;
  auto result = CefDictionaryValue::Create();
  result->SetString("anchor",split->anchor); result->SetString("other",split->other); result->SetString("focused",tree_.active_id);
  result->SetBool("vertical",split->vertical); result->SetBool("reversed",split->reversed);
  result->SetBool("resizing",split_resizing_);
  result->SetInt("ratio",split_preview_ && split_preview_->first == split->anchor ? split_preview_->second : split->ratio);
  result->SetString("expanded",expanded_split_pane_);
  auto panes = CefListValue::Create();
  const double scale = state->window->GetDisplay()->GetDeviceScaleFactor();
  for (const bool second : {false,true}) {
    auto row = CefDictionaryValue::Create(); const auto area = state->embedded->SplitCell(second);
    const auto& id = second ? split->other : split->anchor;
    row->SetString("id",id);
    const auto browser = LiveBrowser(id);
    row->SetBool("canGoBack",browser && browser->CanGoBack());
    row->SetBool("canGoForward",browser && browser->CanGoForward());
    row->SetBool("loading",browser && browser->IsLoading());
    const auto border = state->embedded->SplitBorder(second);
    char hex[8]{}; std::snprintf(hex,sizeof(hex),"#%02x%02x%02x",static_cast<unsigned>(GetRValue(border)),static_cast<unsigned>(GetGValue(border)),static_cast<unsigned>(GetBValue(border)));
    row->SetString("borderColor",hex);
    auto bounds = CefDictionaryValue::Create();
    bounds->SetDouble("x",area.x/scale); bounds->SetDouble("y",area.y/scale);
    bounds->SetDouble("width",area.width/scale); bounds->SetDouble("height",area.height/scale);
    row->SetDictionary("bounds",bounds); panes->SetDictionary(panes->GetSize(),row);
  }
  result->SetList("panes",panes); return result;
}
const SplitGroup* WorkspaceController::SelectedSplit() const {
  const auto visible = OrderedTabs(tree_.nodes,tree_.active_space);
  for (const auto& split : tree_.splits) if ((split.anchor == tree_.active_id || split.other == tree_.active_id) &&
      std::find(visible.begin(),visible.end(),split.anchor) != visible.end() &&
      !closing_.contains(split.anchor) && !closing_.contains(split.other) &&
      !moving_unloads_.contains(split.anchor) && !moving_unloads_.contains(split.other) &&
      !archiving_.contains(split.anchor) && !archiving_.contains(split.other)) return &split;
  return nullptr;
}
void WorkspaceController::OnPageFocused(CefRefPtr<CefBrowser> browser) {
  if (HasLinkPeek()) return;
  const auto state = state_.lock(); const auto* split = SelectedSplit();
  const auto binding = browser_nodes_.find(browser->GetIdentifier());
  if (!state || !state->embedded || !split || binding == browser_nodes_.end() || tree_.active_id == binding->second ||
      (binding->second != split->anchor && binding->second != split->other) || !IsWindowVisible(browser->GetHost()->GetWindowHandle())) return;
  tree_.active_id = binding->second; state->content = browser;
  state->ui_editing = false; state->keyboard_target = browser->GetIdentifier();
#if CEF_API_ADDED(CEF_EXPERIMENTAL)
  browser->GetHost()->NotifyTabActivated();
#endif
  EnsureActive(); Publish();
  TreeCommand command{TreeAction::Activate}; command.id = binding->second; Dispatch(command,{},true);
}
bool WorkspaceController::HandleSplit(CefRefPtr<CefDictionaryValue> request, Reply done) {
  const auto method = request->GetString("method").ToString();
  if (method != "split.focus" && method != "split.expand" && method != "split.preview" && method != "split.beginResize" && method != "split.navigate" && method != "split.site") return false;
  const auto state = state_.lock(); const auto* split = SelectedSplit();
  if (!ready_ || shutting_down_ || !state || !split || request->GetType("id") != VTYPE_STRING || !closing_.empty() || closing_commits_) {
    done(409,"Split view is unavailable",nullptr); return true;
  }
  const auto id = request->GetString("id").ToString();
  if (id != split->anchor && id != split->other) { done(400,"Invalid split pane",nullptr); return true; }
  if (method == "split.site") {
    if (!state->panel_popup || request->GetSize() != 6 || request->GetType("x") != VTYPE_INT || request->GetType("y") != VTYPE_INT ||
        request->GetType("width") != VTYPE_INT || request->GetType("height") != VTYPE_INT) {
      done(400,"Invalid split site button",nullptr); return true;
    }
    const double scale = state->window->GetDisplay()->GetDeviceScaleFactor();
    const auto cell = state->embedded->SplitCell(id == split->other);
    const int x=request->GetInt("x"), y=request->GetInt("y"), w=request->GetInt("width"), h=request->GetInt("height");
    if (w < 1 || h < 1 || w > 32 || h > 32 || x < cell.x/scale-1 || y < cell.y/scale-1 ||
        static_cast<double>(x)+w > (cell.x+cell.width)/scale+1 || static_cast<double>(y)+h > cell.y/scale+31) {
      done(400,"Site button must be inside its pane header",nullptr); return true;
    }
    // Repeating this pane's button toggles closed; another pane opens its own
    // settings instead of toggling the previous site's panel.
    if (tree_.active_id != id && state->panel_popup->is_open()) state->panel_popup->Hide(false);
    tree_.active_id = id; EnsureActive(); Publish();
    const auto browser = LiveBrowser(id);
    if (!browser || state->content != browser) { done(409,"This split pane is still opening",nullptr); return true; }
    state->panel_popup->Toggle("site",CefRect(x,y,w,h));
    TreeCommand activate{TreeAction::Activate}; activate.id=id; Dispatch(activate,std::move(done),true); return true;
  }
  if (method == "split.navigate") {
    const auto navigation = request->GetType("action") == VTYPE_STRING ? request->GetString("action").ToString() : "";
    if (request->GetSize() != 3 || (navigation != "back" && navigation != "forward" && navigation != "reload" &&
        navigation != "stop" && navigation != "address" && navigation != "copy")) {
      done(400,"Invalid split navigation",nullptr); return true;
    }
    // Resolve and act on the selected member in one UI-thread operation. A
    // separate focus IPC followed by a global navigation IPC can race a click.
    tree_.active_id = id; EnsureActive(); Publish();
    const auto browser = LiveBrowser(id);
    if (navigation != "address" && navigation != "copy" && (!browser || state->content != browser)) {
      done(409,"This split pane is still opening",nullptr); return true;
    }
    if (navigation == "address") {
      if (state->command_bar) state->command_bar->Open(false);
    } else if (navigation == "copy") {
      const auto node = std::find_if(tree_.nodes.begin(),tree_.nodes.end(),[&](const auto& item) { return item.id == id; });
      if (node == tree_.nodes.end()) { done(409,"Split pane is unavailable",nullptr); return true; }
      const auto address = browser && browser->GetMainFrame() ? browser->GetMainFrame()->GetURL() : CefString(node->url);
      const int code = CopyLinkText(state->window->GetWindowHandle(),address.ToWString());
      if (code) { done(code,code == 409 ? "The clipboard is busy. Try again." : "Could not copy the page address",nullptr); return true; }
      if (state->embedded) state->embedded->Notify("copy",L"Copied current URL");
    } else if (navigation == "reload") Reload();
    else if (browser) {
      if (navigation == "back") NavigateHistory(false);
      else if (navigation == "forward") NavigateHistory(true);
      else browser->StopLoad();
    }
    if (navigation != "address" && browser) FocusBrowser(browser);
    TreeCommand activate{TreeAction::Activate}; activate.id = id; Dispatch(activate,std::move(done),true); return true;
  }
  if (method == "split.beginResize") {
    if (request->GetSize() != 2 || id != split->anchor || !expanded_split_pane_.empty() ||
        !PrimaryButtonDown() || GetForegroundWindow() != state->window->GetWindowHandle()) {
      done(400,"Invalid split resize gesture",nullptr); return true;
    }
    const auto a = state->embedded->SplitCell(false), b = state->embedded->SplitCell(true);
    RECT gap = split->vertical ? RECT{a.x,std::min(a.y+a.height,b.y+b.height),a.x+a.width,std::max(a.y,b.y)} :
      RECT{std::min(a.x+a.width,b.x+b.width),a.y,std::max(a.x,b.x),a.y+a.height};
    InflateRect(&gap,3,3); POINT cursor{};
    if (!GetCursorPos(&cursor) || !ScreenToClient(state->window->GetWindowHandle(),&cursor) || !PtInRect(&gap,cursor)) {
      done(400,"Resize must start at the split divider",nullptr); return true;
    }
    split_resize_anchor_ = split->anchor; split_resize_initial_ = split->ratio; split_resizing_ = true;
    FocusShell(state); ResizeSplitTick(++split_resize_generation_); done(0,"",Snapshot()); return true;
  } else if (method == "split.preview") {
    if (request->GetSize() != 3 || id != split->anchor || request->GetType("ratio") != VTYPE_INT || request->GetInt("ratio") < 1000 || request->GetInt("ratio") > 9000) {
      done(400,"Invalid split ratio",nullptr); return true;
    }
    split_preview_ = {split->anchor,request->GetInt("ratio")};
  } else {
    if (request->GetSize() != 2) { done(400,"Invalid split command",nullptr); return true; }
    if (method == "split.expand") expanded_split_pane_ = expanded_split_pane_ == id ? "" : id;
    tree_.active_id = id;
  }
  EnsureActive(); Publish();
  if (method != "split.preview" && !state->ui_editing) if (const auto browser = LiveBrowser(id)) FocusBrowser(browser);
  if (method != "split.preview") { TreeCommand activate{TreeAction::Activate}; activate.id = id; Dispatch(activate,std::move(done),true); return true; }
  done(0,"",Snapshot()); return true;
}
bool WorkspaceController::SplitResizeHovered(const std::string& anchor) const {
  const auto state = state_.lock(); const auto* split = SelectedSplit();
  if (!state || !state->window || !state->embedded || !split || split->anchor != anchor || !expanded_split_pane_.empty()) return false;
  const auto root = state->window->GetWindowHandle();
  POINT point{}; if (GetForegroundWindow() != root || !GetCursorPos(&point) || GetAncestor(WindowFromPoint(point),GA_ROOT) != root || !ScreenToClient(root,&point)) return false;
  const auto a=state->embedded->SplitCell(false), b=state->embedded->SplitCell(true);
  const RECT gap = split->vertical ? RECT{a.x,std::min(a.y+a.height,b.y+b.height),a.x+a.width,std::max(a.y,b.y)} :
    RECT{std::min(a.x+a.width,b.x+b.width),a.y,std::max(a.x,b.x),a.y+a.height};
  return PtInRect(&gap,point) != FALSE;
}
void WorkspaceController::ResizeSplitTick(unsigned generation) {
  const auto state = state_.lock(); const auto* split = SelectedSplit();
  if (!split_resizing_ || generation != split_resize_generation_) return;
  const bool cancelled = !state || !state->window || !state->embedded || !split || split->anchor != split_resize_anchor_ ||
    shutting_down_ || (GetAsyncKeyState(VK_ESCAPE) & 0x8000) || GetForegroundWindow() != state->window->GetWindowHandle();
  if (cancelled) {
    split_resizing_ = false; split_preview_.reset(); if (state && !shutting_down_) { EnsureActive(); Publish(); } return;
  }
  if (!PrimaryButtonDown()) {
    split_resizing_ = false;
    const int ratio = split_preview_ ? split_preview_->second : split_resize_initial_;
    TreeCommand command{TreeAction::LayoutSplit}; command.id = split->anchor; command.position = ratio; command.value = split->vertical;
    Dispatch(command,[weak = weak_from_this()](int code,const std::string& error,CefRefPtr<CefDictionaryValue>) {
      if (const auto self = weak.lock()) { self->split_preview_.reset(); if (code) self->error_ = error; self->EnsureActive(); self->Publish(); }
    },true);
    if (state->content) FocusBrowser(state->content); return;
  }
  POINT cursor{};
  if (GetCursorPos(&cursor) && ScreenToClient(state->window->GetWindowHandle(),&cursor)) {
    const auto a = state->embedded->SplitCell(false), b = state->embedded->SplitCell(true);
    const int start = split->vertical ? std::min(a.y,b.y) : std::min(a.x,b.x);
    const int size = split->vertical ? a.height+b.height : a.width+b.width;
    const int position = split->vertical ? cursor.y : cursor.x;
    const int ratio = std::clamp(static_cast<int>(10000. * (position-start) / std::max(1,size)),1000,9000);
    if (!split_preview_ || split_preview_->second != ratio) { split_preview_ = {split->anchor,ratio}; EnsureActive(); Publish(); }
  }
  CefPostDelayedTask(TID_UI,base::BindOnce([](std::weak_ptr<WorkspaceController> weak,unsigned generation) {
    if (const auto self = weak.lock()) self->ResizeSplitTick(generation);
  },weak_from_this(),generation),16);
}
}  // namespace red
