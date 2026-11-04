#include "windows/embedded_content.h"
#include "windows/devtools_dock.h"
#include "windows/sidebar_peek.h"
#include "workspace/controller.h"
#include "windows/focus.h"
#include "include/views/cef_display.h"
#include "include/base/cef_callback.h"
#include "include/wrapper/cef_closure_task.h"
#include "windows/motion.h"
#include <algorithm>
#include <cmath>
namespace red {
namespace {
constexpr float kLiveScale=LinkPeekScene::kLiveSourceScale;
#if defined(RED_CEF_PAGE_CONTAINER)
constexpr float kLiveDim=LinkPeekScene::kLiveSourceDim;
#else
constexpr float kLiveDim=0;  // Legacy live frame owns its own veil.
#endif
void TransformSource(CefRefPtr<CefBrowser> browser,float scale,float dim,int ms,HWND root) {
#if defined(RED_CEF_PAGE_CONTAINER)
  const auto tint=LinkPeekScene::SourceVeilColor(root);
  browser->GetHost()->SetContentContainerTransform(scale,dim,ms,
      CefColorSetARGB(255,GetRValue(tint),GetGValue(tint),GetBValue(tint)));
#elif defined(RED_CEF_CONTENT_TRANSFORM)
  browser->GetHost()->SetContentTransform(scale,dim,ms);
#else
  (void)browser; (void)scale; (void)dim; (void)ms; (void)root;
#endif
}
// gfx::Tween::FAST_OUT_SLOW_IN (cubic-bezier .4,0,.2,1), as the engine animates.
float FastOutSlowIn(float x) {
  if (x<=0) return 0; if (x>=1) return 1;
  float lo=0,hi=1,t=x;
  for (int i=0;i<24;++i) {
    t=(lo+hi)/2; const float u=1-t,bx=3*u*u*t*.4f+3*u*t*t*.2f+t*t*t;
    (bx<x ? lo : hi)=t;
  }
  const float u=1-t; return 3*u*t*t+t*t*t;
}
}
std::vector<CefRefPtr<CefBrowser>> EmbeddedContent::LinkPeekSourceBrowsers() const {
  std::vector<CefRefPtr<CefBrowser>> sources;
  for (const auto& [id,browser]:browsers_) {
    if (id==link_peek_id_ || !IsWindowVisible(browser->GetHost()->GetWindowHandle())) continue;
    if (id==link_peek_base_ || IsSplitPage(id)) sources.push_back(browser);
  }
  return sources;
}
float EmbeddedContent::LinkPeekSourceScale() const {
  if (live_ms_<=0) return live_scale_to_;
  const auto elapsed=static_cast<long long>(GetTickCount64()-live_started_);
  const float progress=FastOutSlowIn(static_cast<float>(elapsed)/static_cast<float>(live_ms_));
  return live_scale_from_+(live_scale_to_-live_scale_from_)*progress;
}
bool EmbeddedContent::LinkPeekSourcesAnimating() const {
  return live_ms_>0 && GetTickCount64()<live_started_+live_ms_;
}
void EmbeddedContent::AnimateLinkPeekSources(float scale,float dim,int ms) {
#if defined(RED_CEF_CONTENT_TRANSFORM)
  if (!NativeMotionEnabled()) ms=0;
  live_scale_from_=LinkPeekSourceScale(); live_scale_to_=scale; live_dim_=dim; live_ms_=ms; live_started_=GetTickCount64();
  const auto state=state_.lock();
  if (state && state->probe) {
    auto motion=CefDictionaryValue::Create(); motion->SetInt("duration_ms",ms); motion->SetDouble("scale",scale);
    motion->SetDouble("started",static_cast<double>(live_started_)); state->Record("peek_source_animation_request",motion);
  }
  for (const auto& browser:LinkPeekSourceBrowsers()) TransformSource(browser,scale,dim,ms,state && state->window ? state->window->GetWindowHandle() : nullptr);
  // The scene's frame covers the pages' uncovered edges on the same curve.
  corners_.LinkPeekLiveDepth(live_scale_from_,scale,ms);
#else
  (void)scale; (void)dim; (void)ms;
#endif
}

CefRect EmbeddedContent::LinkPeekCard() const {
  auto area=browser_fullscreen_ ? FullClientBounds() : WorkspacePhysicalBounds();
  const auto state=state_.lock(); const double scale=state && state->window ? state->window->GetDisplay()->GetDeviceScaleFactor() : 1.;
  const int edge=static_cast<int>(std::lround(8*scale)),rail=static_cast<int>(std::lround(44*scale));
  const int width=std::max(1,std::min(static_cast<int>(area.width*.88),area.width-2*edge-rail));
  const int x=std::max(area.x+edge,std::min(area.x+(area.width-width)/2,area.x+area.width-edge-rail-width));
  if (browser_fullscreen_) return {x,area.y+edge,width,std::max(1,area.height-2*edge)};
  // The card reaches the page area's top and bottom edges (user tuned: 10 and
  // 4 DIP of overhang were too much); kept as a knob, clamped to the window.
  const auto client=FullClientBounds(); const int overhang=0;
  const int top=std::max(client.y,area.y-overhang),bottom=std::min(client.y+client.height,area.y+area.height+overhang);
  return {x,top,width,std::max(1,bottom-top)};
}
CefRect EmbeddedContent::LinkPeekViewport() const { return IsFullscreenPage(link_peek_id_) ? FullClientBounds() : LinkPeekCard(); }
void EmbeddedContent::SetLinkPeek(int id,bool resume) {
  if (id != link_peek_id_) HideLinkStatus();
  const auto state=state_.lock(); if (!state || !state->window) return;
  const bool fresh=!HasLinkPeek();
  if (fresh) {
    FinishPresentation(presentation_generation_,false);
    link_peek_base_=state->content ? state->content->GetIdentifier() : -1;
    link_peek_composition_failed_=link_peek_scene_started_=link_peek_page_ready_=link_peek_geometry_traced_=false; link_peek_return_delay_=0;
    link_peek_frame_.clear(); link_peek_loader_.clear(); link_peek_frame_query_=link_peek_paint_query_=0;
    link_peek_waiting_=false; link_peek_source_frames_.clear();
  }
  link_peek_id_=id; crash_presented_=-1;
  if (const auto browser=browsers_.find(id); browser!=browsers_.end()) {
    state->content=browser->second; state->keyboard_target=id; ObserveLinkPeekReadiness(browser->second);
  }
  if (!fresh) { LayoutLinkPeek(); return; }
#if defined(RED_CEF_CONTENT_TRANSFORM)
  link_peek_live_=true;
#endif
  if (resume) {
    // Its page kept all state while parked: present the card at rest at once;
    // the source picture (and dimmer) joins when captured.
    ++link_peek_generation_; link_peek_scene_started_=link_peek_page_ready_=true;
    if (link_peek_live_) { StartLinkPeekScene(); AnimateLinkPeekSources(kLiveScale,kLiveDim,0); return; }
    CaptureLinkPeekSources(); link_peek_waiting_=false; StartLinkPeekScene(); return;
  }
  if (link_peek_live_) {
    // The pages stay live: the chromatic wave answers the click and the card
    // grows at once while they recede and dim.
    ++link_peek_generation_;
    const auto area=SplitDropArea();
    const bool pulsed=corners_.PulseLinkPeek(state->window->GetWindowHandle(),RECT{area.x,area.y,area.x+area.width,area.y+area.height},link_peek_origin_,
      static_cast<float>(state->window->GetDisplay()->GetDeviceScaleFactor()));
    auto glow=CefDictionaryValue::Create(); glow->SetBool("shown",pulsed); glow->SetInt("ms",0); state->Record("link_peek_pulse",glow);
    StartLinkPeekScene();
    AnimateLinkPeekSources(kLiveScale,kLiveDim,LinkPeekScene::kReturnMs);
    auto live=CefDictionaryValue::Create();
#if defined(RED_CEF_PAGE_CONTAINER)
    live->SetBool("container",true);
#else
    live->SetBool("container",false);
#endif
    live->SetDouble("scale",kLiveScale); live->SetDouble("dim",kLiveDim);
    live->SetInt("duration_ms",LinkPeekScene::kReturnMs); live->SetInt("card_delay_ms",LinkPeekScene::kLiveCardDelayMs);
    state->Record("link_peek_live_sources",live);
    return;
  }
  // Freeze first: the pages stay as they are (a chromatic wave answers the
  // click) until their frames are captured; then the card grows over pictures
  // identical to them, so there is no visible swap.
  ++link_peek_generation_;
  const auto area=SplitDropArea();
  const auto pulse_started=GetTickCount64();
  const bool pulsed=corners_.PulseLinkPeek(state->window->GetWindowHandle(),RECT{area.x,area.y,area.x+area.width,area.y+area.height},link_peek_origin_,
    static_cast<float>(state->window->GetDisplay()->GetDeviceScaleFactor()));
  auto glow=CefDictionaryValue::Create(); glow->SetBool("shown",pulsed); glow->SetInt("ms",static_cast<int>(GetTickCount64()-pulse_started));
  state->Record("link_peek_pulse",glow);
  CaptureLinkPeekSources();
  if (link_peek_waiting_) LayoutLinkPeek(); else StartLinkPeekScene();
}
void EmbeddedContent::StartLinkPeekScene() {
  link_peek_waiting_=false;
  if (!HasLinkPeek()) { link_peek_source_frames_.clear(); return; }
  LayoutLinkPeek();
  for (const auto& [handle,bytes]:link_peek_source_frames_) corners_.HoldLinkPeekSource(handle,bytes.data(),bytes.size());
  link_peek_source_frames_.clear();
  if (const auto state=state_.lock(); state && state->workspace) state->workspace->Refresh();
  // Masks the live pages once their pictures are on screen; reveals the site
  // once entry has finished.
  for (const bool settle:{false,true}) CefPostDelayedTask(TID_UI,base::BindOnce([](std::weak_ptr<State> weak,unsigned long long generation,bool settle) {
    if (const auto state=weak.lock(); state && state->embedded && state->embedded->HasLinkPeek() && state->embedded->link_peek_generation_==generation) {
      if (settle) state->embedded->corners_.SettleLinkPeekScene();
      state->embedded->LayoutLinkPeek(); if (state->workspace) state->workspace->Refresh();
    }
  },state_,link_peek_generation_,settle),settle ? LinkPeekScene::kEntryMs+corners_.LinkPeekEntryDelayMs()+30 : static_cast<int>(LinkPeekScene::kMaskDelayMs)+10);
}
void EmbeddedContent::ClearLinkPeek(bool promote) {
  const auto state=state_.lock(); if (!HasLinkPeek()) return;
  const auto base=browsers_.find(link_peek_base_);
  const int preview=link_peek_id_; link_peek_id_=-1; link_peek_base_=-1; ++link_peek_generation_;
  DiscardLinkPeekClose(); link_peek_return_delay_=0; corners_.EndLinkPeekScene(); corners_.HideLinkPeekShadow();
  if (link_peek_live_) {
    // Every page (a split partner may have left the sources meanwhile).
    for (const auto& [id,browser]:browsers_) if (id!=preview) TransformSource(browser,1,0,0,state && state->window ? state->window->GetWindowHandle() : nullptr);
    link_peek_live_=false; live_scale_from_=live_scale_to_=1; live_dim_=0; live_ms_=0;
  }
  link_peek_source_captures_.clear(); link_peek_waiting_=false; link_peek_source_frames_.clear();
  if (state && !promote) state->content=base==browsers_.end() ? nullptr : base->second;
  for (const auto& [id,browser]:browsers_) {
    const auto handle=browser->GetHost()->GetWindowHandle(); EnableWindow(handle,TRUE); Shape(handle);
    if (id==preview && !promote) ShowWindow(handle,SW_HIDE);
  }
  if (!promote) { Resize(bounds_); RaiseActive(); }
}
int EmbeddedContent::ReturnPendingLinkPeek() {
  if (link_peek_id_!=-2) return 0;
  link_peek_return_delay_=corners_.BeginLinkPeekReturn();
  if (link_peek_live_) AnimateLinkPeekSources(1,0,link_peek_return_delay_ ? LinkPeekScene::kReturnMs : 0);
  if (const auto state=state_.lock()) {
    auto result=CefDictionaryValue::Create(); result->SetBool("animated",link_peek_return_delay_>0);
    result->SetBool("pending_browser",true); state->Record("link_peek_return",result);
  }
  return link_peek_return_delay_;
}
void EmbeddedContent::ShapeLinkPeek(HWND handle) const {
  const auto state=state_.lock(); if (!state || !state->window || link_peek_waiting_) return;
  const auto preview=browsers_.find(link_peek_id_);
  const bool is_preview=preview!=browsers_.end() && preview->second->GetHost()->GetWindowHandle()==handle;
  HRGN region=nullptr;
  if (is_preview && LinkPeekComposed() && !corners_.LinkPeekPreviewVisible()) {
    // Shown but drawn nowhere until revealed: a hidden Chrome window stops
    // painting and repaints late when shown (a stale/blank flash).
    region=CreateRectRgn(0,0,0,0);
  } else if (is_preview && !IsFullscreenPage(link_peek_id_)) {
    RECT client{}; GetClientRect(handle,&client);
    const int diameter=static_cast<int>(std::lround((LinkPeekComposed() ? 12 : 16)*state->window->GetDisplay()->GetDeviceScaleFactor()));
    // Native clipping stays inside the smooth 8-DIP composition edge.
    region=CreateRoundRectRgn(0,0,client.right+1,client.bottom+1,diameter,diameter);
  } else if (!is_preview && link_peek_live_ && LinkPeekComposed()) {
    // The live page recedes inside its full window; the scene's frame covers its edges.
    region=nullptr;
  } else if (!is_preview && LinkPeekComposed() && LinkPeekDepthReady() && !link_peek_return_delay_) {
    // A retained full-size native page must not remain visible behind its
    // reduced picture, including its outer edges. Keep the browser alive.
    region=CreateRectRgn(0,0,0,0);
  } else if (!is_preview && !LinkPeekComposed()) {
    RECT client{},frame{}; GetClientRect(handle,&client); GetWindowRect(handle,&frame);
    POINT point{frame.left,frame.top}; ScreenToClient(state->window->GetWindowHandle(),&point);
    const auto card=LinkPeekCard(); const int rail=static_cast<int>(std::lround(32*state->window->GetDisplay()->GetDeviceScaleFactor()));
    region=CreateRectRgn(0,0,client.right,client.bottom);
    const auto hole=CreateRectRgn(card.x-point.x,card.y-point.y,card.x+card.width+rail-point.x,card.y+card.height-point.y);
    if (region && hole) CombineRgn(region,region,hole,RGN_DIFF);
    if (hole) DeleteObject(hole);
  }
  const auto previous=CreateRectRgn(0,0,0,0); const int exists=previous ? GetWindowRgn(handle,previous) : ERROR;
  const bool same=region ? exists!=ERROR && EqualRgn(region,previous) : exists==ERROR;
  if (previous) DeleteObject(previous);
  if (same) { if (region) DeleteObject(region); return; }
  if (!SetWindowRgn(handle,region,TRUE) && region) DeleteObject(region);
}
void EmbeddedContent::LayoutLinkPeek() {
  const auto state=state_.lock(); if (!state || !state->window || !HasLinkPeek()) return;
  const HWND root=state->window->GetWindowHandle();
  if (link_peek_waiting_) {  // Nothing changes on screen until the frames are in.
    if (const auto preview=browsers_.find(link_peek_id_); preview!=browsers_.end()) ShowWindow(preview->second->GetHost()->GetWindowHandle(),SW_HIDE);
    return;
  }
  if (state->devtools_dock) state->devtools_dock->Layout(LinkPeekViewport(),link_peek_id_,IsFullscreen());
  const HWND sidebar=InlineSidebar();
  std::vector<HWND> sources; HWND preview=nullptr;
  for (const auto& [id,browser]:browsers_) {
    const auto handle=browser->GetHost()->GetWindowHandle();
    if (id!=link_peek_id_ && (!IsWindowVisible(handle) || (id!=link_peek_base_ && !IsSplitPage(id)))) continue;
    const auto target=PagePhysicalBounds(id); RECT rect{}; GetWindowRect(handle,&rect);
    POINT point{rect.left,rect.top}; ScreenToClient(root,&point);
    if (point.x!=target.x || point.y!=target.y || rect.right-rect.left!=target.width || rect.bottom-rect.top!=target.height)
      SetWindowPos(handle,nullptr,target.x,target.y,target.width,target.height,SWP_NOZORDER|SWP_NOACTIVATE);
    if (id==link_peek_id_) {
      preview=handle;
      if (GetWindow(handle,GW_HWNDPREV)!=sidebar) SetWindowPos(handle,sidebar ? sidebar : HWND_TOP,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
    } else { sources.push_back(handle); EnableWindow(handle,FALSE); }
  }
  const auto card=LinkPeekCard(),area=SplitDropArea();
  const bool docked=state->devtools_dock && state->devtools_dock->Describe()!=nullptr;
  bool composed=false;
  if (!link_peek_composition_failed_ && !IsFullscreenPage(link_peek_id_) && !docked && crash_presented_!=link_peek_id_) {
    int clip_left=area.x;
    if (sidebar) { RECT rect{}; GetWindowRect(sidebar,&rect); POINT p{rect.right,rect.top}; ScreenToClient(root,&p); clip_left=std::max(clip_left,static_cast<int>(p.x)); }
    composed=corners_.ShowLinkPeekScene(root,sources,preview,RECT{area.x,area.y,area.x+area.width,area.y+area.height},
      RECT{card.x,card.y,card.x+card.width,card.y+card.height},link_peek_origin_,state->window->GetDisplay()->GetDeviceScaleFactor(),state->workspace && state->workspace->CanSplitLinkPeek(),clip_left,!link_peek_scene_started_,link_peek_page_ready_,
      link_peek_live_);
    if (!composed) { link_peek_composition_failed_=true; state->Record("peek_live_composition_failed"); }
    else if (!link_peek_scene_started_) { link_peek_scene_started_=true; state->Record("peek_live_composition_started"); }
  } else corners_.EndLinkPeekScene();
  if (link_peek_live_ && !LinkPeekSourcesAnimating()) {
    // Resized pages need the transform again (it is relative to their size);
    // without composition they show unscaled, like the native fallback.
    const float scale=composed ? live_scale_to_ : 1,dim=composed ? live_dim_ : 0;
    if (state->probe) {
      auto layout=CefDictionaryValue::Create(); layout->SetInt("elapsed_ms",static_cast<int>(GetTickCount64()-live_started_));
      layout->SetInt("duration_ms",live_ms_); layout->SetDouble("scale",scale); state->Record("peek_source_layout_target",layout);
    }
    for (const auto& browser:LinkPeekSourceBrowsers()) TransformSource(browser,scale,dim,0,root);
  }
  for (HWND source:sources) ShapeLinkPeek(source);
  if (preview) {
    ShapeLinkPeek(preview); EnableWindow(preview,!LinkPeekOpening() && (!composed || corners_.LinkPeekPreviewVisible()));
    if (crash_presented_==link_peek_id_) ShowWindow(preview,SW_HIDE);
    else if (!IsWindowVisible(preview)) ShowWindow(preview,SW_SHOWNOACTIVATE);
    if (composed && corners_.LinkPeekPreviewVisible() && !link_peek_geometry_traced_ && state->tab_trace.is_open()) {
      // Numeric only: compare the ordinary Chrome client with the exact card
      // used by composition, once per opening. No URL, title or page pixels.
      RECT client{},frame{},root_frame{},root_client{};
      POINT page_at{},root_at{};
      GetClientRect(preview,&client); GetWindowRect(preview,&frame); ClientToScreen(preview,&page_at);
      GetWindowRect(root,&root_frame); GetClientRect(root,&root_client); ClientToScreen(root,&root_at);
      const auto metrics=CefDictionaryValue::Create();
      metrics->SetInt("card_x",card.x); metrics->SetInt("card_y",card.y);
      metrics->SetInt("card_width",card.width); metrics->SetInt("card_height",card.height);
      metrics->SetInt("client_dx",page_at.x-root_at.x-card.x); metrics->SetInt("client_dy",page_at.y-root_at.y-card.y);
      metrics->SetInt("client_dw",client.right-card.width); metrics->SetInt("client_dh",client.bottom-card.height);
      metrics->SetInt("page_inset_x",page_at.x-frame.left); metrics->SetInt("page_inset_y",page_at.y-frame.top);
      metrics->SetInt("root_inset_x",root_at.x-root_frame.left); metrics->SetInt("root_inset_y",root_at.y-root_frame.top);
      metrics->SetInt("root_width",root_client.right); metrics->SetInt("root_height",root_client.bottom);
      metrics->SetInt("dpi",GetDpiForWindow(root)); metrics->SetBool("maximized",state->window->IsMaximized());
      state->Record("page_transition_peek_geometry",metrics); link_peek_geometry_traced_=true;
    }
  }
  if (composed || IsFullscreenPage(link_peek_id_)) corners_.HideLinkPeekShadow();
  else corners_.ShowLinkPeekShadow(root,RECT{card.x,card.y,card.x+card.width,card.y+card.height},state->window->GetDisplay()->GetDeviceScaleFactor());
  UpdateCorners();
}
}  // namespace red
