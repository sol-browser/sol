#pragma once
#include <functional>
#include <memory>
#include <string>
#include "app/state.h"
#include "include/views/cef_browser_view.h"
#include "include/views/cef_window.h"

namespace red {
// Experiment B: one Chrome-style BrowserView with Chrome's own toolbar
// (GetChromeToolbarType) beside the Alloy React shell in a Chrome-style
// window, and "tab" switching by swapping the single Chrome BrowserView.
// Observes only; no engine patch, reparenting or fabricated extension state.
class ToolbarProbe {
 public:
  // |manual|: no timed view swaps or close; a person drives the toolbar.
  ToolbarProbe(std::weak_ptr<State> state, cef_chrome_toolbar_type_t type, bool manual,
               std::function<CefRefPtr<CefClient>()> client_factory);
  CefRefPtr<CefBrowserView> CreateView(const std::string& url);
  // Window assembled; schedules swap A -> B -> A and the final close.
  void Start(CefRefPtr<CefWindow> window, CefRefPtr<CefBrowserView> first, const std::string& second_url);
  void OnToolbarAvailable(CefRefPtr<CefBrowserView> view);
  void OnViewBrowserDestroyed(int browser_id);
  bool Finished() const { return finished_; }
 private:
  void Swap(CefRefPtr<CefBrowserView> out, CefRefPtr<CefBrowserView> in, const char* phase);
  void Describe(const char* phase);
  std::weak_ptr<State> state_;
  cef_chrome_toolbar_type_t type_;
  bool manual_;
  std::function<CefRefPtr<CefClient>()> client_factory_;
  CefRefPtr<CefWindow> window_;
  CefRefPtr<CefBrowserView> first_, second_;
  CefRefPtr<CefView> toolbar_;
  bool finished_ = false;
};
}  // namespace red
