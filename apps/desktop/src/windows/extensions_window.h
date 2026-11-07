#pragma once
#include <memory>
#include "app/state.h"
#include "include/views/cef_window.h"
namespace red {
// "Manage Extensions": Chrome's own chrome://extensions page (enable, remove,
// details, site access, options) in a separate Chrome-style window of the web
// profile. It is the only place chrome:// is shown: navigation stays on
// chrome://extensions; web links open as workspace tabs and extension pages
// (options) through the same path as extension-opened tabs.
class ExtensionsWindow : public std::enable_shared_from_this<ExtensionsWindow> {
 public:
  explicit ExtensionsWindow(std::weak_ptr<State> state) : state_(std::move(state)) {}
  void Show();
  void Close();
  CefRefPtr<CefWindow> window() const { return window_; }  // Native window probes only.
  void Created(CefRefPtr<CefWindow> window) { window_ = window; }
  void Destroyed(CefRefPtr<CefWindow> window) { if (window_ && window_->IsSame(window)) window_ = nullptr; }
 private:
  std::weak_ptr<State> state_;
  CefRefPtr<CefWindow> window_;
};
}  // namespace red
