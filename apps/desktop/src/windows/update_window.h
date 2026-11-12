#pragma once
#include "app/state.h"
#include "include/views/cef_browser_view.h"
#include "include/wrapper/cef_message_router.h"
namespace red {
// "Check for Updates": a small window of Sol's own (a trusted UI document with its
// own IPC, like the Boost editor) that checks the release feed, shows the download
// as it happens and offers "Restart to update". The document polls update.state;
// the check itself runs in app/updates.cpp.
class UpdateWindow : public std::enable_shared_from_this<UpdateWindow> {
 public:
  explicit UpdateWindow(std::weak_ptr<State> state) : state_(std::move(state)) {}
  void Show();
  void Present();
  CefRefPtr<CefWindow> parent() const;
  void Created(CefRefPtr<CefWindow> window);
  void BrowserCreated(CefRefPtr<CefBrowser> browser);
  void BrowserClosed(int id);
  void Destroyed() { window_ = nullptr; ready_ = false; }
  void Close();
  CefRefPtr<CefWindow> window() const { return window_; }  // Native probes only.
  bool Query(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, const CefString& request,
             bool persistent, CefRefPtr<CefMessageRouterBrowserSide::Callback> callback);
 private:
  std::string Response() const;
  std::weak_ptr<State> state_;
  CefRefPtr<CefWindow> window_;
  CefRefPtr<CefBrowser> browser_;
  bool creating_ = false, ready_ = false;
};
void ShowUpdateWindow(const std::shared_ptr<State>& state);
}  // namespace red
