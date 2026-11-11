#include "windows/devtools_dock.h"
#include "windows/device_emulation.h"
#include "windows/devtools.h"
#include "windows/devtools_window.h"
#include "windows/embedded_content.h"
#include "windows/focus.h"
#include "workspace/controller.h"
#include "include/cef_app.h"
#include "include/views/cef_display.h"
#include "include/base/cef_callback.h"
#include "include/wrapper/cef_closure_task.h"
#include <algorithm>
#include <cmath>

namespace red {
namespace {
constexpr int kHeader = 28, kDivider = 5;
int DeviceToolbarHeight(const CefRect& page, float scale) { return std::lround((page.width / scale < 340 ? 88 : 60) * scale); }
float Scale(const std::shared_ptr<State>& s) { return s->window->GetDisplay()->GetDeviceScaleFactor(); }
void Place(HWND window, const CefRect& r, bool show) {
  if (!window || !IsWindow(window)) return;
  if (!show) { if (IsWindowVisible(window)) ShowWindow(window, SW_HIDE); return; }
  RECT now{}; GetWindowRect(window, &now);
  POINT origin{now.left, now.top}; ScreenToClient(GetParent(window), &origin);
  if (origin.x != r.x || origin.y != r.y || now.right - now.left != r.width || now.bottom - now.top != r.height || !IsWindowVisible(window))
    SetWindowPos(window, nullptr, r.x, r.y, r.width, r.height, SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW);
}
CefRefPtr<CefDictionaryValue> Bounds(const CefRect& r, float scale) {
  auto result = CefDictionaryValue::Create();
  result->SetDouble("x", r.x / scale); result->SetDouble("y", r.y / scale);
  result->SetDouble("width", r.width / scale); result->SetDouble("height", r.height / scale);
  return result;
}
}
struct DevToolsDock::Entry {
  int frontend = -1;
  DevToolsSide side;
  DevToolsSide last_dock=DevToolsSide::Right;
  int width = 420, height = 300;
  bool reopening = false;
  CefRect header, divider;
  std::shared_ptr<DeviceEmulation> device;
  std::shared_ptr<DevToolsWindow> window;  // Separate window (side Window) once created.
  explicit Entry(DevToolsSide value) : side(value) {}
};
DevToolsDock::~DevToolsDock() {
  // A separate window outlives no dock: it would stay open without its tools.
  for (const auto& [target, entry] : entries_) if (entry->window) entry->window->Close();
}
std::shared_ptr<DevToolsDock::Entry> DevToolsDock::Ensure(int target) {
  if (auto at = entries_.find(target); at != entries_.end()) return at->second;
  auto entry = std::make_shared<Entry>(preferred_); entries_[target] = entry; return entry;
}
void DevToolsDock::Configure(int target, CefWindowInfo& info) {
  auto s = state_.lock(); auto entry = Ensure(target);
  if (s && s->window && entry->side != DevToolsSide::Window)
    info.SetAsChild(s->window->GetWindowHandle(), CefRect(-4000, -4000, 420, 400));
  else if (entry->window && entry->window->ready())
    info.SetAsChild(entry->window->hwnd(), entry->window->FrontendBounds());
  else info.SetAsPopup(nullptr, "Developer Tools");  // Not reached: ReadyToShow creates the window first.
  info.runtime_style = CEF_RUNTIME_STYLE_CHROME;
}
bool DevToolsDock::ReadyToShow(int target) {
  auto s = state_.lock(); auto entry = Ensure(target);
  if (entry->side != DevToolsSide::Window) return true;
  if (entry->window && entry->window->ready()) return true;
  if (!s || !s->window) return false;
  if (!entry->window) {
    // Remembered bounds, or a window over the right part of the browser window.
    auto bounds = window_bounds_;
    if (bounds.IsEmpty()) {
      const auto owner = s->window->GetBounds();
      const int width = std::min(980, std::max(520, owner.width * 3 / 5)), height = std::max(420, owner.height - 120);
      bounds = CefRect(owner.x + owner.width - width - 40, owner.y + 60, width, height);
    }
    entry->window = std::make_shared<DevToolsWindow>(state_, target, bounds, [weak = state_, target] {
      auto s = weak.lock(); if (!s || !s->devtools_dock) return;
      auto& entries = s->devtools_dock->entries_;
      const auto at = entries.find(target);
      const auto page = s->browsers.find(target);
      if (at == entries.end() || page == s->browsers.end() || at->second->side != DevToolsSide::Window) {
        if (at != entries.end() && at->second->window) { at->second->window->Close(); at->second->window.reset(); }
        return;
      }
      at->second->reopening = false;
      ShowPageDevTools(s, page->second);
    });
    entry->window->Open();
  }
  return false;
}
void DevToolsDock::Attached(int target, CefRefPtr<CefBrowser> frontend) {
  Ensure(target)->frontend = frontend->GetIdentifier();
  if (const auto entry = Ensure(target); entry->side == DevToolsSide::Window && entry->window) entry->window->Attach(frontend);
  // Chrome finalizes its initial window bounds after OnAfterCreated.
  CefPostTask(TID_UI, base::BindOnce([](std::weak_ptr<State> weak) {
    if (auto s = weak.lock(); s && s->devtools_dock) s->devtools_dock->Relayout();
  }, state_));
}
CefRect DevToolsDock::DockedPageBounds(int target, const CefRect& area) const {
  const auto at = entries_.find(target); auto s = state_.lock();
  if (!s || !s->window || fullscreen_ || at == entries_.end() || at->second->frontend == -1) return area;
  // A separate window carries its own controls: the page keeps the whole area.
  if (at->second->side==DevToolsSide::Window) return area;
  const auto& entry = *at->second; const float scale = Scale(s);
  const int divider = std::max(1, static_cast<int>(std::lround(kDivider * scale)));
  const bool bottom = entry.side == DevToolsSide::Bottom;
  const int available = bottom ? area.height : area.width;
  const int min_page = std::min(available / 2, static_cast<int>(std::lround((bottom ? 140 : 200) * scale)));
  const int min_tools = std::min(available / 2, static_cast<int>(std::lround((bottom ? 160 : 260) * scale)));
  const int max_extent = std::max(1, available - min_page - divider);
  const int extent = std::clamp(static_cast<int>(std::lround((bottom ? entry.height : entry.width) * scale)),
      std::min(max_extent, std::max(1, min_tools)), max_extent);
  auto page = area;
  if (bottom) page.height = std::max(1, available - extent - divider);
  else { page.width = std::max(1, available - extent - divider); if (entry.side == DevToolsSide::Left) page.x += extent + divider; }
  return page;
}
CefRect DevToolsDock::DeviceViewport(int target, const CefRect& area, double* preview_scale) const {
  auto s = state_.lock(); const auto at = entries_.find(target);
  if (!s || !s->window || fullscreen_ || at == entries_.end() || !at->second->device || !at->second->device->config().enabled) return area;
  const auto& config = at->second->device->config();
  const float scale = Scale(s);
  const int toolbar = DeviceToolbarHeight(area, scale), padding = std::lround(12 * scale);
  const int width = std::max(1, area.width - 2 * padding), height = std::max(1, area.height - toolbar - 2 * padding);
  const double fit = std::min(width / (config.width * static_cast<double>(scale)), height / (config.height * static_cast<double>(scale)));
  const double zoom = std::max(.001, std::min(fit, config.scale > 0 ? config.scale : 1.));
  const int w = std::max(1, static_cast<int>(std::lround(config.width * scale * zoom)));
  const int h = std::max(1, static_cast<int>(std::lround(config.height * scale * zoom)));
  if (preview_scale) *preview_scale = zoom;
  return CefRect(area.x + (area.width - w) / 2, area.y + toolbar + padding, w, h);
}
CefRect DevToolsDock::PageBounds(int target, const CefRect& area) const {
  return DeviceViewport(target, DockedPageBounds(target, area));
}
void DevToolsDock::Layout(const CefRect& area, int active, bool fullscreen) {
  bool changed = area_ != area || active_ != active || fullscreen_ != fullscreen;
  area_ = area; active_ = active; fullscreen_ = fullscreen;
  auto s = state_.lock(); if (!s || !s->window) return;
  const int header = std::lround(kHeader * Scale(s));
  const int gap = std::max(1, static_cast<int>(std::lround(kDivider * Scale(s))));
  for (const auto& [target, entry] : entries_) {
    const auto frontend = s->browsers.find(entry->frontend);
    if (frontend == s->browsers.end()) continue;
    if (target == active && entry->device) {
      double zoom = 1;
      DeviceViewport(target, DockedPageBounds(target, area), &zoom);
      entry->device->Apply(zoom);
    }
    if (entry->side == DevToolsSide::Window) {
      changed |= !entry->header.IsEmpty() || !entry->divider.IsEmpty();
      entry->header = {}; entry->divider = {};
      continue;
    }
    const HWND hwnd = frontend->second->GetHost()->GetWindowHandle();
    const bool show = target == active && !fullscreen && !entry->reopening;
    if (!show) { Place(hwnd, {}, false); continue; }
    const auto page = DockedPageBounds(target, area);
    auto dock = area, divider = area;
    if (entry->side == DevToolsSide::Bottom) {
      divider.y = page.y + page.height; divider.height = gap;
      dock.y = divider.y + gap; dock.height = area.y + area.height - dock.y;
    } else if (entry->side == DevToolsSide::Left) {
      dock.width = page.x - area.x - gap; divider.x = page.x - gap; divider.width = gap;
    } else {
      divider.x = page.x + page.width; divider.width = gap;
      dock.x = divider.x + gap; dock.width = area.x + area.width - dock.x;
    }
    const CefRect title(dock.x, dock.y, dock.width, std::min(header, dock.height));
    changed |= title != entry->header || divider != entry->divider;
    entry->header = title; entry->divider = divider;
    Place(hwnd, CefRect(dock.x, dock.y + header, std::max(1, dock.width), std::max(1, dock.height - header)), true);
  }
  // Publish after the caller commits its active page. No recursive layouts or
  // privileged bridge in DevTools: the existing trusted Shell paints controls
  // in the unoccupied header/gutter between native browser children.
  // Separate windows show whether their page is active (device preview).
  if (changed) for (const auto& [target, entry] : entries_) if (entry->window) entry->window->Publish();
  if (changed && !live_ && !refresh_queued_) {
    refresh_queued_ = true;
    CefPostTask(TID_UI, base::BindOnce([](std::weak_ptr<State> weak) {
      if (auto s = weak.lock(); s && s->devtools_dock) {
        s->devtools_dock->refresh_queued_ = false;
        if (s->workspace && !s->workspace->WindowClosing()) s->workspace->Refresh();
      }
    }, state_));
  }
}
void DevToolsDock::Relayout() {
  if (auto s = state_.lock(); s && s->embedded && s->window && s->workspace && !s->workspace->WindowClosing()) {
    s->embedded->Relayout(); s->embedded->QueueRaise(); s->workspace->Refresh();
  }
}
HWND DevToolsDock::StackAfter(HWND above) const {
  const auto state = state_.lock();
  const auto active = entries_.find(active_);
  if (!state || !state->window || fullscreen_ || active == entries_.end() ||
      active->second->side == DevToolsSide::Window || active->second->reopening) return above;
  const auto frontend = state->browsers.find(active->second->frontend);
  if (frontend == state->browsers.end()) return above;
  const HWND root = state->window->GetWindowHandle(), child = frontend->second->GetHost()->GetWindowHandle();
  if (IsIconic(root) || !IsWindowVisible(child) || GetAncestor(child, GA_PARENT) != root) return above;
  // The same stack pass places the site windows below the dock. Leaving the
  // inspector in the old z-order lets Alloy's full-client input child cover it.
  if (GetWindow(child, GW_HWNDPREV) != above &&
      !SetWindowPos(child, above ? above : HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE)) return above;
  return child;
}
CefRefPtr<CefDictionaryValue> DevToolsDock::Describe() const {
  auto s = state_.lock(); const auto at = entries_.find(active_);
  if (!s || !s->window || fullscreen_ || at == entries_.end()) return nullptr;
  const auto& entry = *at->second;
  if (entry.frontend == -1 || entry.reopening) return nullptr;
  auto result = CefDictionaryValue::Create(); result->SetInt("id", entry.frontend);
  result->SetString("side", entry.side==DevToolsSide::Window ? "window" : entry.side == DevToolsSide::Bottom ? "bottom" : entry.side == DevToolsSide::Left ? "left" : "right");
  const float scale = Scale(s);
  const auto page = DockedPageBounds(active_, area_);
  if (entry.side==DevToolsSide::Window) return nullptr;  // Its controls are in its own window.
  result->SetInt("extent", std::lround(((entry.side == DevToolsSide::Bottom ? area_.height - page.height : area_.width - page.width) / scale) - kDivider));
  result->SetDictionary("header", Bounds(entry.header, scale)); result->SetDictionary("divider", Bounds(entry.divider, scale));
  return result;
}
bool DevToolsDock::Control(int frontend, const std::string& action) {
  auto s = state_.lock(); const auto at = entries_.find(active_);
  if (!s || !s->content || s->content->GetIdentifier() != active_ || at == entries_.end() || at->second->frontend != frontend) return false;
  if (action == "device") return ToggleDevice(active_);
  if (action == "focus") {
    if (fullscreen_ || !at->second->device || !at->second->device->config().enabled || at->second->reopening) return false;
    // Re-focusing a native widget during pointerdown can discard Blink's
    // pending pointer capture. A real Shell click already owns keyboard focus.
    const auto shell=s->browsers.find(s->shell_id);
    if(shell!=s->browsers.end() && !BrowserOwnsKeyboard(s,shell->second)) {
      if(BrowserOwnsKeyboard(s,s->content)) s->content->GetHost()->SetFocus(false);
      FocusShell(s);
    }
    return true;
  }
  if (action == "close") ClosePageDevTools(s, active_);
  else SetSide(active_, action == "bottom" ? DevToolsSide::Bottom : action == "left" ? DevToolsSide::Left : action == "window" ? DevToolsSide::Window : DevToolsSide::Right);
  return true;
}
bool DevToolsDock::ControlTarget(int target, const std::string& action) {
  auto s = state_.lock(); const auto at = entries_.find(target);
  if (!s || at == entries_.end() || at->second->reopening) return false;
  if (action == "device") return target == active_ && ToggleDevice(target);
  if (action == "close") { ClosePageDevTools(s, target); return true; }
  if (action != "right" && action != "bottom" && action != "left") return false;
  SetSide(target, action == "bottom" ? DevToolsSide::Bottom : action == "left" ? DevToolsSide::Left : DevToolsSide::Right);
  return true;
}
std::shared_ptr<DevToolsWindow> DevToolsDock::SeparateWindow(int target) const {
  const auto at = entries_.find(target);
  return at == entries_.end() ? nullptr : at->second->window;
}
bool DevToolsDock::DeviceEnabled(int target) const {
  const auto at = entries_.find(target);
  return at != entries_.end() && at->second->device && at->second->device->config().enabled;
}
bool DevToolsDock::Resize(int frontend, int extent, bool live) {
  const auto at = entries_.find(active_); auto s = state_.lock();
  if (!s || !s->content || s->content->GetIdentifier() != active_ || fullscreen_ || at == entries_.end() || at->second->frontend != frontend) return false;
  auto& entry = *at->second;
  if (entry.side == DevToolsSide::Window || entry.reopening) return false;
  (entry.side == DevToolsSide::Bottom ? entry.height : entry.width) = extent;
  if (!live) { Relayout(); return true; }
  // Per pointer move: no workspace snapshot or Shell re-render, which used to
  // run three times per move on the UI thread that also drives DevTools.
  if (!s->embedded || !s->window || !s->workspace || s->workspace->WindowClosing()) return false;
  live_ = true; s->embedded->Relayout(); live_ = false;
  return true;
}
bool DevToolsDock::Hovered(int frontend) const {
  const auto at = entries_.find(active_); auto s = state_.lock();
  if (!s || !s->window || fullscreen_ || at == entries_.end() || at->second->frontend != frontend) return false;
  POINT point{}; const HWND root = s->window->GetWindowHandle();
  if (!GetCursorPos(&point) || GetAncestor(WindowFromPoint(point), GA_ROOT) != root || !ScreenToClient(root, &point)) return false;
  const auto& r = at->second->divider;
  const RECT rect{r.x, r.y, r.x + r.width, r.y + r.height};
  return PtInRect(&rect, point) != FALSE;
}
bool DevToolsDock::SetDevice(int frontend, const DeviceConfig& config) {
  auto s = state_.lock(); const auto at = entries_.find(active_);
  if (!s || !s->content || fullscreen_ || s->content->GetIdentifier() != active_ || at == entries_.end() ||
      at->second->frontend != frontend || at->second->reopening) return false;
  auto& entry = *at->second;
  if (!entry.device) entry.device = std::make_shared<DeviceEmulation>(state_, s->content);
  entry.device->Set(config); Relayout(); return true;
}
bool DevToolsDock::ToggleDevice(int target) {
  const auto at = entries_.find(target); if (at == entries_.end() || target != active_) return false;
  auto config = at->second->device ? at->second->device->config() : DeviceConfig{};
  config.enabled = !config.enabled; return SetDevice(at->second->frontend, config);
}
CefRefPtr<CefDictionaryValue> DevToolsDock::DescribeDevice() const {
  auto s = state_.lock(); const auto at = entries_.find(active_);
  if (!s || !s->window || fullscreen_ || at == entries_.end() || at->second->frontend == -1 || at->second->reopening) return nullptr;
  const auto& entry = *at->second;
  const auto config = entry.device ? entry.device->config() : DeviceConfig{};
  const float scale = Scale(s); const auto area = DockedPageBounds(active_, area_);
  auto toolbar = area; toolbar.height = DeviceToolbarHeight(area, scale);
  const auto viewport = DeviceViewport(active_, area);
  auto result = CefDictionaryValue::Create(); result->SetInt("id", entry.frontend);
  result->SetBool("enabled", config.enabled); result->SetInt("width", config.width); result->SetInt("height", config.height);
  result->SetDouble("dpr", config.dpr); result->SetDouble("scale", config.scale);
  result->SetDouble("effectiveScale", entry.device ? entry.device->effective_scale() : 1.);
  result->SetBool("mobile", config.mobile); result->SetBool("touch", config.touch);
  result->SetBool("pending", entry.device && entry.device->pending()); result->SetString("error", entry.device ? entry.device->error() : "");
  result->SetDictionary("toolbar", Bounds(toolbar, scale)); result->SetDictionary("viewport", Bounds(viewport, scale));
  result->SetDictionary("area", Bounds(area, scale)); return result;
}
void DevToolsDock::Detached(int target, int frontend) {
  const auto at = entries_.find(target);
  if (at == entries_.end() || at->second->frontend != frontend) return;
  auto entry = at->second; entry->frontend = -1;
  if (entry->window) {
    entry->window->FrontendClosed(frontend);
    // Moving to a dock (or closing) ends the window; reopening into a window keeps it.
    if (!(entry->reopening && entry->side == DevToolsSide::Window)) { entry->window->Close(); entry->window.reset(); }
  }
  if (!entry->reopening) entries_.erase(at);
  else CefPostTask(TID_UI, base::BindOnce([](std::weak_ptr<State> weak, int target) {
    if (auto s = weak.lock(); s && s->devtools_dock) s->devtools_dock->Reopen(target);
  }, state_, target));
  Relayout();
}
void DevToolsDock::Reopen(int target, int attempt) {
  auto s = state_.lock(); const auto at = entries_.find(target);
  if (!s || !s->window || at == entries_.end()) return;
  const auto page = s->browsers.find(target);
  if (page == s->browsers.end() || !s->workspace || s->workspace->WindowClosing()) { entries_.erase(at); return; }
  if (page->second->GetHost()->HasDevTools()) {
    if (attempt < 10) CefPostDelayedTask(TID_UI, base::BindOnce([](std::weak_ptr<State> weak, int target, int attempt) {
      if (auto s = weak.lock(); s && s->devtools_dock) s->devtools_dock->Reopen(target, attempt + 1);
    }, state_, target, attempt), 50);
    else { entries_.erase(at); Relayout(); }
    return;
  }
  at->second->reopening = false; ShowPageDevTools(s, page->second);
}
void DevToolsDock::SetSide(int target, DevToolsSide side) {
  auto s = state_.lock(); const auto at = entries_.find(target);
  if (!s || at == entries_.end() || at->second->side == side) return;
  auto entry = at->second;
  const bool recreate = entry->side == DevToolsSide::Window || side == DevToolsSide::Window;
  if (entry->side!=DevToolsSide::Window) entry->last_dock=entry->side;
  entry->side = preferred_ = side;
  if (recreate) {
    entry->reopening = true;
    const auto page = s->browsers.find(target);
    if (page != s->browsers.end()) page->second->GetHost()->CloseDevTools();
  }
  Relayout();
}
void DevToolsDock::ToggleDock(int target) {
  const auto at=entries_.find(target);if(at==entries_.end())return;
  SetSide(target,at->second->side==DevToolsSide::Window ? at->second->last_dock : DevToolsSide::Window);
}
}  // namespace red
