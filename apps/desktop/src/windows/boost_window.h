#pragma once
#include "app/state.h"
#include "include/views/cef_browser_view.h"
#include "include/wrapper/cef_message_router.h"
#include "site/boost_store.h"
namespace red {
// Boost editor (docs/boosts.md): a small movable window owned by the browser window, like
// Settings a trusted UI document with its own IPC. It edits the Boost of the host it was
// opened for; reopening it from another site retargets it. Zap and Focus pick elements on
// the page it was opened from.
class BoostWindow : public std::enable_shared_from_this<BoostWindow> {
 public:
  explicit BoostWindow(std::weak_ptr<State> state) : state_(std::move(state)) {}
  void Show(CefRefPtr<CefBrowser> page);
  void Present();
  CefRefPtr<CefWindow> parent() const;
  void Created(CefRefPtr<CefWindow> window);
  void BrowserCreated(CefRefPtr<CefBrowser> browser);
  void BrowserClosed(int id);
  void Destroyed() { window_ = nullptr; ready_ = false; }
  void Close();
  bool Query(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, const CefString& request,
             bool persistent, CefRefPtr<CefMessageRouterBrowserSide::Callback> callback);
  const std::string& host() const { return key_; }
 private:
  std::string Response() const;
  void StopPick();
  void Picked(CefRefPtr<CefDictionaryValue> pick);
  void Resolve();
  int Save(Boost boost, std::string& error);
  std::weak_ptr<State> state_;
  CefRefPtr<CefWindow> window_;
  CefRefPtr<CefBrowser> browser_, page_;
  std::string key_, picking_;
  CefRefPtr<CefMessageRouterBrowserSide::Callback> pending_pick_;
  bool creating_ = false, ready_ = false;
};
void ShowBoostEditor(const std::shared_ptr<State>& state, CefRefPtr<CefBrowser> page);
}  // namespace red
