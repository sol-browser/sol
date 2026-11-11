#pragma once
#include "app/state.h"
namespace red {
struct DeviceConfig;
class DevToolsWindow;
enum class DevToolsSide { Right, Bottom, Left, Window };

// Native siblings of the website, created with CEF's child WindowInfo. Never
// reparent a live Chromium HWND or give the frontend the Shell's IPC bridge.
class DevToolsDock {
 public:
  explicit DevToolsDock(std::weak_ptr<State> state) : state_(std::move(state)) {}
  ~DevToolsDock();
  void Configure(int target, CefWindowInfo& info);
  void Attached(int target, CefRefPtr<CefBrowser> frontend);
  void Detached(int target, int frontend);
  CefRect PageBounds(int target, const CefRect& area) const;
  void Layout(const CefRect& area, int active, bool fullscreen);
  HWND StackAfter(HWND above) const;
  void SetSide(int target, DevToolsSide side);
  void ToggleDock(int target);
  CefRefPtr<CefDictionaryValue> Describe() const;
  bool Control(int frontend, const std::string& action);
  // From the separate window's own strip: acts on that window's page, which
  // need not be the active one (device preview only works on the active page).
  bool ControlTarget(int target, const std::string& action);
  bool IsActive(int target) const { return target == active_; }
  bool DeviceEnabled(int target) const;
  // Separate window: false until the window for |target| exists; it then shows
  // the page's DevTools itself (ShowPageDevTools asks this first).
  bool ReadyToShow(int target);
  void RememberWindowBounds(const CefRect& bounds) { window_bounds_ = bounds; }
  std::shared_ptr<DevToolsWindow> SeparateWindow(int target) const;  // Probes.
  // |live|: a divider drag in progress. Moves only the native windows; the
  // caller replies with LayoutSnapshot and the final (non-live) resize
  // publishes the full workspace once.
  bool Resize(int frontend, int extent, bool live = false);
  bool Hovered(int frontend) const;
  void SetLive(bool live) { live_ = live; }
  bool SetDevice(int frontend, const DeviceConfig& config);
  bool ToggleDevice(int target);
  CefRefPtr<CefDictionaryValue> DescribeDevice() const;
 private:
  struct Entry;
  std::shared_ptr<Entry> Ensure(int target);
  void Relayout();
  void Reopen(int target, int attempt = 0);
  CefRect DockedPageBounds(int target, const CefRect& area) const;
  CefRect DeviceViewport(int target, const CefRect& area, double* scale = nullptr) const;
  std::weak_ptr<State> state_;
  std::map<int, std::shared_ptr<Entry>> entries_;
  DevToolsSide preferred_ = DevToolsSide::Right;
  CefRect area_;
  int active_ = -1;
  bool fullscreen_ = false;
  bool refresh_queued_ = false;
  CefRect window_bounds_;  // Last separate-window bounds (DIP screen), reused for the next one.
  bool live_ = false;
};
}  // namespace red
