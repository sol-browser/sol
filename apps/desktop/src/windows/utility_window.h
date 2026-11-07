#pragma once
#include "app/state.h"
#include "include/cef_client.h"
#include "include/views/cef_window_delegate.h"

namespace red {
// Settings and built-in managers belong to their opening browser. They remain
// resizable, but are never independent minimizable/maximizable browser windows.
CefRect UtilityWindowBounds(const CefRect& owner, const CefRect& work, const CefSize& preferred);
void PresentUtilityWindow(CefRefPtr<CefWindow> window, CefRefPtr<CefWindow> owner);

class UtilityWindowDelegate : public CefWindowDelegate {
 public:
  UtilityWindowDelegate(std::weak_ptr<State> state, CefSize size) : state_(std::move(state)), size_(size) {}
  CefSize GetPreferredSize(CefRefPtr<CefView>) override { return size_; }
  bool CanMinimize(CefRefPtr<CefWindow>) override { return false; }
  bool CanMaximize(CefRefPtr<CefWindow>) override { return false; }
  CefRefPtr<CefWindow> GetParentWindow(CefRefPtr<CefWindow>, bool* menu, bool* activate) override;
  CefRect GetInitialBounds(CefRefPtr<CefWindow>) override;
  void OnWindowCreated(CefRefPtr<CefWindow> window) override;
  void OnWindowClosing(CefRefPtr<CefWindow> window) override;
 protected:
  CefRefPtr<CefWindow> parent() const;
 private:
  std::weak_ptr<State> state_;
  CefSize size_;
};

class UtilityWindowClient : public CefClient, public CefCommandHandler, public CefKeyboardHandler {
 public:
  CefRefPtr<CefCommandHandler> GetCommandHandler() override { return this; }
  CefRefPtr<CefKeyboardHandler> GetKeyboardHandler() override { return this; }
  bool OnChromeCommand(CefRefPtr<CefBrowser>, int command, cef_window_open_disposition_t) override;
  bool IsChromeAppMenuItemVisible(CefRefPtr<CefBrowser>, int command) override;
  bool OnPreKeyEvent(CefRefPtr<CefBrowser>, const CefKeyEvent&, CefEventHandle, bool*) override;
};
}  // namespace red
