#pragma once
#include "app/state.h"
#include "include/views/cef_browser_view.h"
#include "include/wrapper/cef_message_router.h"
#include "windows/popup_presentation.h"
namespace red {
class SearchSuggestions;
// An owned Alloy window: React cannot paint above native Chrome child HWNDs.
// Only its exact main document gets the small command-bar IPC capability.
class CommandBar : public std::enable_shared_from_this<CommandBar> {
 public:
  explicit CommandBar(std::weak_ptr<State> state) : state_(std::move(state)) {}
  ~CommandBar();
  void Open(bool new_tab);
  // Little Sol launcher (Ctrl+Alt+N): an unowned bar centred on the display under the cursor. What
  // it opens goes into its hidden Little Sol, which then shows; dismissing it closes that window.
  void OpenDetached();
  void Hide(bool restore_focus);
  void Shutdown();
  void Reposition();
  void Deactivated();
  // A text field's context menu takes activation; the bar stays open until it closes.
  void MenuOpened() { menu_open_ = true; }
  void MenuClosed();
  CefRefPtr<CefWindow> window() const { return window_; }
  void Refresh() { if (open_) Publish(); }
  void Ready();
  void Created(CefRefPtr<CefBrowser> browser);
  void Closed(int id);
  void Destroyed();
  bool Query(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, int64_t query,
      const CefString& request, bool persistent, CefRefPtr<CefMessageRouterBrowserSide::Callback> callback);
  void Cancel(int64_t query);
  // Fixed DOM driver, reachable only from the explicit organization/browsing probes.
  void ProbeInput(const std::string& input, int attempts = 100, bool actions_checked = false);
  void ProbeCancel();
  // --probe-little-window=launcher only: submits |url| in the detached bar, or closes it when empty.
  void ProbeLauncher(const std::shared_ptr<State>& prober, const std::string& url);
  bool detached() const { return detached_; }
  bool shutting_down() const { return shutdown_; }
  // Drops a closed bar page from its window (also for a bar that was already replaced).
  static void Forget(const std::shared_ptr<State>& state, int id);
  bool is_open() const { return open_; }
  bool is_presented() const { return presented_; }
  CefRefPtr<CefWindow> parent() const;
  void Attach(CefRefPtr<CefWindow> window, CefRefPtr<CefBrowserView> view);
 private:
  std::string Response() const;
  void Publish();
  void RestoreAfterMove();
  void BeginOwnerGesture();
  void FinishOwnerGesture();
  void TrackOwnerGesture();
  void FollowOwnerGesture(int request);
  static LRESULT CALLBACK OwnerMessage(HWND window, UINT message, WPARAM wparam, LPARAM lparam, UINT_PTR id, DWORD_PTR data);
  std::weak_ptr<State> state_;
  CefRefPtr<CefWindow> window_;
  CefRefPtr<CefBrowserView> view_;
  CefRefPtr<CefBrowser> browser_;
  CefRefPtr<CefMessageRouterBrowserSide::Callback> observer_;
  int64_t observer_id_ = -1;
  int request_ = 0;
  int height_ = 120;
  unsigned long long opened_ms_ = 0;
  PopupPresentation presentation_;
  bool prepared_ = false, presented_ = false;
  bool menu_open_ = false;
  bool site_probe_started_ = false;
  bool new_tab_ = true, open_ = false, ready_ = false, shutdown_ = false, submitting_ = false;
  std::string source_id_, source_url_;
  bool detached_ = false, launcher_keep_ = false;
  CefRect detached_area_;
  HWND owner_handle_ = nullptr;
  bool owner_moving_ = false;
  bool gesture_pending_ = false, gesture_dragged_ = false;
  RECT gesture_bounds_{};
  POINT gesture_cursor_{};
  std::shared_ptr<SearchSuggestions> suggestions_;
  int64_t suggestion_query_ = -1;
};
}  // namespace red
