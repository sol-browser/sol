#pragma once
#include <array>
#include <mutex>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include "app/state.h"
#include "include/views/cef_browser_view.h"
#include "include/wrapper/cef_message_router.h"
namespace red {
// The still picture for a capture window's own document (path "/capture-frame-<generation>.png" asked by
// that window's browser); null otherwise. Any thread: the UI scheme handler runs on the IO thread.
std::shared_ptr<const std::string> CaptureFrame(int browser, const std::string& path);

// "Capture" as Chrome's Lens overlay does it: a still picture of the page in front, shown in a window of
// Sol's own (sol://ui/capture.html, CaptureOverlay.tsx) laid exactly over the page area. The person
// hovers elements, drags or adjusts an area, and copies or downloads it there. Nothing is put into the
// website: the picture is a surface copy (Page.captureScreenshot without a clip), and the elements' boxes
// for hovering are read once with DOMSnapshot.captureSnapshot. Once the window covers the page, the whole
// page is taken as one picture (Chrome's clipped capture resizes the page for a moment, docs/page-transitions.md,
// hidden under this window), twice so lazily loaded images are in it; the window then scrolls that picture
// by itself, smoothly, and regions, the visible part and the whole page are cut from it. If the page is too
// large or that picture fails, the wheel scrolls the page underneath and a new picture of the view follows.
class CaptureWindow : public std::enable_shared_from_this<CaptureWindow> {
 public:
  explicit CaptureWindow(std::weak_ptr<State> state) : state_(std::move(state)) {}
  void Start();
  void Close();
  CefRefPtr<CefWindow> parent() const;
  CefRefPtr<CefBrowser> browser() const { return browser_; }  // Probes only.
  bool is_open() const { return window_ != nullptr || creating_; }
  void Created(CefRefPtr<CefWindow> window);
  void BrowserCreated(CefRefPtr<CefBrowser> browser);
  void BrowserClosed(int id);
  void Destroyed();
  void ActivationChanged(bool active);
  bool Query(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, const CefString& request, bool persistent,
             CefRefPtr<CefMessageRouterBrowserSide::Callback> callback);

  // One still picture of the page and what was under it (filled off the UI thread, then handed over).
  struct Shot {
    std::string png;       // The picture: the view, or the whole page.
    std::string view_png;  // The view as it was on screen (fixed elements where they were).
    double view_x = 0, view_y = 0, view_width = 0, view_height = 0;  // CSS pixels: the viewport in the document.
    double page_width = 0, page_height = 0;                          // CSS pixels: the document's size.
    double pic_x = 0, pic_y = 0, pic_width = 0, pic_height = 0;      // CSS pixels: the part of the document in |png|.
    bool full = false;                                               // |png| is the whole page.
    std::vector<std::array<int, 4>> boxes;                           // Element boxes, document coordinates.
  };
  void Snapped(unsigned request, std::shared_ptr<Shot> shot);
  void Done();  // The picture is delivered: the window closes.
  void FullSnapped(unsigned request, std::optional<std::string> png, double width, double height);

 private:
  void Snap();
  void Show();
  std::string Response() const;
  void Finish(CefRefPtr<CefDictionaryValue> data, CefRefPtr<CefMessageRouterBrowserSide::Callback> callback);
  void StartFull();
  bool FullComing() const { return full_passes_ < kFullPasses && !full_failed_; }
  static constexpr unsigned kFullPasses = 2;
  std::weak_ptr<State> state_;
  CefRefPtr<CefWindow> window_;
  CefRefPtr<CefBrowser> browser_, page_;
  CefRefPtr<CefMessageRouterBrowserSide::Callback> pending_scroll_, pending_full_;
  unsigned full_request_ = 0, full_passes_ = 0, full_after_ = 0;
  unsigned long long full_started_ = 0;
  bool full_failed_ = false, full_running_ = false, closing_after_full_ = false;
  std::shared_ptr<Shot> shot_;
  unsigned generation_ = 0, request_ = 0, reclaimed_ = 0;
  bool creating_ = false, ready_ = false, finishing_ = false, activated_ = false, snapping_ = false;
};
}  // namespace red
