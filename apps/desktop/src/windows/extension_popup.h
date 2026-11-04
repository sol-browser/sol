#pragma once
#include <memory>
#include <string>
#include "app/state.h"
#include "include/views/cef_browser_view.h"
#include "include/views/cef_window.h"
namespace red {
// An extension's toolbar popup, Arc-style: a small owned frameless window
// (no caption buttons or icon) next to the site popover. It is an Alloy-style
// browser in the web profile: experiments showed that Chrome then resolves the
// popup's chrome.tabs.query({active, currentWindow}) to the visible workspace
// page, while a Chrome-style host resolves to the popup itself. The window
// follows the document's size (like Chrome's 25..800 x 25..600 popup) and
// closes when it loses activation, on Escape or on window.close().
// Opens an installed extension as a click on its toolbar button would: its action (with the
// visible page's per-tab grants), its popup with the top-right corner at |anchor| (screen DIP),
// or its options page. Nothing for an unknown id.
void RunExtension(const std::shared_ptr<State>& state, const std::string& id, const CefPoint& anchor);
class ExtensionPopup : public std::enable_shared_from_this<ExtensionPopup> {
 public:
  explicit ExtensionPopup(std::weak_ptr<State> state) : state_(std::move(state)) {}
  // |anchor|: top-right corner of the popup, in screen DIP coordinates.
  void Open(const std::string& id, const std::string& path, const CefPoint& anchor);
  // |which|: close only if that window is still the current popup.
  void Close(CefRefPtr<CefWindow> which = nullptr);
  bool is_open() const { return window_ != nullptr; }
  const std::string& id() const { return id_; }
  // Callbacks from the popup's browser and window.
  void Created(CefRefPtr<CefWindow> window);
  void Resize(const CefSize& size);
  void Destroyed(CefRefPtr<CefWindow> window);
  bool Owns(int browser_id) const { return browser_id_ == browser_id; }
  void SetBrowser(int browser_id) { browser_id_ = browser_id; }
 private:
  void Place();
  std::weak_ptr<State> state_;
  CefRefPtr<CefWindow> window_;
  std::string id_;
  CefPoint anchor_;
  CefSize size_{320, 120};
  int browser_id_ = -1;
};
}  // namespace red
