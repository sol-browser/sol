#pragma once
#include "app/state.h"
#include "include/views/cef_window.h"

namespace red {
// A dedicated Chrome Runtime WebUI, never a privileged Sol document. Each
// window retains the opening profile even if its Space subsequently changes.
class PasswordsWindow : public std::enable_shared_from_this<PasswordsWindow> {
 public:
  PasswordsWindow(std::weak_ptr<State> state, CefRefPtr<CefRequestContext> context, std::string title)
      : state_(std::move(state)), context_(context), title_(std::move(title)) {}
  void Show(bool settings);
  void Close();
  CefRefPtr<CefWindow> window() const { return window_; }  // Native window probes only.
  void Created(CefRefPtr<CefWindow> window);
  void Destroyed();
  void UpdateTitle(const std::string& title);
  void BrowserCreated(CefRefPtr<CefBrowser> browser);
  void BrowserClosed(int id);
  const std::string& title() const { return title_; }
  CefRefPtr<CefBrowser> browser() const { return browser_; }  // Native synthetic probes only.
 private:
  std::weak_ptr<State> state_;
  CefRefPtr<CefRequestContext> context_;
  CefRefPtr<CefWindow> window_;
  CefRefPtr<CefBrowser> browser_;
  std::string title_;
  bool creating_ = false, closing_ = false;
};
// Only registered persistent profiles may be opened. Private windows refuse.
bool ShowPasswords(const std::shared_ptr<State>& state, const std::string& profile, bool settings = false);
}  // namespace red
