#include "workspace/controller.h"
#include "windows/pointer.h"
#include "windows/embedded_content.h"
#include "windows/focus.h"
#include "windows/devtools_dock.h"
#include "windows/command_bar.h"
#include "windows/clipboard.h"
#include "windows/panel_popup.h"
#include "windows/sidebar_peek.h"
#include "windows/menu_popup.h"
#include "windows/split_layout_preview.h"
#include "workspace/tab_order.h"
#include <algorithm>
#include <climits>
#include <map>
#include <cmath>
#include <cstdio>
#include "include/views/cef_display.h"
#include "include/base/cef_callback.h"
#include "include/wrapper/cef_closure_task.h"

// Split View (docs/split-view.md): 2-4 pages of one group shown together. The
// layout tree and its geometry are pure (workspace/split_layout.h); this file
// validates browser requests, drives the native divider gesture and commits.
namespace red {
namespace {
// Where an added pane grows from: a zero-width line at that edge of |rect|.
SplitRect Collapsed(const SplitRect& rect, SplitEdge edge) {
  switch (edge) {
    case SplitEdge::Left: return {rect.x, rect.y, 0, rect.height};
    case SplitEdge::Right: return {rect.x + rect.width, rect.y, 0, rect.height};
    case SplitEdge::Top: return {rect.x, rect.y, rect.width, 0};
    default: return {rect.x, rect.y + rect.height, rect.width, 0};
  }
}
CefRefPtr<CefDictionaryValue> Bounds(const SplitRect& rect, double scale) {
  auto bounds = CefDictionaryValue::Create();
  bounds->SetDouble("x",rect.x/scale); bounds->SetDouble("y",rect.y/scale);
  bounds->SetDouble("width",rect.width/scale); bounds->SetDouble("height",rect.height/scale);
  return bounds;
}
// How long the pointer rests on a dragged divider before the website windows resize.
constexpr unsigned long long kSplitSettleMs = 180;
bool ReadPath(CefRefPtr<CefDictionaryValue> request, std::vector<int>& path) {
  if (request->GetType("path") != VTYPE_LIST) return false;
  const auto list = request->GetList("path");
  if (list->GetSize() > static_cast<size_t>(kMaxSplitPanes)) return false;
  for (size_t i = 0; i < list->GetSize(); ++i) {
    if (list->GetType(i) != VTYPE_INT || list->GetInt(i) < 0 || list->GetInt(i) >= kMaxSplitPanes) return false;
    path.push_back(list->GetInt(i));
  }
  return true;
}
}  // namespace

// The picture shown while a pane is moved by its header.
struct SplitMoveVisual { SplitLayoutPreview preview; std::map<std::string, SplitPageLabel> labels; };

std::optional<TreeCommand> WorkspaceController::SplitAddCommand(const SplitGroup& group, const std::string& page, int target, SplitEdge edge) const {
  const int panes = static_cast<int>(group.pages.size());
  if (panes >= kMaxSplitPanes || group.Contains(page)) return std::nullopt;
  const auto layout = InsertSplitPane(group.layout, target, edge, panes);
  if (!layout) return std::nullopt;
  TreeCommand command{TreeAction::AddSplitPane};
  command.id = group.anchor; command.parent = page; command.layout = FormatSplitLayout(*layout);
  return command;
}

void WorkspaceController::CommitSplitLayout(const SplitGroup& group, const SplitNode& layout, bool linked, Reply done) {
  TreeCommand command{TreeAction::LayoutSplit};
  command.id = group.anchor; command.layout = FormatSplitLayout(layout); command.value = linked && LinkableSplitGrid(layout);
  // Show it at once; the shared tree confirms (or the preview is dropped on error).
  split_preview_ = {group.anchor,layout}; EnsureActive(); Publish();
  Dispatch(command,[weak = weak_from_this(),done = std::move(done)](int code,const std::string& error,CefRefPtr<CefDictionaryValue> snapshot) {
    if (const auto self = weak.lock()) { self->split_preview_.reset(); if (code) self->error_ = error; self->EnsureActive(); self->Publish(); }
    if (done) done(code,error,snapshot);
  },true);
}

std::optional<WorkspaceController::SplitDrop> WorkspaceController::SplitDropAt(const std::string& id, POINT screen) {
  const auto state = state_.lock();
  if (!state || !state->window || !state->embedded || !ready_ || shutting_down_ || !closing_.empty() || closing_commits_ ||
      !tab_drag_ || tab_drag_->id != id || tab_drag_->space != tree_.active_space || state->embedded->IsPageFullscreen()) return {};
  auto& drag = *tab_drag_;
  if (drag.previous.empty() || drag.previous == id || (tree_.active_id != id && tree_.active_id != drag.previous)) return {};
  const auto root = state->window->GetWindowHandle();
  const HWND foreground = GetForegroundWindow();
  const HWND peek = state->sidebar_peek && state->sidebar_peek->window_visible() ? state->sidebar_peek->host() : nullptr;
  // A tab carried out of the floating sidebar keeps that window in the foreground
  // after the sidebar slides away (it is parked, not destroyed).
  const HWND peek_host = state->sidebar_peek ? state->sidebar_peek->host() : nullptr;
  if (IsIconic(root) || !foreground || (foreground != root && foreground != peek_host)) return {};
  RECT sidebar{};
  if (peek && GetWindowRect(peek, &sidebar) && PtInRect(&sidebar, screen)) return {};
  const auto area = state->embedded->SplitDropArea();
  POINT point = screen; if (!ScreenToClient(root,&point) || point.x < area.x || point.y < area.y || point.x >= area.x+area.width || point.y >= area.y+area.height) return {};
  const auto current = std::find_if(tree_.nodes.begin(),tree_.nodes.end(),[&](const auto& n) { return n.id == drag.previous; });
  const auto dragged = std::find_if(tree_.nodes.begin(),tree_.nodes.end(),[&](const auto& n) { return n.id == id; });
  if (current == tree_.nodes.end() || dragged == tree_.nodes.end() || current->folder || dragged->folder || current->archived || dragged->archived ||
      archiving_.contains(id) || archiving_.contains(current->id) || moving_unloads_.contains(id) || moving_unloads_.contains(current->id)) return {};
  const auto visible = OrderedTabsForSpace(tree_,tree_.active_space);
  if (std::find(visible.begin(),visible.end(),id) == visible.end()) return {};
  const SplitGroup* group = nullptr;
  for (const auto& split : tree_.splits) {
    if (split.Contains(id)) return {};
    if (split.Contains(current->id)) group = &split;
  }
  if (group ? static_cast<int>(group->pages.size()) >= kMaxSplitPanes ||
              std::find(visible.begin(),visible.end(),group->anchor) == visible.end()
            : (current->favorite && dragged->favorite) || std::find(visible.begin(),visible.end(),current->id) == visible.end()) return {};
  // A press selects the dragged tab. Restore its partner on entering the page,
  // even at the center, and use that same partner if release beats activation.
  if (!drag.restored && tree_.active_id == id) {
    drag.restored = true;
    TreeCommand activate{TreeAction::Activate}; activate.id = current->id;
    Dispatch(activate, {}, true);
  }
  const double x = (point.x-area.x) / static_cast<double>(std::max(1,area.width));
  const double y = (point.y-area.y) / static_cast<double>(std::max(1,area.height));
  POINT client_origin{0,0}; if (!ClientToScreen(root,&client_origin)) return {};
  auto to_screen = [client_origin](SplitRect rect) -> std::optional<CefRect> {
    return CefRect(client_origin.x+rect.x,client_origin.y+rect.y,rect.width,rect.height);
  };
  const auto screen_area = CefRect(client_origin.x+area.x,client_origin.y+area.y,area.width,area.height);
  // Not on a drop target: the layout as it is now, so the preview does not
  // vanish and reappear (the pages would flash between).
  auto neutral = [&]() -> std::optional<SplitDrop> {
    SplitDrop drop{TreeCommand{TreeAction::Activate},CefRect(),screen_area,{},true};
    if (group) {
      const int panes = static_cast<int>(group->pages.size());
      const auto now = state->embedded->SplitPaneGeometry(group->layout,panes,group->linked);
      for (int slot = 0; slot < panes; ++slot) drop.cards.push_back({group->pages[slot],*to_screen(now.panes[slot]),*to_screen(now.panes[slot]),false});
    } else drop.cards.push_back({current->id,screen_area,screen_area,false});
    return drop;
  };
  if (group) {
    // Joining a split: a strip along the whole area's edge adds a full-length
    // pane; otherwise the nearest edge of the pane under the pointer splits it.
    const int panes = static_cast<int>(group->pages.size());
    int target = -1; double px = x, py = y;
    if (std::min({x,1-x,y,1-y}) > .06) {
      const auto geometry = state->embedded->SplitPaneGeometry(group->layout,panes,group->linked);
      for (int slot = 0; slot < panes && target == -1; ++slot) {
        const auto& cell = geometry.panes[slot];
        if (!cell.Contains(point.x,point.y)) continue;
        target = slot;
        px = (point.x-cell.x) / static_cast<double>(std::max(1,cell.width)); py = (point.y-cell.y) / static_cast<double>(std::max(1,cell.height));
      }
      if (target == -1 || std::min({px,1-px,py,1-py}) > .28) return neutral();
    }
    const bool vertical = std::min(py,1-py) < std::min(px,1-px);
    const auto edge = vertical ? (py < .5 ? SplitEdge::Top : SplitEdge::Bottom) : (px < .5 ? SplitEdge::Left : SplitEdge::Right);
    auto command = SplitAddCommand(*group,id,target,edge);
    if (!command) return {};
    const auto layout = ParseSplitLayout(command->layout,panes+1);
    if (!layout) return {};
    const auto now = state->embedded->SplitPaneGeometry(group->layout,panes,group->linked);
    const auto after = state->embedded->SplitPaneGeometry(*layout,panes+1,false);
    SplitDrop drop{std::move(*command),*to_screen(after.panes[panes]),screen_area,{}};
    for (int slot = 0; slot < panes; ++slot)
      drop.cards.push_back({group->pages[slot],*to_screen(now.panes[slot]),*to_screen(after.panes[slot]),false});
    const SplitRect source = target == -1 ? SplitRect{area.x,area.y,area.width,area.height} : now.panes[target];
    drop.cards.push_back({id,*to_screen(Collapsed(source,edge)),*to_screen(after.panes[panes]),true});
    return drop;
  }
  const double edge = std::min({x,1-x,y,1-y}); if (edge > .28) return neutral();
  const bool vertical = std::min(y,1-y) < std::min(x,1-x), leading = vertical ? y < .5 : x < .5;
  const bool anchor_current = !current->favorite;
  TreeCommand create{TreeAction::CreateSplit}; create.id = anchor_current ? current->id : id;
  create.parent = anchor_current ? id : current->id; create.value = vertical; create.reversed = leading == anchor_current;
  // The dragged page's half as the pair will lay it out.
  const auto pair = SplitGroup::Pair(create.id,create.parent,vertical,create.reversed);
  const auto after = state->embedded->SplitPaneGeometry(pair.layout,2,false);
  const SplitRect whole{area.x,area.y,area.width,area.height};
  const auto edge_side = vertical ? (leading ? SplitEdge::Top : SplitEdge::Bottom) : (leading ? SplitEdge::Left : SplitEdge::Right);
  SplitDrop drop{std::move(create),*to_screen(after.panes[pair.Slot(id)]),screen_area,{}};
  drop.cards.push_back({current->id,*to_screen(whole),*to_screen(after.panes[pair.Slot(current->id)]),false});
  drop.cards.push_back({id,*to_screen(Collapsed(whole,edge_side)),*to_screen(after.panes[pair.Slot(id)]),true});
  return drop;
}

CefRefPtr<CefDictionaryValue> WorkspaceController::DescribeSplitView() const {
  const auto* split = SelectedSplit(); const auto state = state_.lock();
  if (!split || !state || !state->window || !state->embedded || state->embedded->IsPageFullscreen()) return nullptr;
  const int count = static_cast<int>(split->pages.size());
  const bool previewing = split_preview_ && split_preview_->first == split->anchor && ValidSplitLayout(split_preview_->second,count);
  const auto& layout = previewing ? split_preview_->second : split->layout;
  auto result = CefDictionaryValue::Create();
  result->SetString("anchor",split->anchor); result->SetString("focused",tree_.active_id);
  result->SetString("expanded",expanded_split_pane_); result->SetBool("resizing",split_resizing_);
  result->SetBool("linked",split->linked); result->SetString("layout",FormatSplitLayout(layout));
  result->SetBool("canAdd",count < kMaxSplitPanes);
  // A pane carried over the sidebar by its header: both sidebars show where it lands.
  if (split_move_ && split_move_->moving && split_move_->unsplit && split->Contains(split_move_->page)) result->SetString("leaving",split_move_->page);
  auto presets = CefListValue::Create();
  for (const auto& preset : SplitMenuPresets(count)) presets->SetString(presets->GetSize(),preset);
  result->SetList("presets",presets);
  const double scale = state->window->GetDisplay()->GetDeviceScaleFactor();
  auto panes = CefListValue::Create();
  for (const int slot : SplitReadingOrder(layout)) {
    if (slot < 0 || slot >= count) continue;
    auto row = CefDictionaryValue::Create(); const auto& id = split->pages[slot];
    const auto area = state->embedded->SplitCell(slot);
    row->SetString("id",id);
    const auto browser = LiveBrowser(id);
    row->SetBool("canGoBack",browser && browser->CanGoBack());
    row->SetBool("canGoForward",browser && browser->CanGoForward());
    row->SetBool("loading",browser && browser->IsLoading());
    const auto border = state->embedded->SplitBorder(slot);
    char hex[8]{}; std::snprintf(hex,sizeof(hex),"#%02x%02x%02x",static_cast<unsigned>(GetRValue(border)),static_cast<unsigned>(GetGValue(border)),static_cast<unsigned>(GetBValue(border)));
    row->SetString("borderColor",hex);
    row->SetDictionary("bounds",Bounds({area.x,area.y,area.width,area.height},scale)); panes->SetDictionary(panes->GetSize(),row);
  }
  result->SetList("panes",panes);
  auto dividers = CefListValue::Create();
  if (expanded_split_pane_.empty()) for (const auto& divider : state->embedded->SplitPaneGeometry().dividers) {
    auto row = CefDictionaryValue::Create();
    auto path = CefListValue::Create();
    for (const int index : divider.path) path->SetInt(path->GetSize(),index);
    row->SetList("path",path); row->SetInt("index",divider.index); row->SetBool("cross",divider.cross);
    row->SetString("axis",divider.axis == 'v' ? "v" : "h");
    // The leading pane's share of the two it separates, for assistive technology.
    int share = 50;
    if (const auto* container = SplitNodeAt(layout,divider.path); container && divider.index + 1 < static_cast<int>(container->children.size())) {
      const int a = container->children[divider.index].weight, b = container->children[divider.index+1].weight;
      share = static_cast<int>(std::lround(100. * a / std::max(1,a+b)));
    }
    row->SetInt("share",share);
    row->SetDictionary("bounds",Bounds(divider.rect,scale)); dividers->SetDictionary(dividers->GetSize(),row);
  }
  result->SetList("dividers",dividers);
  return result;
}

const SplitGroup* WorkspaceController::SelectedSplit() const {
  const auto visible = OrderedTabsForSpace(tree_,tree_.active_space);
  for (const auto& split : tree_.splits) if (split.Contains(tree_.active_id) &&
      std::find(visible.begin(),visible.end(),split.anchor) != visible.end() &&
      std::none_of(split.pages.begin(),split.pages.end(),[&](const auto& page) {
        return closing_.contains(page) || moving_unloads_.contains(page) || archiving_.contains(page); })) return &split;
  return nullptr;
}

void WorkspaceController::OnPageFocused(CefRefPtr<CefBrowser> browser) {
  if (HasLinkPeek()) return;
  const auto state = state_.lock(); const auto* split = SelectedSplit();
  const auto binding = browser_nodes_.find(browser->GetIdentifier());
  if (!state || !state->embedded || !split || binding == browser_nodes_.end() || tree_.active_id == binding->second ||
      !split->Contains(binding->second) || !IsWindowVisible(browser->GetHost()->GetWindowHandle())) return;
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
  static const std::set<std::string> methods{"split.focus","split.expand","split.beginResize","split.navigate","split.site",
                                             "split.add","split.preset","split.nudge","split.menu","split.beginMove"};
  if (!methods.contains(method)) return false;
  const auto state = state_.lock(); const auto* split = SelectedSplit();
  if (!ready_ || shutting_down_ || !state || !split || request->GetType("id") != VTYPE_STRING || !closing_.empty() || closing_commits_) {
    done(409,"Split view is unavailable",nullptr); return true;
  }
  const auto id = request->GetString("id").ToString();
  // Pane commands name a member; group commands name the group (its anchor).
  const bool group_command = method == "split.beginResize" || method == "split.add" || method == "split.preset" || method == "split.nudge";
  if (group_command ? id != split->anchor : !split->Contains(id)) { done(400,"Invalid split pane",nullptr); return true; }
  const int count = static_cast<int>(split->pages.size());
  if (method == "split.add") {
    // {id, target, edge, page}: |page| joins beside |target| ("" for the whole area).
    SplitEdge edge = SplitEdge::Right; std::string edge_name;
    if (request->GetSize() != 5 || request->GetType("target") != VTYPE_STRING || request->GetType("edge") != VTYPE_STRING ||
        request->GetType("page") != VTYPE_STRING || request->GetString("page").length() != 36) {
      done(400,"Invalid split addition",nullptr); return true;
    }
    const auto target_id = request->GetString("target").ToString();
    const int target = target_id.empty() ? -1 : split->Slot(target_id); edge_name = request->GetString("edge").ToString();
    if ((!target_id.empty() && target < 0) || (edge_name != "auto" && !ParseSplitEdge(edge_name,edge))) { done(400,"Invalid split addition",nullptr); return true; }
    if (edge_name == "auto") {
      const auto geometry = state->embedded->SplitPaneGeometry(split->layout,count,split->linked);
      const bool wide = target == -1 ? split->layout.axis == 'h' : geometry.panes[target].width >= geometry.panes[target].height;
      edge = wide ? SplitEdge::Right : SplitEdge::Bottom;
    }
    const auto page = request->GetString("page").ToString();
    auto command = SplitAddCommand(*split,page,target,edge);
    if (!command) { done(409,count >= kMaxSplitPanes ? "A split view holds at most four tabs" : "This tab cannot join the split",nullptr); return true; }
    Dispatch(*command,[weak = weak_from_this(),page,done = std::move(done)](int code,const std::string& error,CefRefPtr<CefDictionaryValue> snapshot) {
      if (const auto self = weak.lock(); self && !code) if (const auto browser = self->LiveBrowser(page)) FocusBrowser(browser);
      done(code,error,snapshot);
    },true);
    return true;
  }
  if (method == "split.preset") {
    if (request->GetSize() != 3 || request->GetType("preset") != VTYPE_STRING) { done(400,"Invalid split preset",nullptr); return true; }
    const auto name = request->GetString("preset").ToString();
    auto layout = SplitPreset(name,count);
    if (!layout) { done(400,"This layout needs a different number of tabs",nullptr); return true; }
    // The pages keep their reading order in the new arrangement.
    *layout = RelabelSplitSlots(*layout,SplitReadingOrder(split->layout));
    CommitSplitLayout(*split,*layout,name == "grid",std::move(done)); return true;
  }
  if (method == "split.nudge") {
    std::vector<int> path;
    if (request->GetSize() != 5 || !ReadPath(request,path) || request->GetType("index") != VTYPE_INT || request->GetType("delta") != VTYPE_INT ||
        request->GetInt("delta") < -1 || request->GetInt("delta") > 1 || !expanded_split_pane_.empty()) {
      done(400,"Invalid split divider",nullptr); return true;
    }
    const int index = request->GetInt("index"), delta = request->GetInt("delta");
    const auto layout = delta == 0 ? EqualizeSplitContainer(split->layout,path,split->linked)
                                   : NudgeSplitDivider(split->layout,path,index,250*delta,split->linked);
    if (!layout || !ValidSplitLayout(*layout,count)) { done(400,"Invalid split divider",nullptr); return true; }
    CommitSplitLayout(*split,*layout,split->linked,std::move(done)); return true;
  }
  if (method == "split.menu") {
    // The pane's layout menu, under its header button: presets, equal sizes,
    // swap, remove. Native owns every row and its action.
    if (request->GetSize() != 6 || request->GetType("x") != VTYPE_INT || request->GetType("y") != VTYPE_INT ||
        request->GetType("width") != VTYPE_INT || request->GetType("height") != VTYPE_INT) {
      done(400,"Invalid split menu button",nullptr); return true;
    }
    const double scale = state->window->GetDisplay()->GetDeviceScaleFactor();
    const auto cell = state->embedded->SplitCell(split->Slot(id));
    const int x=request->GetInt("x"), y=request->GetInt("y"), w=request->GetInt("width"), h=request->GetInt("height");
    if (w < 1 || h < 1 || w > 32 || h > 32 || x < cell.x/scale-1 || y < cell.y/scale-1 ||
        static_cast<double>(x)+w > (cell.x+cell.width)/scale+1 || static_cast<double>(y)+h > cell.y/scale+31) {
      done(400,"Menu button must be inside its pane header",nullptr); return true;
    }
    if (!state->menu_popup) state->menu_popup = std::make_shared<MenuPopup>(state);
    const auto anchor = split->anchor;
    const std::weak_ptr<WorkspaceController> weak = weak_from_this();
    // Each row re-checks that the same group is still on screen.
    auto run = [weak,anchor](std::function<void(WorkspaceController&,const SplitGroup&)> action) {
      return [weak,anchor,action] {
        const auto self = weak.lock(); if (!self) return;
        const auto* current = self->SelectedSplit();
        if (current && current->anchor == anchor) action(*self,*current);
      };
    };
    MenuPopup::Builder menu;
    static const std::map<std::string,std::pair<const char*,const char*>> names{
      {"columns",{"Side by side","columns"}},{"rows",{"Top and bottom","rows"}},{"main-left",{"Main on the left","mainLeft"}},
      {"main-top",{"Main on top","mainTop"}},{"grid",{"Grid 2 \u00d7 2","grid"}}};
    for (const auto& preset : SplitMenuPresets(count)) {
      auto layout = SplitPreset(preset,count);
      const bool current = layout && RelabelSplitSlots(*layout,SplitReadingOrder(split->layout)) == split->layout;
      const auto& [label,icon] = names.at(preset);
      std::string text = label; if (preset == "columns" && count > 2) text = std::to_string(count) + " columns";
      menu.Add(text,icon,run([preset](WorkspaceController& self,const SplitGroup& group) {
        auto chosen = SplitPreset(preset,static_cast<int>(group.pages.size()));
        if (chosen) self.CommitSplitLayout(group,RelabelSplitSlots(*chosen,SplitReadingOrder(group.layout)),preset == "grid",{});
      }),"",true,current);
    }
    menu.Add("Equal sizes","columns",run([](WorkspaceController& self,const SplitGroup& group) {
      self.CommitSplitLayout(group,EqualizeSplitLayout(group.layout),group.linked,{});
    }),"",split->layout != EqualizeSplitLayout(split->layout));
    menu.Separator();
    if (count > 2) {
      auto& swap = menu.Submenu("Swap with","swap");
      // Each page with the icon the sidebar shows for it. The snapshot is held
      // while its rows are read (child values belong to it).
      const auto snapshot = Snapshot(); const auto rows = snapshot->GetList("nodes");
      std::map<std::string,std::string> icons;
      for (size_t i = 0; rows && i < rows->GetSize(); ++i) {
        const auto row = rows->GetDictionary(i);
        if (split->Contains(row->GetString("id")) && row->HasKey("favicon")) icons[row->GetString("id")] = row->GetString("favicon");
      }
      for (const auto& other : split->pages) {
        if (other == id) continue;
        const auto node = std::find_if(tree_.nodes.begin(),tree_.nodes.end(),[&](const auto& n) { return n.id == other; });
        if (node == tree_.nodes.end()) continue;
        const auto title = !node->custom_title && !node->page_title.empty() ? node->page_title : node->title;
        swap.Add(title.substr(0,80),"tabs",run([id,other](WorkspaceController& self,const SplitGroup& group) {
          TreeCommand command{TreeAction::SwapSplit}; command.id = group.anchor; command.parent = id; command.split_pane = other;
          self.Dispatch(command,{},true);
        }),"",true,false,icons.contains(other) ? icons.at(other) : "");
      }
    } else menu.Add("Swap panes","swap",run([](WorkspaceController& self,const SplitGroup& group) {
      TreeCommand command{TreeAction::SwapSplit}; command.id = group.anchor; self.Dispatch(command,{},true);
    }));
    menu.Separator();
    menu.Add(count > 2 ? "Remove from split" : "Separate tabs","unlink",run([id,count](WorkspaceController& self,const SplitGroup& group) {
      TreeCommand command{count > 2 ? TreeAction::RemoveSplitPane : TreeAction::RemoveSplit}; command.id = group.anchor; command.parent = count > 2 ? id : "";
      self.Dispatch(command,{},true);
    }));
    const auto origin = state->window->GetClientAreaBoundsInScreen();
    state->menu_popup->Open(menu,CefPoint(origin.x + x,origin.y + y + h + 4),"split-layout-" + id);
    done(0,"",Snapshot()); return true;
  }
  if (method == "split.site") {
    if (!state->panel_popup || request->GetSize() != 6 || request->GetType("x") != VTYPE_INT || request->GetType("y") != VTYPE_INT ||
        request->GetType("width") != VTYPE_INT || request->GetType("height") != VTYPE_INT) {
      done(400,"Invalid split site button",nullptr); return true;
    }
    const double scale = state->window->GetDisplay()->GetDeviceScaleFactor();
    const auto cell = state->embedded->SplitCell(split->Slot(id));
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
  if (method == "split.beginMove") {
    // {id}: a press on the pane's header. It focuses the pane at once; if the
    // pointer then travels, the pane follows as a layout preview and moves on
    // release (another pane's edge: beside it; its middle: swap; the whole
    // area's edge strip: a full-length pane).
    POINT cursor{};
    const double scale = state->window->GetDisplay()->GetDeviceScaleFactor();
    const auto cell = state->embedded->SplitCell(split->Slot(id));
    if (request->GetSize() != 2 || !PrimaryButtonDown() || GetForegroundWindow() != state->window->GetWindowHandle() ||
        !GetCursorPos(&cursor) || !ScreenToClient(state->window->GetWindowHandle(),&cursor) ||
        cursor.x < cell.x || cursor.x >= cell.x+cell.width || cursor.y < cell.y || cursor.y >= cell.y+static_cast<int>(std::lround(31*scale))) {
      done(400,"A pane moves from its own header",nullptr); return true;
    }
    tree_.active_id = id; state->ui_editing = false; EnsureActive(); Publish();
    if (const auto browser = LiveBrowser(id)) FocusBrowser(browser);
    if (expanded_split_pane_.empty() && !split_resizing_) {
      split_move_ = SplitMove{split->anchor,id,split->layout,cursor};
      MoveSplitTick(++split_move_generation_);
    }
    TreeCommand activate{TreeAction::Activate}; activate.id = id; Dispatch(activate,std::move(done),true); return true;
  }
  if (method == "split.beginResize") {
    // {id, path, index, cross}: the gesture must start on that divider, with
    // the primary button down in the foreground window; it runs natively.
    std::vector<int> path;
    if (request->GetSize() != 5 || !ReadPath(request,path) || request->GetType("index") != VTYPE_INT || request->GetType("cross") != VTYPE_BOOL ||
        !expanded_split_pane_.empty() || !PrimaryButtonDown() || GetForegroundWindow() != state->window->GetWindowHandle()) {
      done(400,"Invalid split resize gesture",nullptr); return true;
    }
    const auto geometry = state->embedded->SplitPaneGeometry();
    const auto divider = std::find_if(geometry.dividers.begin(),geometry.dividers.end(),[&](const auto& d) {
      return d.path == path && d.index == request->GetInt("index") && d.cross == request->GetBool("cross"); });
    POINT cursor{};
    if (divider == geometry.dividers.end() || !GetCursorPos(&cursor) || !ScreenToClient(state->window->GetWindowHandle(),&cursor)) {
      done(400,"Resize must start at a split divider",nullptr); return true;
    }
    RECT gap{divider->rect.x,divider->rect.y,divider->rect.x+divider->rect.width,divider->rect.y+divider->rect.height};
    InflateRect(&gap,3,3);
    if (!PtInRect(&gap,cursor)) { done(400,"Resize must start at a split divider",nullptr); return true; }
    split_resize_anchor_ = split->anchor; split_resize_initial_ = split->layout; split_resize_divider_ = *divider; split_resizing_ = true;
    split_drag_target_.reset();
    FocusShell(state); ResizeSplitTick(++split_resize_generation_); done(0,"",Snapshot()); return true;
  }
  if (request->GetSize() != 2) { done(400,"Invalid split command",nullptr); return true; }
  if (method == "split.expand") expanded_split_pane_ = expanded_split_pane_ == id ? "" : id;
  tree_.active_id = id;
  EnsureActive(); Publish();
  if (!state->ui_editing) if (const auto browser = LiveBrowser(id)) FocusBrowser(browser);
  TreeCommand activate{TreeAction::Activate}; activate.id = id; Dispatch(activate,std::move(done),true); return true;
}

bool WorkspaceController::FocusSplitNeighbour(int dx, int dy) {
  const auto state = state_.lock(); const auto* split = SelectedSplit();
  if (!state || !state->embedded || !split || !ready_ || shutting_down_ || !closing_.empty() || !expanded_split_pane_.empty()) return false;
  const int next = NeighbourSplitPane(state->embedded->SplitPaneGeometry(),split->Slot(tree_.active_id),dx,dy);
  if (next < 0) return false;
  const auto id = split->pages[next];
  tree_.active_id = id; state->ui_editing = false; EnsureActive(); Publish();
  if (const auto browser = LiveBrowser(id)) FocusBrowser(browser);
  TreeCommand activate{TreeAction::Activate}; activate.id = id; Dispatch(activate,{},true);
  return true;
}

bool WorkspaceController::ToggleSplitExpand() {
  const auto* split = SelectedSplit();
  if (!split || !ready_ || shutting_down_) return false;
  expanded_split_pane_ = expanded_split_pane_ == tree_.active_id ? "" : tree_.active_id;
  EnsureActive(); Publish();
  if (const auto browser = LiveBrowser(tree_.active_id)) FocusBrowser(browser);
  return true;
}

bool WorkspaceController::SplitResizeHovered(const std::string& anchor) const {
  const auto state = state_.lock(); const auto* split = SelectedSplit();
  if (!state || !state->window || !state->embedded || !split || split->anchor != anchor || !expanded_split_pane_.empty()) return false;
  const auto root = state->window->GetWindowHandle();
  POINT point{}; if (GetForegroundWindow() != root || !GetCursorPos(&point) || GetAncestor(WindowFromPoint(point),GA_ROOT) != root || !ScreenToClient(root,&point)) return false;
  const auto geometry = state->embedded->SplitPaneGeometry();
  return std::any_of(geometry.dividers.begin(),geometry.dividers.end(),[&](const auto& divider) { return divider.rect.Contains(point.x,point.y); });
}

void WorkspaceController::MoveSplitTick(unsigned generation) {
  const auto state = state_.lock(); const auto* split = SelectedSplit();
  if (!split_move_ || generation != split_move_generation_) return;
  auto& move = *split_move_;
  auto finish = [&](bool settle = false) {
    const bool shown = split_move_ && split_move_->unsplit; split_move_.reset();
    if (shown) PublishLayout();
    if (!split_move_visual_) return;
    if (!settle) { split_move_visual_->preview.Hide(); return; }
    // The website windows take the new size now and paint it a frame or two
    // later: the preview covers them until then instead of flashing old content.
    CefPostDelayedTask(TID_UI,base::BindOnce([](std::weak_ptr<WorkspaceController> weak,unsigned generation) {
      if (const auto self = weak.lock(); self && self->split_move_visual_ && !self->split_move_ && generation == self->split_move_generation_)
        self->split_move_visual_->preview.Hide();
    },weak_from_this(),split_move_generation_),160);
  };
  const HWND peek = state && state->sidebar_peek && state->sidebar_peek->window_visible() ? state->sidebar_peek->host() : nullptr;
  if (!state || !state->window || !state->embedded || !split || split->anchor != move.anchor || split->layout != move.initial ||
      !split->Contains(move.page) || shutting_down_ || (GetAsyncKeyState(VK_ESCAPE) & 0x8000) ||
      (GetForegroundWindow() != state->window->GetWindowHandle() && (!peek || GetForegroundWindow() != peek))) { finish(); return; }
  if (!PrimaryButtonDown()) {
    const auto layout = move.moving && !move.unsplit ? move.layout : std::nullopt; const bool linked = move.linked;
    const bool unsplit = move.moving && move.unsplit; const auto page = move.page;
    finish(layout.has_value() || unsplit);
    if (layout) CommitSplitLayout(*split,*layout,linked,{});
    if (unsplit) {
      // Dropped on the sidebar: the tab leaves the split and lands directly
      // below the group (or the page that stays); the rest stays on screen.
      TreeCommand command{TreeAction::RemoveSplitPane}; command.id = split->anchor; command.parent = page;
      Dispatch(command,[weak = weak_from_this()](int code,const std::string&,CefRefPtr<CefDictionaryValue>) {
        const auto self = weak.lock(); if (!self || code) return;
        if (const auto browser = self->LiveBrowser(self->tree_.active_id)) FocusBrowser(browser);
      },true);
    }
    return;
  }
  const HWND root = state->window->GetWindowHandle();
  POINT cursor{};
  if (GetCursorPos(&cursor) && ScreenToClient(root,&cursor)) {
    const double scale = state->window->GetDisplay()->GetDeviceScaleFactor();
    if (!move.moving && std::hypot(cursor.x-move.start.x,cursor.y-move.start.y) >= 6*scale) {
      move.moving = true;
      if (!split_move_visual_) split_move_visual_ = std::make_shared<SplitMoveVisual>();
      split_move_visual_->labels = SplitPageLabels(state);
    }
    if (move.moving) {
      const int panes = static_cast<int>(split->pages.size()), slot = split->Slot(move.page);
      const auto area = state->embedded->SplitDropArea();
      const auto now = state->embedded->SplitPaneGeometry();
      const double x = (cursor.x-area.x) / static_cast<double>(std::max(1,area.width));
      const double y = (cursor.y-area.y) / static_cast<double>(std::max(1,area.height));
      // Where it would land: the whole area's edge strip, or a part of another pane.
      int target = -2; double px = x, py = y;
      if (x >= 0 && x <= 1 && y >= 0 && y <= 1 && std::min({x,1-x,y,1-y}) <= .06) target = -1;
      else for (int i = 0; i < panes; ++i) if (i != slot && now.panes[i].Contains(cursor.x,cursor.y)) {
        target = i; const auto& c = now.panes[i];
        px = (cursor.x-c.x) / static_cast<double>(std::max(1,c.width)); py = (cursor.y-c.y) / static_cast<double>(std::max(1,c.height));
      }
      const bool middle = target >= 0 && std::min({px,1-px,py,1-py}) > .28;
      const bool vertical = std::min(py,1-py) < std::min(px,1-px);
      const auto edge = vertical ? (py < .5 ? SplitEdge::Top : SplitEdge::Bottom) : (px < .5 ? SplitEdge::Left : SplitEdge::Right);
      move.layout = target == -2 ? std::nullopt : middle ? std::optional<SplitNode>(SwapSplitSlots(split->layout,slot,target))
                                                         : MoveSplitPane(split->layout,slot,target,edge);
      move.linked = middle && split->linked;
      move.key = std::to_string(target) + (middle ? "m" : std::to_string(static_cast<int>(edge)));
      POINT origin{0,0}; ClientToScreen(root,&origin);
      // Over the sidebar (docked, left of the page area, or the floating one): leave the split.
      // A hidden sidebar does not open on hover while the button is held: carried
      // to the left edge of the page area, the floating sidebar slides out.
      if (!state->sidebar_visible && state->sidebar_peek && !state->sidebar_peek->is_open() &&
          cursor.x < area.x + static_cast<int>(std::lround(24*scale)) && cursor.y >= area.y && cursor.y < area.y+area.height)
        state->sidebar_peek->Show();
      POINT screen_cursor{cursor.x+origin.x,cursor.y+origin.y}; RECT floating{};
      const bool was_unsplit = move.unsplit;
      move.unsplit = (state->sidebar_visible && cursor.x >= 0 && cursor.x < area.x && cursor.y >= area.y && cursor.y < area.y+area.height) ||
                     (peek && GetWindowRect(peek,&floating) && PtInRect(&floating,screen_cursor));
      if (move.unsplit != was_unsplit) PublishLayout();  // The sidebars show (or drop) the landing row.
      auto after = move.layout ? state->embedded->SplitPaneGeometry(*move.layout,panes,move.linked) : now;
      if (move.unsplit) {
        // The others close up as they will; the carried pane slides out toward the sidebar.
        move.layout.reset(); move.key = "sidebar";
        const auto rest = panes > 2 ? RemoveSplitPane(split->layout,slot) : std::nullopt;
        const auto closed = rest ? state->embedded->SplitPaneGeometry(*rest,panes-1,false) : SplitGeometry{};
        for (int i = 0; i < panes; ++i) {
          if (i == slot) after.panes[i] = {area.x,now.panes[i].y,0,now.panes[i].height};
          else after.panes[i] = rest ? closed.panes[i < slot ? i : i-1] : SplitRect{area.x,area.y,area.width,area.height};
        }
      }
      auto screen = [origin](const SplitRect& r) { return CefRect(origin.x+r.x,origin.y+r.y,r.width,r.height); };
      std::vector<SplitPreviewCard> cards;
      for (int i = 0; i < panes; ++i) cards.push_back({split->pages[i],screen(now.panes[i]),screen(after.panes[i]),i == slot});
      split_move_visual_->preview.Update(state,move.layout || move.unsplit ? move.key : "none",
                                         CefRect(origin.x+area.x,origin.y+area.y,area.width,area.height),cards,split_move_visual_->labels,
                                         static_cast<float>(scale));
    }
  }
  CefPostDelayedTask(TID_UI,base::BindOnce([](std::weak_ptr<WorkspaceController> weak,unsigned generation) {
    if (const auto self = weak.lock()) self->MoveSplitTick(generation);
  },weak_from_this(),generation),15);
}

void WorkspaceController::ResizeSplitTick(unsigned generation) {
  const auto state = state_.lock(); const auto* split = SelectedSplit();
  if (!split_resizing_ || generation != split_resize_generation_) return;
  const bool cancelled = !state || !state->window || !state->embedded || !split || split->anchor != split_resize_anchor_ ||
    split->layout != split_resize_initial_ || shutting_down_ || (GetAsyncKeyState(VK_ESCAPE) & 0x8000) ||
    GetForegroundWindow() != state->window->GetWindowHandle();
  if (cancelled) {
    split_resizing_ = false; split_preview_.reset(); split_drag_target_.reset();
    if (state && state->embedded) state->embedded->SetSplitLiveResize(false);
    if (state && !shutting_down_) { EnsureActive(); Publish(); } return;
  }
  const int panes = static_cast<int>(split->pages.size());
  if (!PrimaryButtonDown()) {
    split_resizing_ = false;
    state->embedded->SetSplitLiveResize(false);
    const auto layout = split_drag_target_ ? *split_drag_target_ : split_resize_initial_;
    split_drag_target_.reset();
    if (layout == split->layout) { split_preview_.reset(); EnsureActive(); Publish(); }
    else CommitSplitLayout(*split,layout,split->linked,{});
    if (state->content) FocusBrowser(state->content); return;
  }
  // The cards (headers, frames, dividers) follow every frame; the website
  // windows only move and clip, and resize once the pointer rests.
  POINT cursor{};
  const auto now = GetTickCount64();
  if (GetCursorPos(&cursor) && ScreenToClient(state->window->GetWindowHandle(),&cursor)) {
    const auto layout = DragSplitDivider(split_resize_initial_,panes,state->embedded->SplitPaneArea(),
                                         state->embedded->SplitPaneMetrics(),split_resize_divider_,cursor.x,cursor.y,split->linked);
    if (layout && (!split_drag_target_ || *split_drag_target_ != *layout)) {
      split_drag_target_ = *layout; split_drag_moved_ = now; split_preview_ = {split->anchor,*layout};
      state->embedded->SetSplitLiveResize(true); EnsureActive(); PublishLayout();
    }
  }
  if (split_drag_target_ && now - split_drag_moved_ >= kSplitSettleMs && state->embedded->SplitLiveResize()) {
    state->embedded->SetSplitLiveResize(false); EnsureActive();
  }
  CefPostDelayedTask(TID_UI,base::BindOnce([](std::weak_ptr<WorkspaceController> weak,unsigned generation) {
    if (const auto self = weak.lock()) self->ResizeSplitTick(generation);
  },weak_from_this(),generation),16);
}
}  // namespace red
