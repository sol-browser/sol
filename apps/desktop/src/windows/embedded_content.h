#pragma once
#include <functional>
#include <map>
#include <memory>
#include "app/state.h"
#include "windows/page_corners.h"
#include "windows/popup_animation_host.h"

namespace red {
// Each page is created with public CefWindowInfo::SetAsChild. Never reparent a
// Chromium HWND or detach/reinsert an existing CEF BrowserView.
class EmbeddedContent {
 public:
  EmbeddedContent(std::weak_ptr<State> state, std::function<CefRefPtr<CefClient>(const std::string&, bool)> client_factory);
  ~EmbeddedContent();
  bool Create(const std::string& url, const std::string& node_id = "");
  void OnCreated(CefRefPtr<CefBrowser> browser);
  void OnClosed(int id);
  void Resize(const CefRect& bounds);
  // Child HWNDs move with the root without changing their local bounds. Tell
  // Chrome so its screen-space anchors (e.g. Save password) follow that move.
  void NotifyWindowMoved();
  void Relayout();  // Dock placement changed without changing the workspace.
  // Repaints the page corners after the workspace background changed (theme).
  void RefreshCorners() { UpdateCorners(); }
  void RefreshTheme();
  void ShowPendingColdTab();
  void CancelPendingColdTab();
  bool HasPendingColdTab() const { return presenting_id_ == -1 && cold_cover_; }
  // A page is being revealed (input to it is held until its first frame).
  bool Presenting() const { return presenting_id_ != -1 || cold_cover_; }
  void Activate(CefRefPtr<CefBrowser> browser, bool cold = false);
  void OnPresentationLoadFinished(CefRefPtr<CefBrowser> browser);
  void OnPresentationLoading(CefRefPtr<CefBrowser> browser, bool loading);
  void SetSplitPages(int first, int second, bool vertical, int ratio, bool reversed, int expanded = -1, const std::set<int>& crashed = {});
  bool IsSplitPage(int id) const { return split_enabled_ && (id == split_first_ || id == split_second_); }
  bool HasSplit() const { return split_enabled_; }
  bool HasLinkPeek() const { return link_peek_id_!=-1; }
  bool IsLinkPeek(int id) const { return id==link_peek_id_ && id!=-1; }
  // -2: creating, real browser ID: presented. |resume|: a parked preview
  // returns with its tab, already painted: no glow, no entry, shown at once.
  void SetLinkPeek(int id,bool resume=false);
  void SetLinkPeekOrigin(POINT origin) { link_peek_origin_=origin; }
  POINT LinkPeekOrigin() const { return link_peek_origin_; }
  bool LinkPeekComposed() const { return corners_.LinkPeekComposed(); }
  bool LinkPeekOpening() const { return corners_.LinkPeekOpening(); }
  bool LinkPeekDepthReady() const { return corners_.LinkPeekDepthReady(); }
  int LinkPeekReturnDelay() const { return link_peek_return_delay_; }
  int ReturnPendingLinkPeek();  // Cancel the neutral card before its browser exists.
  void PrepareLinkPeekClose(std::function<void()> ready);
  void DiscardLinkPeekClose();
  void LinkPeekPageReady(CefRefPtr<CefBrowser> browser);
  void OnLinkPeekEvent(CefRefPtr<CefBrowser> browser,const std::string& method,const std::string& json);
  void HoverLinkPeek(int index) { corners_.HoverLinkPeek(index); }
  void ClearLinkPeek(bool promote=false);
  CefRect LinkPeekCard() const;
  CefRect LinkPeekViewport() const;
  CefRect SplitCell(bool second) const;
  COLORREF SplitBorder(bool second) const;
  CefRect SplitDropArea() const { return browser_fullscreen_ ? FullClientBounds() : WorkspacePhysicalBounds(); }
  void PresentCrash(CefRefPtr<CefBrowser> browser);
  void HideAll();
  void RaiseActive();
  void SetLinkStatus(CefRefPtr<CefBrowser> browser, const CefString& value);
  void HideLinkStatus();
  void QueueRaise();
  bool AllContained() const;
  bool HasSingleVisibleActive() const;
  // A Peek intentionally keeps its source(s) visible behind the active card.
  bool VisiblePagesMatchPresentation() const;
  bool ActiveOwnsInputArea() const;
  bool ActiveMatchesBounds() const;
  // Page (JavaScript Fullscreen API) fullscreen: the root window becomes
  // fullscreen and that page covers the whole client area, over our shell.
  void SetFullscreen(CefRefPtr<CefBrowser> browser, bool fullscreen);
  // F11 belongs to the top-level Red window, never Chrome's child widget.
  void ToggleBrowserFullscreen();
  bool ExitBrowserFullscreen();  // Escape, unless the HTML Fullscreen API owns it.
  bool IsFullscreen() const { return browser_fullscreen_ || fullscreen_id_ != -1; }
  bool IsBrowserFullscreen() const { return browser_fullscreen_; }
  bool IsPageFullscreen() const { return fullscreen_id_ != -1; }
  void SetSidebarOverlayVisible(bool visible);  // Keep page captures below the inline UI.
  bool IsFullscreenPage(int id) const { return fullscreen_id_ == id; }
  bool FullscreenGeometryOk() const;
  // Red's one toast style (docs/toasts.md): a compact single line in the
  // top-right corner that lands with a small bounce and leaves after 2.5 s.
  // |keys| are drawn as key caps after |text|, then |tail| ("Press [Esc] to
  // exit full screen"). Each call restarts the entry and resets the timeout.
  // |page|: the browser it is about (a zoomed split pane), else the active page.
  void Notify(const std::string& kind, const std::wstring& text, const std::vector<std::wstring>& keys = {},
              const std::wstring& tail = L"", int duration_ms = 2500, int page = -1);
  void HideNotification();
  bool NotificationVisible() const { return corners_.notification_visible(); }
  bool NotificationGeometryOk() const;
  bool BeginPopupAnimation(HWND popup, bool bottom, float origin_x,
                           bool closing = false, float from_scale = 1.f, float origin_y = -1.f);
  bool FadePopupShadow(HWND popup, int duration_ms);
  void EndPopupAnimation(HWND popup);
  void ConfigurePopupOwner(HWND popup, bool allow_sidebar);
  void RestorePopupOwners(HWND sidebar);
  bool ShowSidebarShadow(const RECT& panel, float scale, double progress);
  void HideSidebarShadow() { corners_.HideSidebarShadow(); }
  void OnPresentationResult(CefRefPtr<CefBrowser> browser, int message, bool success, const void* data, size_t size);
  void OnPresentationEvent(CefRefPtr<CefBrowser> browser, const std::string& method, const std::string& json);
 private:
  // The F11 sidebar while it is a child of the root (pages stack below it), else null.
  HWND InlineSidebar() const;
  void ObservePresentation(CefRefPtr<CefBrowser> browser);
  // Cold page: the outgoing page goes at once and the page area shows what an
  // empty space shows (the UI's .web-slot on the window material) until the new
  // page has painted parked outside the window.
  void ShowColdCover();
  void BlockPresentationInput(HWND handle);
  void BeginPresentation(CefRefPtr<CefBrowser> browser, bool cold);
  void PresentationContentReady(CefRefPtr<CefBrowser> browser, const char* source, bool load_finished);
  void AwaitIncomingFrame(CefRefPtr<CefBrowser> browser);
  void RetryIncomingFrame(CefRefPtr<CefBrowser> browser);
  void ConfirmPresentationFrame(int id);
  void RevealPresentationAfterFrame();
  void CheckPresentationTimeout(unsigned long long generation);
  void FinishPresentation(unsigned long long generation, bool focus = true, bool commit = true, bool keep_gradient = false);
  struct Presentation {
    CefRefPtr<CefRegistration> registration;
    int incoming_capture = 0, frame_attempts = 0, frame_query = 0;
    bool waiting_for_content = false, watching_paint = false, saw_loading = false;
    bool load_ended = false;
    std::string main_frame, loader;
    unsigned long long activated_ms = 0;
  };
  std::map<int, Presentation> presentations_;
  int presenting_id_ = -1;
  int presentation_backdrop_id_ = -1;
  HWND presentation_input_ = nullptr;
  std::set<HWND> presentation_disabled_;
  unsigned long long presentation_generation_ = 0;
  bool cold_presentation_ = false;
  bool cold_cover_ = false;
  CefRect FullClientBounds() const;
  void ExitFullscreenFor(int id);
  void ApplyFullscreenWindow(bool was_fullscreen);
  // Windows 11 draws a 1px border and rounds our window's corners; neither
  // belongs on a fullscreen page.
  void SetWindowFrameForFullscreen(bool fullscreen) const;
  CefRect PhysicalBounds() const;
  CefRect WorkspacePhysicalBounds() const;
  CefRect PagePhysicalBounds(int id) const;
  CefRect LayoutArea(int id) const;
  void LayoutSplit();
  void Shape(HWND handle) const;
  void LayoutLinkPeek();
  void ShapeLinkPeek(HWND handle) const;
  bool OnLinkPeekCapture(CefRefPtr<CefBrowser> browser,int message,bool success,const void* data,size_t size);
  void ObserveLinkPeekReadiness(CefRefPtr<CefBrowser> browser);
  void CaptureLinkPeekSources();
  // Starts the scene once the source frames are in (or their budget ran out).
  void StartLinkPeekScene();
  bool OnLinkPeekReadiness(CefRefPtr<CefBrowser> browser,int message,bool success,const void* data,size_t size);
  // Places the antialiased corner overlays over the visible page (or hides them).
  void UpdateCorners();
  void UpdateLinkStatus();
  std::weak_ptr<State> state_;
  // (node ID, history page) -> client.
  std::function<CefRefPtr<CefClient>(const std::string&, bool)> client_factory_;
  std::map<int, CefRefPtr<CefBrowser>> browsers_;
  HWINEVENTHOOK stacking_hook_ = nullptr;
  bool raise_pending_ = false;
  int crash_presented_ = -1;
  int fullscreen_id_ = -1;
  bool browser_fullscreen_ = false;
  bool window_was_fullscreen_ = false;
  CefRect bounds_;
  bool sidebar_overlay_visible_ = false;
  bool split_enabled_ = false, split_vertical_ = false, split_reversed_ = false;
  int split_first_ = -1, split_second_ = -1, split_ratio_ = 5000, split_expanded_ = -1;
  std::set<int> split_crashed_;
  PageCorners corners_;
  bool link_status_available_ = false;
  int link_status_browser_ = -1;
  std::wstring link_status_text_;
  int link_peek_id_=-1, link_peek_base_=-1;
  POINT link_peek_origin_{};
  bool link_peek_composition_failed_=false,link_peek_scene_started_=false;
  bool link_peek_page_ready_=false,link_peek_geometry_traced_=false;
  int link_peek_frame_query_=0,link_peek_paint_query_=0;
  std::string link_peek_frame_,link_peek_loader_;
  std::map<int,int> link_peek_source_captures_;
  // Freeze first: the scene waits for the source frames, kept here until then.
  bool link_peek_waiting_=false;
  std::vector<std::pair<HWND,std::vector<char>>> link_peek_source_frames_;
  int link_peek_return_delay_=0,link_peek_close_capture_=0;
  std::function<void()> link_peek_capture_ready_;
  unsigned long long link_peek_capture_generation_=0;
  // Probe timing: when the source/closing screenshots were requested.
  unsigned long long link_peek_sources_requested_=0,link_peek_close_requested_=0;
  unsigned long long link_peek_generation_=0;
  // Live sources (engine patch 11, docs/link-peek.md): the source pages recede
  // and dim in place (compositor), their window regions follow the same curve
  // so the workspace shows at the edges. Off: frozen source pictures.
  bool link_peek_live_=false;
  float live_scale_from_=1,live_scale_to_=1,live_dim_=0;
  unsigned long long live_started_=0;
  int live_ms_=0;
  std::vector<CefRefPtr<CefBrowser>> LinkPeekSourceBrowsers() const;
  void AnimateLinkPeekSources(float scale,float dim,int ms);
  float LinkPeekSourceScale() const;
  bool LinkPeekSourcesAnimating() const;
  void UpdateSplitCorners();
  PopupAnimationHost popup_animation_host_;
  unsigned long long notification_generation_ = 0;
  int notification_page_ = -1;
  // Panes of a split being left: kept in place until the next page has painted.
  std::set<int> split_leaving_;
  bool parked_ = false;  // The page being shown paints outside the window first.
  // Where the toast sits: the whole window in full screen, else its page.
  CefRect NotificationArea() const;
  bool corners_recorded_ = false;  // Probe evidence, once.
  bool corners_composited_ = true;  // False: fall back to a (stair-stepped) window region.
};
}  // namespace red
