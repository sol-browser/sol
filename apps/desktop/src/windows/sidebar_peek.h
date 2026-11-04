#pragma once
#include <memory>
#include <windows.h>
#include "app/state.h"
#include "include/cef_browser.h"
#include "include/wrapper/cef_message_router.h"
namespace red {
// Floating browser chrome backed by a trusted Chrome Runtime child browser.
// Normal mode uses a small owned transparent host which clips its horizontal
// slide. In F11 the same wrapper becomes a child of the main window, so clicks
// keep that fullscreen window activated. A translating child frame carries
// the browser; the existing root compositor paints only its outside shadow.
// The browser's immediate parent stays that frame in both modes.
// Chrome UI and site HWNDs use ordinary sibling z-order (UI first), with no
// cutout, resize or snapshot of the website while the sidebar width changes.
// Parked hosts remain shown off screen to avoid cold-show flashes.
//
// Its document (sol://ui/peek.html, trusted UI profile) gets only sidebar IPC:
// snapshots, tree commands, new tab, tab duplicate/mute, the row menu,
// Archive and dropped files. Edge and leave detection poll the pointer
// natively, so the page keeps every click at its left edge.
class SidebarPeek : public std::enable_shared_from_this<SidebarPeek> {
 public:
  explicit SidebarPeek(std::weak_ptr<State> state) : state_(std::move(state)) {}
  // Starts or stops pointer watching (docked sidebar hidden / shown).
  void SetEnabled(bool enabled);
  void Show();
  void Hide();
  void EndResize(bool cancel);
  // Creates the (hidden) window early so the first slide has content.
  void Preload();
  void Shutdown();
  bool is_open() const { return open_; }
  bool is_resizing() const { return resizing_; }
  bool shutting_down() const { return shutdown_; }
  bool window_visible() const { return host_ && !parked_ && IsWindowVisible(host_); }
  HWND host() const { return host_; }
  CefRefPtr<CefBrowser> browser() const { return browser_; }
  void Publish(CefRefPtr<CefDictionaryValue> theme = nullptr);
  // Browser/window callbacks.
  void Created(CefRefPtr<CefBrowser> browser);
  void Closed(int id);
  bool Query(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, int64_t query, const CefString& request,
             bool persistent, CefRefPtr<CefMessageRouterBrowserSide::Callback> callback);
  void Cancel(int64_t query);
  void PreparedFrame(int message, bool success, size_t size);
  // Paths of files dragged over the peek (from the OS drag, never the page).
  void DragEntered(std::vector<std::string> files) { drop_files_ = std::move(files); }
 private:
  void Watch(unsigned generation);
  void ConfigureHost();
  bool Ensure();
  void Place(double progress);
  void Slide(bool in, bool activate);
  void Present();
  void PrepareFrame();
  void PlaceHost();
  void PlaceHost(double progress);
  void SizeFrame(int width, int height, float scale);
  void Park();
  static double Now();
  void Animate(unsigned generation, double started, bool in);
  // Paces a slide by the display: a worker wakes once per composed frame
  // (DwmFlush, i.e. every vsync at any refresh rate) and asks the UI thread to
  // place the slide for that moment. Timers (~15.6 ms) would skip frames.
  struct Pacer;
  void StartPacer(unsigned generation, double started, bool in);
  std::shared_ptr<Pacer> pacer_;
  unsigned pacer_generation_ = 0;
  CefRect Target() const;
  std::weak_ptr<State> state_;
  HWND host_ = nullptr;  // Owned popup normally; root child in F11.
  HWND frame_ = nullptr;  // Sliding child containing the browser.
  HWND hwnd_ = nullptr;  // The sidebar browser, a child of frame_.
  bool shadow_recorded_ = false;
  bool inline_host_ = false;
  bool motion_done_ = true, parked_ = true;
  double shown_ = 0;  // Visible fraction of the sidebar (0..1).
  CefRefPtr<CefBrowser> browser_;
  CefRefPtr<CefRegistration> frame_observer_;
  CefRefPtr<CefMessageRouterBrowserSide::Callback> observer_;
  int64_t observer_id_ = -1;
  std::vector<std::string> drop_files_;
  unsigned watch_ = 0, motion_ = 0;
  int hover_ = 0, away_ = 0;
  bool enabled_ = false, open_ = false, shutdown_ = false;
  bool resizing_ = false;
  bool pointer_sample_valid_ = false;
  POINT previous_cursor_{};
  RECT previous_trigger_{};
  DWORD previous_pointer_check_ = 0;
  int poll_ms_ = 15;  // Next pointer poll interval (Watch).
  double opening_hold_until_ = 0;
  int resize_start_x_ = 0, resize_start_width_ = 0;
  bool probe_clip_recorded_ = false, creating_ = false, pending_show_ = false;
  unsigned prepare_generation_ = 0;
  int prepare_revision_ = 0, frame_request_ = 0;
  double retry_after_ = 0;
  float corner_scale_ = 0;
  COLORREF probe_outside_ = 0, probe_edge_ = 0;
};
}  // namespace red
