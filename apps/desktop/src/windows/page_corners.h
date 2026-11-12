#pragma once
#include <windows.h>
#include <d2d1_1.h>
#include <d3d11.h>
#include <dcomp.h>
#include <wrl/client.h>
#include <array>
#include <map>
#include <span>
#include <string>
#include <vector>
#include "app/theme.h"
#include "windows/sidebar_shadow.h"
#include "windows/link_peek_scene.h"
namespace red {
// Antialiased rounded corners for the page area.
//
// Chromium presents page content through DirectComposition, which Windows
// composes above ordinary (even layered) sibling child windows, and a window
// region clips without antialiasing (stair-stepped corners). So the corners are
// a DirectComposition visual tree on the root window created with
// topmost = TRUE: it is composed above all child windows, moves with the window
// and takes no input. Each corner is a small surface filled by Direct2D with
// the shell background outside a quarter circle (Direct2D antialiasing).
class PageCorners {
 public:
  PageCorners() = default;
  PageCorners(const PageCorners&) = delete;
  PageCorners& operator=(const PageCorners&) = delete;
  // |rect|: page rectangle in |parent| client pixels; |radius| in pixels.
  // Returns false when composition is unavailable (caller falls back to a
  // window region).
  bool Show(HWND parent, const RECT& rect, int radius);
  // One shared root composition target, up to four independently placed frames (Split View).
  bool ShowFrames(HWND parent, std::span<const RECT> frames, int radius,
                  std::span<const COLORREF> borders = {}, float border_width = 0);
  void Hide();
  bool visible() const { return visible_; }
  // Modal shade in the browser's compositor, above its children and below
  // separate owned dialogs.
  bool ShowModalShade(HWND parent);
  void HideModalShade();
  bool modal_shade_visible() const { return modal_shade_ != nullptr; }
  // Passive URL label above the native document: no HWND or hit-test surface.
  bool PrepareLinkStatus(HWND parent) { return Init(parent); }
  bool ShowLinkStatus(HWND parent, const RECT& area, float scale,
                      const std::wstring& text, const Theme& theme);
  void HideLinkStatus();
  bool link_status_visible() const { return link_status_visible_; }
  RECT link_status_bounds() const { return link_status_bounds_; }
  RECT link_status_area() const { return link_status_area_; }
  // Passive notification in the same composition tree, above native Chrome.
  // No new HWND, focus target, site script or hit-test surface.
  // With |keys| the detail line shows them as key caps followed by |tail|
  // ("Press [Esc] to exit full screen"); the card's place and size are those
  // of every toast. Relayout passes |animate| false to keep any active motion.
  bool ShowNotification(HWND parent, const RECT& area, float scale,
                        const std::wstring& title, const std::wstring& detail,
                        const std::vector<std::wstring>& keys, const std::wstring& tail, bool animate,
                        bool* animation_started = nullptr);
  // Toasts take the space's theme: card, text and key caps (set before Show).
  void SetNotificationTheme(const Theme& theme, bool dark) { notification_theme_ = theme; notification_dark_ = dark; }
  void PositionNotification(const RECT& area);
  void HideNotification();
  // Returns the compositor exit duration, or zero when motion is disabled.
  int DismissNotification();
  bool notification_visible() const { return notification_visible_; }
  // The Ctrl+Tab strip (tab_switcher_strip.cpp), centred over |area|, in this composition tree: drawn by the
  // browser, never by a website. Card pictures are JPEG/PNG bytes (empty: the title's letter / a blank icon).
  struct SwitcherCard { std::wstring title, letter; std::string thumb, icon; bool current = false; };
  bool ShowTabSwitcher(HWND parent, const RECT& area, float scale, const std::vector<SwitcherCard>& cards, int selected);
  bool SelectTabSwitcher(int selected);
  void HideTabSwitcher();
  bool tab_switcher_visible() const { return switcher_visible_; }
  RECT tab_switcher_bounds() const { return switcher_bounds_; }
  RECT notification_bounds() const { return notification_bounds_; }
  // Live bitmap of our own layered popup, scaled by the notification compositor.
  // Does not resize its CEF viewport or request a screenshot.
  static constexpr int kPopupShadowMarginDip = 36;
  bool BeginPopupAnimation(HWND parent, HWND popup, bool bottom, float origin_x,
                           bool closing = false, float from_scale = 1.f, float origin_y = -1.f);
  bool FadePopupShadow(HWND popup, int duration_ms);
  void EndPopupAnimation(HWND popup);
  bool ShowSidebarShadow(HWND parent, const RECT& panel, float scale, double progress);
  void HideSidebarShadow() { sidebar_shadow_.Hide(); }
  bool ShowLinkPeekShadow(HWND parent,const RECT& panel,float scale);
  void HideLinkPeekShadow() { link_peek_shadow_.Hide(); }
  bool ShowLinkPeekScene(HWND parent,const std::vector<HWND>& sources,HWND preview,const RECT& area,const RECT& card,POINT origin,float scale,bool can_split,int clip_left,bool animate,bool page_ready,bool live=false) {
    return Init(parent) && link_peek_scene_.Show(device_.Get(),d2d_.Get(),root_.Get(),parent,sources,preview,area,card,origin,scale,can_split,clip_left,animate,page_ready,live);
  }
  void EndLinkPeekScene() { link_peek_scene_.End(); link_peek_scene_.EndPulse(); }
  void LinkPeekLiveDepth(float from,float to,int ms) { link_peek_scene_.LiveDepth(from,to,ms); }
  bool PulseLinkPeek(HWND parent,const RECT& area,POINT origin,float scale) {
    return Init(parent) && link_peek_scene_.Pulse(device_.Get(),d2d_.Get(),root_.Get(),parent,area,origin,scale);
  }
  void SettleLinkPeekScene() { link_peek_scene_.Settle(); }
  bool LinkPeekComposed() const { return link_peek_scene_.Active(); }
  bool LinkPeekOpening() const { return link_peek_scene_.Opening(); }
  int LinkPeekEntryDelayMs() const { return link_peek_scene_.EntryDelayMs(); }
  bool LinkPeekDepthReady() const { return link_peek_scene_.DepthReady(); }
  bool LinkPeekPreviewVisible() const { return link_peek_scene_.PreviewVisible(); }
  bool HoldLinkPeekSource(HWND handle,const void* png,size_t size) { return link_peek_scene_.HoldSource(handle,png,size); }
  void HoverLinkPeek(int index) { link_peek_scene_.Hover(index); }
  bool HoldLinkPeekFrame(const void* png,size_t size,int width,int height) {
    const auto frame=DecodePageSnapshot(png,size,width,height); if (!frame) return false;
    link_peek_scene_.HoldClosingFrame(frame.Get()); return true;
  }
  int BeginLinkPeekReturn() { return link_peek_scene_.BeginReturn(); }
  void DiscardLinkPeekFrame() { link_peek_scene_.DiscardClosingFrame(); }
 private:
  Microsoft::WRL::ComPtr<IDCompositionSurface> DecodePageSnapshot(const void* png, size_t size, int width, int height);
  bool Init(HWND parent);
  bool Paint(int corner, int radius, COLORREF color, COLORREF border, float border_width);
  bool PaintTabSwitcher();
  Microsoft::WRL::ComPtr<IDCompositionVisual> switcher_;
  Microsoft::WRL::ComPtr<IDCompositionEffectGroup> switcher_opacity_;
  Microsoft::WRL::ComPtr<IDCompositionSurface> switcher_surface_;
  std::vector<SwitcherCard> switcher_cards_;
  std::vector<Microsoft::WRL::ComPtr<ID2D1Bitmap1>> switcher_thumbs_, switcher_icons_;
  RECT switcher_area_{}, switcher_bounds_{};
  float switcher_scale_ = 1;
  int switcher_selected_ = 0;
  bool switcher_visible_ = false;
  Microsoft::WRL::ComPtr<ID3D11Device> d3d_;
  Microsoft::WRL::ComPtr<ID2D1Factory1> d2d_factory_;
  Microsoft::WRL::ComPtr<ID2D1DeviceContext> d2d_;
  Microsoft::WRL::ComPtr<IDCompositionDevice> device_;
  Microsoft::WRL::ComPtr<IDCompositionTarget> target_;
  Microsoft::WRL::ComPtr<IDCompositionVisual> root_;
  Microsoft::WRL::ComPtr<IDCompositionVisual> modal_shade_;
  SidebarShadow sidebar_shadow_;
  SidebarShadow link_peek_shadow_;
  LinkPeekScene link_peek_scene_;
  Microsoft::WRL::ComPtr<IDCompositionVisual> popup_anchor_, popup_picture_, popup_shadow_;
  Microsoft::WRL::ComPtr<IDCompositionEffectGroup> popup_shadow_opacity_;
  Microsoft::WRL::ComPtr<IDCompositionScaleTransform> popup_growth_;
  Microsoft::WRL::ComPtr<IDCompositionRectangleClip> popup_clip_;
  Microsoft::WRL::ComPtr<IUnknown> popup_bitmap_;
  Microsoft::WRL::ComPtr<IDCompositionSurface> popup_shadow_surface_;
  HWND popup_source_ = nullptr;
  Microsoft::WRL::ComPtr<IDCompositionVisual> notification_;
  Microsoft::WRL::ComPtr<IDCompositionVisual> notification_motion_;
  Microsoft::WRL::ComPtr<IDCompositionEffectGroup> notification_opacity_;
  Microsoft::WRL::ComPtr<IDCompositionScaleTransform> notification_growth_;
  Microsoft::WRL::ComPtr<IDCompositionSurface> notification_surface_;
  int notification_width_ = 0, notification_height_ = 0;
  bool notification_visible_ = false;
  RECT notification_bounds_{};
  float notification_scale_ = 1;
  std::wstring notification_title_, notification_detail_, notification_tail_;
  std::vector<std::wstring> notification_keys_;
  Theme notification_theme_{};
  bool notification_dark_ = false;
  std::array<Microsoft::WRL::ComPtr<IDCompositionVisual>, 16> visuals_;
  std::array<Microsoft::WRL::ComPtr<IDCompositionSurface>, 16> surfaces_;
  std::array<COLORREF, 16> colors_{};
  std::array<COLORREF, 16> borders_{};
  float border_width_ = 0;
  HWND parent_ = nullptr;
  int radius_ = 0;
  bool failed_ = false, visible_ = false;
  Microsoft::WRL::ComPtr<IDCompositionVisual> link_status_;
  Microsoft::WRL::ComPtr<IDCompositionSurface> link_status_surface_;
  Microsoft::WRL::ComPtr<IDCompositionEffectGroup> link_status_opacity_;
  std::wstring link_status_text_;
  Theme link_status_theme_;
  RECT link_status_bounds_{};
  RECT link_status_area_{};
  float link_status_scale_ = 0;
  int link_status_limit_ = 0, link_status_width_ = 0;
  bool link_status_visible_ = false;
};
// The workspace background behind the page corners, matching style.css
// `.workspace` (linear-gradient(155deg, #eedfe8 0%, #e5dff0 55%, #dbdeee 100%)).
// Keep both in sync when the shell background changes. The authored stops
// follow the shell theme (SetWorkspaceBackgroundTheme), as the CSS does.
COLORREF WorkspaceBackgroundAt(double x, double y, double width, double height, HWND parent = nullptr);
void SetWorkspaceBackgroundTheme(const Theme& theme, bool dark, HWND parent = nullptr);
void ForgetWorkspaceBackgroundTheme(HWND parent);
// The three themed gradient stops (0, 55 %, 100 %) of the workspace background.
std::array<COLORREF, 3> WorkspaceBackgroundStops(HWND parent = nullptr);
bool WorkspaceUsesSpatialPalette(HWND parent);
}  // namespace red
