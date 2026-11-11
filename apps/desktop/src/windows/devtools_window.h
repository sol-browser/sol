#pragma once
#include <functional>
#include "app/state.h"
#include "include/views/cef_window.h"
#include "include/wrapper/cef_message_router.h"

namespace red {
// The separate DevTools window (docs/devtools.md, "Own window"). Sol's own window,
// not Chrome's: its top strip is a trusted UI document with the same placement
// controls as the docked inspector (sol://ui/devtools.html), and the DevTools
// frontend is a native child below it, created with CEF's child WindowInfo like
// the docked one. Chrome's own DevTools window could not carry Sol's controls, so
// they used to stay behind in the browser window.
class DevToolsWindow : public std::enable_shared_from_this<DevToolsWindow> {
 public:
  // |ready| runs once the window and its header exist (the frontend can be created).
  DevToolsWindow(std::weak_ptr<State> state, int target, CefRect bounds, std::function<void()> ready)
      : state_(std::move(state)), target_(target), initial_(bounds), ready_callback_(std::move(ready)) {}
  void Open();
  bool ready() const { return ready_; }
  HWND hwnd() const;
  int target() const { return target_; }
  CefRefPtr<CefBrowser> header() const { return header_; }  // Probes.
  // Client-area rectangle below the header (physical pixels), for the frontend child.
  CefRect FrontendBounds() const;
  void Attach(CefRefPtr<CefBrowser> frontend);
  void FrontendClosed(int id);
  void Close();
  void Publish();
  // Window and header callbacks.
  void Created(CefRefPtr<CefWindow> window);
  void BoundsChanged(const CefRect& bounds);
  bool CanClose();
  void Destroyed();
  void HeaderCreated(CefRefPtr<CefBrowser> browser);
  void HeaderClosed(int id);
  bool Query(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, int64_t query, const CefString& request,
             bool persistent, CefRefPtr<CefMessageRouterBrowserSide::Callback> callback);
  void Cancel(int64_t query);
 private:
  void Layout();
  std::string Payload() const;
  std::weak_ptr<State> state_;
  int target_;
  CefRect initial_;
  std::function<void()> ready_callback_;
  CefRefPtr<CefWindow> window_;
  CefRefPtr<CefBrowser> header_, frontend_;
  CefRefPtr<CefMessageRouterBrowserSide::Callback> observer_;
  int64_t observer_id_ = -1;
  bool ready_ = false, closing_ = false, creating_ = false;
};
}  // namespace red
