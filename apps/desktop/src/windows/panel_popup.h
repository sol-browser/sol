#pragma once
#include <string>
#include "app/state.h"
#include "include/views/cef_browser_view.h"
#include "include/wrapper/cef_message_router.h"
#include "windows/popup_presentation.h"
namespace red {
// Arc-style popover for the Archive (opens up from the sidebar's bottom bar)
// and Downloads (opens down from the top bar). Like the command bar it is an
// owned frameless Alloy window, because the shell cannot paint above native
// Chrome page windows. Its document only gets panel IPC: restore an archived
// tab, control downloads, resize and close itself.
class PanelPopup : public std::enable_shared_from_this<PanelPopup> {
 public:
  explicit PanelPopup(std::weak_ptr<State> state) : state_(std::move(state)) {}
  // |anchor| is the opening button in root client DIP coordinates.
  void Toggle(const std::string& kind, const CefRect& anchor);
  bool PreviewIntegration(const std::string& node, const CefRect& anchor, HWND surface);
  bool DismissIntegration();
  void Hide(bool restore_focus, bool animate = false);
  void Deactivated();
  void Shutdown();
  void Reposition();
  void Publish(CefRefPtr<CefDictionaryValue> theme = nullptr);
  void Ready();
  void Created(CefRefPtr<CefBrowser> browser);
  void Closed(int id);
  void Destroyed();
  bool Query(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, int64_t query, const CefString& request,
             bool persistent, CefRefPtr<CefMessageRouterBrowserSide::Callback> callback);
  void Cancel(int64_t query);
  void Attach(CefRefPtr<CefWindow> window) { window_ = window; presentation_.Attach(window, state_); Reposition(); }
  bool shutting_down() const { return shutdown_; }
  bool is_open() const { return open_ || presentation_.closing() || (hover_ && hover_->is_open()); }
  bool is_presented() const { return presented_; }
  CefRefPtr<CefWindow> parent() const;
  // The download menu closed; hide too if the person clicked outside.
  void MenuClosed();
  // "All Site Settings": switch the open site popover to its permissions view.
  void ShowSiteDetails();
  // A menu command runs before the menu reports closing; let it hide the popover.
  void MenuDone() { menu_open_ = false; }
  // A text field's context menu (UiContextMenu) holds the popover open the same way.
  void MenuOpened() { menu_open_ = true; }
  CefRefPtr<CefWindow> window() const { return window_; }
  // Probe-only fixed DOM driver: click the popover row containing |text|.
  void ProbeClick(const std::string& text, int attempts = 100);
 private:
  std::string Response(CefRefPtr<CefDictionaryValue> theme = nullptr) const;
  bool PointerOverOpener() const;
  bool PointerOverIntegration() const;
  bool IntegrationOwnerVisible() const;
  void FinishOpenerPress(int request);
  void DismissInactive(int request);
  void SiteMenu(int x, int y, CefRefPtr<CefMessageRouterBrowserSide::Callback> callback);
  void DownloadMenu(int id, int x, int y, CefRefPtr<CefMessageRouterBrowserSide::Callback> callback);
  std::weak_ptr<State> state_;
  CefRefPtr<CefWindow> window_;
  CefRefPtr<CefBrowser> browser_;
 public:
  CefRefPtr<CefBrowser> browser() const { return browser_; }  // Probes only.
  const PanelPopup* hover_panel() const { return hover_.get(); }  // Probes only.
 private:
  PanelPopup(std::weak_ptr<State> state, bool hover_only) : state_(std::move(state)), hover_only_(hover_only) {}
  // The passive hover widget and focusable editors cannot share one CEF window:
  // menu activation policy is fixed at creation.
  std::shared_ptr<PanelPopup> hover_;
  const bool hover_only_ = false;
  CefRefPtr<CefMessageRouterBrowserSide::Callback> observer_;
  int64_t observer_id_ = -1;
  std::string kind_ = "downloads";
  CefRect anchor_;
  float origin_x_ = .5f, integration_origin_y_ = 0.f;
  PopupPresentation presentation_;
  int height_ = 120, request_ = 0, site_details_ = 0;
  unsigned long long opened_ms_ = 0;
  bool open_ = false, ready_ = false, shutdown_ = false, menu_open_ = false;
  // The new-space editor previews its theme on the window without saving; it
  // is dropped when the editor closes without creating the space.
  bool draft_theme_ = false;
  void DropDraftTheme();
  void WatchIntegration(int request);
  bool PreviewOpeningValid(const std::shared_ptr<State>& state) const;
  void SyncIntegrationPreview();
  void OpenCachedPreview(std::string token, bool compose, CefRefPtr<CefMessageRouterBrowserSide::Callback> cb);
  void OpenPreviewInPin(const std::string& url, std::string token, bool compose,
                        CefRefPtr<CefMessageRouterBrowserSide::Callback> cb, bool refresh_mail = false);
  HWND integration_surface_ = nullptr;
  CefRefPtr<CefDictionaryValue> integration_;
  std::string integration_source_;
  std::weak_ptr<BrowsingProfile> integration_profile_;
  bool integration_cached_ = false, integration_favorite_ = false;
  int integration_cache_revision_ = -1;
  std::shared_ptr<int> pending_integration_action_;
  unsigned long long integration_left_ = 0;
  bool prepared_ = false, presented_ = false;
};
}  // namespace red
