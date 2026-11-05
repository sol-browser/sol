#pragma once
#include <memory>
#include <string>
#include "app/state.h"
#include "include/views/cef_browser_view.h"
#include "include/wrapper/cef_message_router.h"

namespace red {
class BrowserImport;
// Sol's first run (docs/sol-plan.md, stage 6): a window of its own before the
// browser. Its page (sol://ui/welcome.html) plays the intro, offers to import
// from another browser and to make Sol the default, then finishes: the welcome
// closes and the browser window, created hidden meanwhile, appears. Closing the
// welcome any other way finishes it too.
class WelcomeWindow : public std::enable_shared_from_this<WelcomeWindow> {
 public:
  explicit WelcomeWindow(std::weak_ptr<State> state) : state_(std::move(state)) {}
  void Show();
  void Created(CefRefPtr<CefWindow> window) { window_ = window; }
  CefRefPtr<CefWindow> window() const { return window_; }
  void BrowserCreated(CefRefPtr<CefBrowser> browser);
  void BrowserClosed(int id);
  void Destroyed();
  // Closes the welcome (once) and shows the browser window.
  void Finish();
  bool Query(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, const CefString& request,
             bool persistent, CefRefPtr<CefMessageRouterBrowserSide::Callback> callback);
 private:
  std::string Response(bool refresh_default) const;
  std::weak_ptr<State> state_;
  CefRefPtr<CefWindow> window_;
  CefRefPtr<CefBrowser> browser_;
  std::shared_ptr<BrowserImport> import_;
  mutable CefRefPtr<CefDictionaryValue> default_status_;
  bool finished_ = false;
};
// Shows the welcome for |state|'s window, which stays hidden until it finishes.
void ShowWelcome(const std::shared_ptr<State>& state);
}  // namespace red
