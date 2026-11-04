#pragma once
#include <windows.h>
#include <dcomp.h>
#include <d2d1_1.h>
#include <wrl/client.h>
#include <vector>

namespace red {
// Stable in-memory source frames and a neutral animated card. Sites keep their
// HWNDs and viewports; the preview uses its ordinary HWND after entry. Never
// mirror or change Chrome's layered/cloak styles (intermittent GPU flicker).
class LinkPeekScene {
 public:
  ~LinkPeekScene() { End(); EndPulse(); }
  bool Show(IDCompositionDevice* device,ID2D1DeviceContext* context,IDCompositionVisual* root,HWND parent,
    const std::vector<HWND>& sources,HWND preview,const RECT& area,const RECT& card,POINT origin,float scale,bool can_split,int clip_left,bool animate,bool page_ready,
    bool live=false);
  void Settle();
  void End();
  void Hover(int index);
  bool HoldSource(HWND handle,const void* png,size_t size);
  // The source picture matches the live pages when it appears; they are
  // masked only once it is surely on screen, so no frame shows neither.
  bool DepthReady() const { return depth_ready_ && GetTickCount64()>=depth_started_+kMaskDelayMs; }
  static constexpr unsigned long long kMaskDelayMs=80;
  // Entry (growth, bounce, controls) and return, milliseconds.
  static constexpr int kEntryMs=460,kReturnMs=300;
  // While the source frame is captured: a faint iridescent scanline with a
  // soft trail sweeps the page area top to bottom; its border glows faintly.
  bool Pulse(IDCompositionDevice* device,ID2D1DeviceContext* context,IDCompositionVisual* root,HWND parent,const RECT& area,POINT origin,float scale);
  void EndPulse();
  bool PreviewVisible() const { return revealed_; }
  void HoldClosingFrame(IDCompositionSurface* frame) { closing_frame_=frame; }
  // Live sources: the pages scale from |from| to |to| over |ms| (the engine's
  // FAST_OUT_SLOW_IN curve); a frame of the workspace background with rounded
  // inner corners covers their uncovered edges on the same curve.
  void LiveDepth(float from,float to,int ms);
  void DiscardClosingFrame() { closing_frame_.Reset(); }
  int BeginReturn();
  bool Returning() const { return returning_; }
  bool Active() const { return scene_!=nullptr; }
  bool Opening() const { return Active() && GetTickCount64()<opening_until_; }
 private:
  struct Window {
    HWND handle=nullptr;
    Microsoft::WRL::ComPtr<ID2D1Bitmap1> bitmap;
  };
  bool Build(bool animate);
  bool PositionSourceTarget();
  bool RefreshSource(bool animate);
  Microsoft::WRL::ComPtr<IDCompositionSurface> Canvas(bool background,bool sources);
  Microsoft::WRL::ComPtr<IDCompositionSurface> Solid(int width,int height,D2D1_COLOR_F color);
  Microsoft::WRL::ComPtr<IDCompositionSurface> Shadow(int width,int height);
  Microsoft::WRL::ComPtr<IDCompositionSurface> Tinted(int width,int height);
  static constexpr float kVeil=.36f;
 public:
  // The depth live source pages recede to (the frozen pictures' 98.5%).
  static constexpr float kLiveSourceScale=.985f;
  static constexpr float kLiveSourceDim=.26f;
  static COLORREF SourceVeilColor(HWND parent);
  // Live sources: the page recedes first, then the card grows (milliseconds after the click).
  static constexpr int kLiveCardDelayMs=220;
  // The card's entry delay of the current scene (0, or kLiveCardDelayMs when live).
  int EntryDelayMs() const { return entry_delay_ms_; }
 private:
  bool PaintControls();
  void Reveal();
  Microsoft::WRL::ComPtr<IDCompositionDevice> device_;
  Microsoft::WRL::ComPtr<IDCompositionTarget> source_target_;
  Microsoft::WRL::ComPtr<ID2D1DeviceContext> context_;
  Microsoft::WRL::ComPtr<IDCompositionVisual> root_,scene_,front_,picture_,controls_,source_picture_,background_;
  Microsoft::WRL::ComPtr<IDCompositionVisual> source_root_,source_scene_;
  Microsoft::WRL::ComPtr<IDCompositionRectangleClip> scene_clip_;
  Microsoft::WRL::ComPtr<IDCompositionSurface> placeholder_,shadow_,controls_surface_;
  Microsoft::WRL::ComPtr<IDCompositionSurface> source_surface_,background_surface_;
  Microsoft::WRL::ComPtr<IDCompositionSurface> closing_frame_;
  Microsoft::WRL::ComPtr<IDCompositionScaleTransform> source_scale_,front_scale_;
  Microsoft::WRL::ComPtr<IDCompositionEffectGroup> shade_,front_opacity_,controls_opacity_;
  Microsoft::WRL::ComPtr<IDCompositionEffectGroup> live_opacity_,dim_opacity_;
  Microsoft::WRL::ComPtr<IDCompositionSurface> dim_surface_;
  Microsoft::WRL::ComPtr<IDCompositionDevice> pulse_device_;
  Microsoft::WRL::ComPtr<IDCompositionVisual> pulse_root_,pulse_;
  std::vector<Microsoft::WRL::ComPtr<IDCompositionSurface>> pulse_surfaces_;
  std::vector<Window> sources_;
  Window preview_;
  HWND parent_=nullptr,source_host_=nullptr;
  RECT area_{},card_{};
  POINT origin_{};
  float scale_=1;
  float bounce_=.01f;
  // Live sources (engine patch 11): the pages recede themselves; no source
  // picture, background or dimmer layer is built.
  bool live_=false;
  float live_from_=1,live_to_=1; int live_ms_=0;
  Microsoft::WRL::ComPtr<IDCompositionVisual> live_frame_;
  // The frame's pieces in the card's columns: the live Peek window covers
  // them at rest, the animated card does not (entry, return).
  Microsoft::WRL::ComPtr<IDCompositionEffectGroup> live_center_opacity_;
  std::vector<Microsoft::WRL::ComPtr<IDCompositionSurface>> live_surfaces_;
  bool BuildLiveFrame();
  Microsoft::WRL::ComPtr<IDCompositionAnimation> Eased(float from,float to,float seconds);
  Microsoft::WRL::ComPtr<IDCompositionSurface> Corner(int size,COLORREF color,int quadrant);
  bool can_split_=true,returning_=false,page_ready_=false,revealed_=false,depth_ready_=false,faded_=false;
  int hover_=-1;
  unsigned long long opening_until_=0;
  int entry_delay_ms_=0;
  unsigned long long depth_started_=0;
};
}  // namespace red
