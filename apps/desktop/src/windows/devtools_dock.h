#pragma once
#include "app/state.h"
namespace red {
struct DeviceConfig;
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
  void SetSide(int target, DevToolsSide side);
  void ToggleDock(int target);
  CefRefPtr<CefDictionaryValue> Describe() const;
  bool Control(int frontend, const std::string& action);
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
  bool live_ = false;
};
}  // namespace red
