#include "windows/link_peek_scene.h"
#include "windows/page_corners.h"
#include "windows/motion.h"
#include <d2d1effects.h>
#include <wincodec.h>
#include <algorithm>
#include <cmath>
using Microsoft::WRL::ComPtr;
namespace red {
namespace {
LRESULT CALLBACK PeekSourceProc(HWND window,UINT message,WPARAM w,LPARAM l) {
  switch (message) {
    case WM_NCHITTEST: return HTTRANSPARENT;
    case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: { PAINTSTRUCT paint; BeginPaint(window,&paint); EndPaint(window,&paint); return 0; }
  }
  return DefWindowProcW(window,message,w,l);
}
HWND CreatePeekSource(HWND parent) {
  static const ATOM atom=[] {
    WNDCLASSEXW wc{sizeof(wc)}; wc.lpfnWndProc=PeekSourceProc; wc.hInstance=GetModuleHandleW(nullptr);
    wc.lpszClassName=L"RedPeekSourceLayer"; return RegisterClassExW(&wc);
  }();
  return atom ? CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP|WS_EX_TRANSPARENT|WS_EX_NOACTIVATE,
    L"RedPeekSourceLayer",L"",WS_CHILD,0,0,1,1,parent,nullptr,GetModuleHandleW(nullptr),nullptr) : nullptr;
}
ComPtr<IDCompositionRectangleClip> Rounded(IDCompositionDevice* device,float width,float height,float radius) {
  ComPtr<IDCompositionRectangleClip> clip; if (FAILED(device->CreateRectangleClip(&clip))) return nullptr;
  clip->SetLeft(0.f); clip->SetTop(0.f); clip->SetRight(width); clip->SetBottom(height);
  clip->SetTopLeftRadiusX(radius); clip->SetTopLeftRadiusY(radius); clip->SetTopRightRadiusX(radius); clip->SetTopRightRadiusY(radius);
  clip->SetBottomLeftRadiusX(radius); clip->SetBottomLeftRadiusY(radius); clip->SetBottomRightRadiusX(radius); clip->SetBottomRightRadiusY(radius); return clip;
}
// A saturated color |shift| (units of 60 degrees) around the theme's hue.
D2D1_COLOR_F ThemeTint(HWND parent,float shift,float alpha) {
  const auto theme=WorkspaceBackgroundStops(parent)[1];
  const float r=GetRValue(theme)/255.f,g=GetGValue(theme)/255.f,b=GetBValue(theme)/255.f;
  const float high=std::max({r,g,b}),low=std::min({r,g,b}),range=high-low;
  float hue=0; if (range>.001f) hue=high==r ? std::fmod((g-b)/range,6.f) : high==g ? (b-r)/range+2 : (r-g)/range+4;
  const float h=std::fmod(hue+shift+12.f,6.f),x=1-std::abs(std::fmod(h,2.f)-1),c=.85f,m=.15f;
  const float rgb[6][3]={{c,x*c,0},{x*c,c,0},{0,c,x*c},{0,x*c,c},{x*c,0,c},{c,0,x*c}};
  const auto& p=rgb[static_cast<int>(h)%6]; return D2D1::ColorF(p[0]+m,p[1]+m,p[2]+m,alpha);
}
ComPtr<IDCompositionAnimation> Flight(IDCompositionDevice* device,float from,float to,float duration) {
  ComPtr<IDCompositionAnimation> a; const float d=to-from;
  if (FAILED(device->CreateAnimation(&a)) || FAILED(a->AddCubic(0,from,0,3*d/(duration*duration),-2*d/(duration*duration*duration))) || FAILED(a->End(duration,to))) return nullptr;
  return a;
}
}
bool LinkPeekScene::PositionSourceTarget() {
  if (!source_host_) source_host_=CreatePeekSource(parent_);
  if (!source_host_) return false;
  if (!source_target_ && (FAILED(device_->CreateTargetForHwnd(source_host_,TRUE,&source_target_)) ||
      FAILED(device_->CreateVisual(&source_root_)) || FAILED(source_target_->SetRoot(source_root_.Get())))) return false;
  RECT client{},frame{}; POINT at{};
  if (!GetClientRect(parent_,&client) || !GetWindowRect(source_host_,&frame) || !ClientToScreen(source_host_,&at) || !ScreenToClient(parent_,&at)) return false;
  const bool sized=at.x==0 && at.y==0 && frame.right-frame.left==client.right && frame.bottom-frame.top==client.bottom;
  const HWND above=IsWindow(preview_.handle) ? preview_.handle : HWND_TOP;
  const bool placed=GetWindow(source_host_,GW_HWNDPREV)==(above==HWND_TOP ? nullptr : above);
  if (!sized || !placed || !IsWindowVisible(source_host_)) return SetWindowPos(source_host_,above,0,0,client.right,client.bottom,
    SWP_NOACTIVATE|SWP_SHOWWINDOW|(sized ? SWP_NOMOVE|SWP_NOSIZE : 0))!=FALSE;
  return true;
}
ComPtr<IDCompositionSurface> LinkPeekScene::Solid(int width,int height,D2D1_COLOR_F color) {
  if (width<1 || height<1 || width>16384 || height>16384) return nullptr;
  ComPtr<IDCompositionSurface> surface; ComPtr<IDXGISurface> dxgi; ComPtr<ID2D1Bitmap1> target; POINT offset{};
  if (FAILED(device_->CreateSurface(width,height,DXGI_FORMAT_B8G8R8A8_UNORM,DXGI_ALPHA_MODE_PREMULTIPLIED,&surface)) ||
      FAILED(surface->BeginDraw(nullptr,__uuidof(IDXGISurface),&dxgi,&offset))) return nullptr;
  const auto properties=D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET|D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
    D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED));
  bool ok=SUCCEEDED(context_->CreateBitmapFromDxgiSurface(dxgi.Get(),&properties,&target));
  if (ok) {
    context_->SetTarget(target.Get()); context_->SetTransform(D2D1::Matrix3x2F::Translation(static_cast<float>(offset.x),static_cast<float>(offset.y)));
    context_->BeginDraw();
    context_->PushAxisAlignedClip(D2D1::RectF(0,0,static_cast<float>(width),static_cast<float>(height)),D2D1_ANTIALIAS_MODE_ALIASED);
    context_->Clear(color); context_->PopAxisAlignedClip(); ok=SUCCEEDED(context_->EndDraw()); context_->SetTarget(nullptr);
  }
  ok=SUCCEEDED(surface->EndDraw()) && ok; return ok ? surface : nullptr;
}
ComPtr<IDCompositionSurface> LinkPeekScene::Shadow(int width,int height) {
  const int margin=static_cast<int>(std::lround(36*scale_));
  auto surface=Solid(width+2*margin,height+2*margin,D2D1::ColorF(0,0,0,0));
  if (!surface) return nullptr;
  ComPtr<IDXGISurface> dxgi; ComPtr<ID2D1Bitmap1> target; POINT offset{};
  if (FAILED(surface->BeginDraw(nullptr,__uuidof(IDXGISurface),&dxgi,&offset))) return nullptr;
  const auto properties=D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET|D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
    D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED));
  ComPtr<ID2D1SolidColorBrush> brush; ComPtr<ID2D1CommandList> shape; ComPtr<ID2D1Effect> blur;
  ComPtr<ID2D1GradientStopCollection> stops; ComPtr<ID2D1LinearGradientBrush> mask;
  const float feather=std::min(.25f,32*scale_/height);
  const D2D1_GRADIENT_STOP gradient[]={{0,D2D1::ColorF(0,0,0,0)},{feather,D2D1::ColorF(0,0,0,1)},
    {1-feather,D2D1::ColorF(0,0,0,1)},{1,D2D1::ColorF(0,0,0,0)}};
  bool ok=SUCCEEDED(context_->CreateBitmapFromDxgiSurface(dxgi.Get(),&properties,&target)) &&
    SUCCEEDED(context_->CreateSolidColorBrush(D2D1::ColorF(0,0,0,.36f),&brush)) && SUCCEEDED(context_->CreateCommandList(&shape)) &&
    SUCCEEDED(context_->CreateEffect(CLSID_D2D1GaussianBlur,&blur)) &&
    SUCCEEDED(context_->CreateGradientStopCollection(gradient,4,&stops)) &&
    SUCCEEDED(context_->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(D2D1::Point2F(0,static_cast<float>(margin)),D2D1::Point2F(0,static_cast<float>(margin+height))),stops.Get(),&mask));
  if (ok) {
    context_->SetTarget(shape.Get()); context_->SetTransform(D2D1::Matrix3x2F::Identity()); context_->BeginDraw();
    const float x=static_cast<float>(margin),y=x+5*scale_;
    context_->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(x,y,x+width,y+height),8*scale_,8*scale_),brush.Get());
    ok=SUCCEEDED(context_->EndDraw()) && SUCCEEDED(shape->Close());
    blur->SetInput(0,shape.Get()); ok=SUCCEEDED(blur->SetValue(D2D1_GAUSSIANBLUR_PROP_STANDARD_DEVIATION,10*scale_)) && ok;
    context_->SetTarget(target.Get()); context_->SetTransform(D2D1::Matrix3x2F::Translation(static_cast<float>(offset.x),static_cast<float>(offset.y)));
    context_->BeginDraw();
    context_->PushAxisAlignedClip(D2D1::RectF(0,0,static_cast<float>(width+2*margin),static_cast<float>(height+2*margin)),D2D1_ANTIALIAS_MODE_ALIASED);
    context_->Clear(D2D1::ColorF(0,0,0,0));
    D2D1_LAYER_PARAMETERS1 layer{}; layer.contentBounds=D2D1::InfiniteRect(); layer.maskAntialiasMode=D2D1_ANTIALIAS_MODE_PER_PRIMITIVE;
    layer.maskTransform=D2D1::Matrix3x2F::Identity(); layer.opacity=1; layer.opacityBrush=mask.Get();
    context_->PushLayer(&layer,nullptr); context_->DrawImage(blur.Get()); context_->PopLayer();
    // The real page now sits below this passive surface. Keep only the outer
    // shadow; an opaque live mirror previously covered its dark interior.
    context_->SetPrimitiveBlend(D2D1_PRIMITIVE_BLEND_COPY); brush->SetColor(D2D1::ColorF(0,0,0,0));
    context_->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(static_cast<float>(margin),static_cast<float>(margin),static_cast<float>(margin+width),static_cast<float>(margin+height)),8*scale_,8*scale_),brush.Get());
    context_->SetPrimitiveBlend(D2D1_PRIMITIVE_BLEND_SOURCE_OVER); context_->PopAxisAlignedClip(); ok=SUCCEEDED(context_->EndDraw()) && ok;
  }
  context_->SetTarget(nullptr); ok=SUCCEEDED(surface->EndDraw()) && ok; return ok ? surface : nullptr;
}
// The lower layer's veil: a dimmer with a translucent diagonal gradient in
// the theme's hues over it (shown at kVeil opacity).
ComPtr<IDCompositionSurface> LinkPeekScene::Tinted(int width,int height) {
  auto surface=Solid(width,height,D2D1::ColorF(0,0,0,0)); if (!surface) return nullptr;
  ComPtr<IDXGISurface> dxgi; ComPtr<ID2D1Bitmap1> target; POINT offset{};
  if (FAILED(surface->BeginDraw(nullptr,__uuidof(IDXGISurface),&dxgi,&offset))) return nullptr;
  const auto properties=D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET|D2D1_BITMAP_OPTIONS_CANNOT_DRAW,D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED));
  const D2D1_GRADIENT_STOP stops[]={{0,ThemeTint(parent_,-1.1f,.42f)},{.5f,ThemeTint(parent_,0,.3f)},{1,ThemeTint(parent_,1.2f,.42f)}};
  ComPtr<ID2D1GradientStopCollection> collection; ComPtr<ID2D1LinearGradientBrush> gradient;
  bool ok=SUCCEEDED(context_->CreateBitmapFromDxgiSurface(dxgi.Get(),&properties,&target)) && SUCCEEDED(context_->CreateGradientStopCollection(stops,3,&collection)) &&
    SUCCEEDED(context_->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(D2D1::Point2F(0,0),D2D1::Point2F(static_cast<float>(width),static_cast<float>(height))),collection.Get(),&gradient));
  if (ok) {
    const auto full=D2D1::RectF(0,0,static_cast<float>(width),static_cast<float>(height));
    context_->SetTarget(target.Get()); context_->SetTransform(D2D1::Matrix3x2F::Translation(static_cast<float>(offset.x),static_cast<float>(offset.y)));
    context_->BeginDraw(); context_->PushAxisAlignedClip(full,D2D1_ANTIALIAS_MODE_ALIASED);
    context_->Clear(D2D1::ColorF(0,0,0,.62f)); context_->FillRectangle(full,gradient.Get());
    context_->PopAxisAlignedClip(); ok=SUCCEEDED(context_->EndDraw()); context_->SetTarget(nullptr);
  }
  ok=SUCCEEDED(surface->EndDraw()) && ok; return ok ? surface : nullptr;
}
bool LinkPeekScene::PaintControls() {
  // Three 32-DIP buttons, 8 DIP apart; glyphs keep their 24-DIP grid, centered.
  constexpr float size=32,step=40,inset=(size-24)/2;
  const int width=static_cast<int>(std::lround(size*scale_)),height=static_cast<int>(std::lround((2*step+size)*scale_));
  auto surface=Solid(width,height,D2D1::ColorF(0,0,0,0));
  if (!surface) return false;
  ComPtr<IDXGISurface> dxgi; ComPtr<ID2D1Bitmap1> target; POINT offset{};
  if (FAILED(surface->BeginDraw(nullptr,__uuidof(IDXGISurface),&dxgi,&offset))) return false;
  const auto properties=D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET|D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
    D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED));
  ComPtr<ID2D1SolidColorBrush> fill,ink;
  bool ok=SUCCEEDED(context_->CreateBitmapFromDxgiSurface(dxgi.Get(),&properties,&target)) &&
    SUCCEEDED(context_->CreateSolidColorBrush(D2D1::ColorF(.13f,.13f,.15f,.92f),&fill)) &&
    SUCCEEDED(context_->CreateSolidColorBrush(D2D1::ColorF(1,1,1,.9f),&ink));
  if (ok) {
    context_->SetTarget(target.Get()); context_->BeginDraw();
    context_->SetTransform(D2D1::Matrix3x2F::Translation(static_cast<float>(offset.x),static_cast<float>(offset.y)));
    context_->PushAxisAlignedClip(D2D1::RectF(0,0,static_cast<float>(width),static_cast<float>(height)),D2D1_ANTIALIAS_MODE_ALIASED);
    context_->Clear(D2D1::ColorF(0,0,0,0));
    for (int index=0; index<3; ++index) {
      const auto button=D2D1::Matrix3x2F::Scale(scale_,scale_)*D2D1::Matrix3x2F::Translation(static_cast<float>(offset.x),offset.y+step*scale_*index);
      context_->SetTransform(button);
      fill->SetColor(index==hover_ ? D2D1::ColorF(.29f,.29f,.32f,.98f) : D2D1::ColorF(.13f,.13f,.15f,.92f));
      context_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(size/2,size/2),size/2,size/2),fill.Get());
      context_->SetTransform(D2D1::Matrix3x2F::Translation(inset,inset)*button);
      ink->SetColor(D2D1::ColorF(1,1,1,index==2 && !can_split_ ? .35f : .9f));
      const auto line=[&](float x,float y,float xx,float yy) { context_->DrawLine(D2D1::Point2F(x,y),D2D1::Point2F(xx,yy),ink.Get(),1.3f); };
      if (!index) { line(8,8,16,16); line(16,8,8,16); }
      else if (index==1) {
        context_->DrawRectangle(D2D1::RectF(6,8,15,17),ink.Get(),1.2f); line(11,13,18,6); line(13,6,18,6); line(18,6,18,11);
      } else { context_->DrawRectangle(D2D1::RectF(6,7,18,17),ink.Get(),1.2f); line(12,7,12,17); }
    }
    context_->PopAxisAlignedClip(); ok=SUCCEEDED(context_->EndDraw()); context_->SetTarget(nullptr);
  }
  ok=SUCCEEDED(surface->EndDraw()) && ok;
  if (ok) { controls_surface_=surface; controls_->SetContent(surface.Get()); } return ok;
}

bool LinkPeekScene::HoldSource(HWND handle,const void* png,size_t size) {
  const auto source=std::find_if(sources_.begin(),sources_.end(),[&](const auto& w){return w.handle==handle;});
  if (source==sources_.end() || !png || !size || size>16*1024*1024 || returning_) return false;
  ComPtr<IWICImagingFactory> factory; ComPtr<IWICStream> stream; ComPtr<IWICBitmapDecoder> decoder;
  ComPtr<IWICBitmapFrameDecode> frame; ComPtr<IWICFormatConverter> converter;
  if (FAILED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory))) || FAILED(factory->CreateStream(&stream)) ||
      FAILED(stream->InitializeFromMemory(static_cast<BYTE*>(const_cast<void*>(png)),static_cast<DWORD>(size))) ||
      FAILED(factory->CreateDecoderFromStream(stream.Get(),nullptr,WICDecodeMetadataCacheOnLoad,&decoder)) || FAILED(decoder->GetFrame(0,&frame))) return false;
  UINT width=0,height=0; if (FAILED(frame->GetSize(&width,&height)) || !width || !height || static_cast<unsigned long long>(width)*height>16*1024*1024) return false;
  if (FAILED(factory->CreateFormatConverter(&converter)) || FAILED(converter->Initialize(frame.Get(),GUID_WICPixelFormat32bppPBGRA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom))) return false;
  const auto properties=D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_NONE,D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED));
  if (FAILED(context_->CreateBitmapFromWicBitmap(converter.Get(),&properties,&source->bitmap))) return false;
  const bool ready=std::all_of(sources_.begin(),sources_.end(),[](const auto& w){return w.bitmap!=nullptr;});
  if (ready && !depth_ready_) {
    depth_ready_=true; depth_started_=GetTickCount64(); if (!RefreshSource(true)) return false; device_->Commit();
  }
  return true;
}
ComPtr<IDCompositionSurface> LinkPeekScene::Canvas(bool background,bool sources) {
  const int width=area_.right-area_.left,height=area_.bottom-area_.top;
  auto surface=Solid(width,height,D2D1::ColorF(0,0,0,0)); if (!surface) return nullptr;
  ComPtr<IDXGISurface> dxgi; ComPtr<ID2D1Bitmap1> target; POINT offset{};
  if (FAILED(surface->BeginDraw(nullptr,__uuidof(IDXGISurface),&dxgi,&offset))) return nullptr;
  const auto properties=D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET|D2D1_BITMAP_OPTIONS_CANNOT_DRAW,D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED));
  ComPtr<ID2D1SolidColorBrush> brush; bool ok=SUCCEEDED(context_->CreateBitmapFromDxgiSurface(dxgi.Get(),&properties,&target)) && SUCCEEDED(context_->CreateSolidColorBrush(D2D1::ColorF(0,0,0,.22f),&brush));
  const auto full=D2D1::RectF(0,0,static_cast<float>(width),static_cast<float>(height));
  ComPtr<ID2D1Factory> factory; ComPtr<ID2D1RoundedRectangleGeometry> frame_clip; context_->GetFactory(&factory);
  ok=SUCCEEDED(factory->CreateRoundedRectangleGeometry(D2D1::RoundedRect(full,6*scale_,6*scale_),&frame_clip)) && ok;
  if (ok) {
    context_->SetTarget(target.Get()); context_->BeginDraw();
    const auto translation=D2D1::Matrix3x2F::Translation(static_cast<float>(offset.x),static_cast<float>(offset.y));
    context_->SetTransform(translation);
    // BeginDraw supplies fresh atlas storage, not the pixels from Solid's
    // earlier update. Clear every update and stay inside its allotted rectangle.
    context_->PushAxisAlignedClip(D2D1::RectF(0,0,static_cast<float>(width),static_cast<float>(height)),D2D1_ANTIALIAS_MODE_ALIASED);
    context_->Clear(D2D1::ColorF(0,0,0,0));
    const auto push=[&](ID2D1Geometry* clip) {
      D2D1_LAYER_PARAMETERS1 layer{}; layer.contentBounds=D2D1::InfiniteRect(); layer.geometricMask=clip;
      layer.maskAntialiasMode=D2D1_ANTIALIAS_MODE_PER_PRIMITIVE; layer.maskTransform=D2D1::Matrix3x2F::Identity(); layer.opacity=1;
      context_->PushLayer(&layer,nullptr);
    };
    if (background) {
      RECT client{}; GetClientRect(parent_,&client); const auto colors=WorkspaceBackgroundStops(parent_);
      if(WorkspaceUsesSpatialPalette(parent_)){
        constexpr UINT size=48;std::array<std::uint32_t,size*size> pixels{};
        for(UINT y=0;y<size;++y)for(UINT x=0;x<size;++x){const auto c=WorkspaceBackgroundAt(area_.left+double(x)*width/(size-1),area_.top+double(y)*height/(size-1),client.right,client.bottom,parent_);
          pixels[y*size+x]=0xff000000u|(std::uint32_t(GetRValue(c))<<16)|(std::uint32_t(GetGValue(c))<<8)|GetBValue(c);}
        const auto mesh_properties=D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_NONE,D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED));
        ComPtr<ID2D1Bitmap1> mesh;ok=SUCCEEDED(context_->CreateBitmap(D2D1::SizeU(size,size),pixels.data(),size*4,&mesh_properties,&mesh));
        if(ok)context_->DrawBitmap(mesh.Get(),D2D1::RectF(0,0,static_cast<float>(width),static_cast<float>(height)),1,D2D1_INTERPOLATION_MODE_LINEAR);
      }else{
      D2D1_GRADIENT_STOP stops[3]{}; const float positions[]={0,.55f,1};
      for (int i=0;i<3;++i) stops[i]={positions[i],D2D1::ColorF(GetRValue(colors[i])/255.f,GetGValue(colors[i])/255.f,GetBValue(colors[i])/255.f)};
      ComPtr<ID2D1GradientStopCollection> collection; ComPtr<ID2D1LinearGradientBrush> gradient;
      constexpr float dx=.42261826f,dy=.90630779f;
      const float length=client.right*dx+client.bottom*dy,cx=client.right/2.f-area_.left,cy=client.bottom/2.f-area_.top;
      ok=SUCCEEDED(context_->CreateGradientStopCollection(stops,3,&collection)) && SUCCEEDED(context_->CreateLinearGradientBrush(
        D2D1::LinearGradientBrushProperties(D2D1::Point2F(cx-dx*length/2,cy-dy*length/2),D2D1::Point2F(cx+dx*length/2,cy+dy*length/2)),collection.Get(),&gradient));
      if (ok) context_->FillRectangle(D2D1::RectF(0,0,static_cast<float>(width),static_cast<float>(height)),gradient.Get());
      }
    }
    const bool clip_sources=sources || !background;
    if (clip_sources) push(frame_clip.Get());
    if (sources) for (const auto& source:sources_) if (source.bitmap) {
      RECT frame{}; GetWindowRect(source.handle,&frame); POINT point{frame.left,frame.top}; ScreenToClient(parent_,&point);
      const float x=static_cast<float>(point.x-area_.left),y=static_cast<float>(point.y-area_.top);
      context_->DrawBitmap(source.bitmap.Get(),D2D1::RectF(x,y,x+frame.right-frame.left,y+frame.bottom-frame.top),1,D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    }
    if (clip_sources) context_->PopLayer();
    context_->PopAxisAlignedClip(); ok=SUCCEEDED(context_->EndDraw()) && ok; context_->SetTarget(nullptr);
  }
  ok=SUCCEEDED(surface->EndDraw()) && ok; return ok ? surface : nullptr;
}
bool LinkPeekScene::RefreshSource(bool animate) {
  if (live_) return true;
  if (!source_picture_ || !background_) return false;
  if (!depth_ready_) { source_picture_->SetContent(nullptr); background_->SetContent(nullptr); return true; }
  source_surface_=Canvas(false,true); background_surface_=Canvas(true,false);
  if (!source_surface_ || !background_surface_) return false;
  source_picture_->SetContent(source_surface_.Get()); background_->SetContent(background_surface_.Get());
  // Appears identical to the live pages (scale 1, no dimmer), then recedes
  // and dims together with the card's growth.
  const auto depth=EaseNativeMotion(device_.Get(),1,.985f,kReturnMs/1000.f);
  const auto dim=EaseNativeMotion(device_.Get(),0,kVeil,kReturnMs/1000.f);
  if (animate && NativeMotionEnabled() && depth && dim) {
    source_scale_->SetScaleX(depth.Get()); source_scale_->SetScaleY(depth.Get()); dim_opacity_->SetOpacity(dim.Get());
  } else { source_scale_->SetScaleX(.985f); source_scale_->SetScaleY(.985f); dim_opacity_->SetOpacity(kVeil); }
  return true;
}
bool LinkPeekScene::Build(bool animate) {
  if (live_ && live_ms_>0) { live_from_=live_to_; live_ms_=0; }
  const int width=card_.right-card_.left,height=card_.bottom-card_.top;
  ComPtr<IDCompositionVisual> scene,source_scene,front,picture,shadow,controls,backdrop,source,background,dim;
  ComPtr<IDCompositionRectangleClip> scene_clip; ComPtr<IDCompositionScaleTransform> source_scale,front_scale;
  ComPtr<IDCompositionEffectGroup> front_opacity,controls_opacity,cover,dim_opacity;
  if (FAILED(device_->CreateVisual(&scene)) || FAILED(device_->CreateVisual(&source_scene)) || FAILED(device_->CreateRectangleClip(&scene_clip)) || FAILED(device_->CreateVisual(&source)) ||
      FAILED(device_->CreateVisual(&background)) || FAILED(device_->CreateScaleTransform(&source_scale)) || FAILED(device_->CreateVisual(&front)) ||
      FAILED(device_->CreateVisual(&picture)) || FAILED(device_->CreateVisual(&shadow)) || FAILED(device_->CreateScaleTransform(&front_scale)) ||
      FAILED(device_->CreateEffectGroup(&front_opacity)) || FAILED(device_->CreateVisual(&controls)) || FAILED(device_->CreateEffectGroup(&controls_opacity)) ||
      FAILED(device_->CreateVisual(&backdrop)) || FAILED(device_->CreateEffectGroup(&cover)) || FAILED(device_->CreateEffectGroup(&dim_opacity)) ||
      FAILED(device_->CreateVisual(&dim))) return false;
  const float extent=std::max({1.f,std::abs(static_cast<float>(origin_.y-card_.top)),std::abs(static_cast<float>(card_.bottom-origin_.y))});
  bounce_=std::min(.016f,6*scale_/extent);
  const float delay=entry_delay_ms_/1000.f;
  const auto growth=PeekEntryMotion(device_.Get(),bounce_,delay),fade=EaseNativeMotion(device_.Get(),0,1,.08f,delay);
  ComPtr<IDCompositionAnimation> buttons;
  constexpr float reveal=.14f; const float shown=delay+kEntryMs/1000.f-reveal;
  if (!growth || !fade || FAILED(device_->CreateAnimation(&buttons)) || FAILED(buttons->AddCubic(0,0,0,0,0)) ||
      FAILED(buttons->AddCubic(shown,0,3/reveal,-3/(reveal*reveal),1/(reveal*reveal*reveal))) || FAILED(buttons->End(shown+reveal,1))) return false;
  source->SetOffsetX(static_cast<float>(area_.left)); source->SetOffsetY(static_cast<float>(area_.top));
  background->SetOffsetX(static_cast<float>(area_.left)); background->SetOffsetY(static_cast<float>(area_.top));
  source_scale->SetCenterX((area_.right-area_.left)/2.f); source_scale->SetCenterY((area_.bottom-area_.top)/2.f); source->SetTransform(source_scale.Get());
  dim_surface_=Tinted(area_.right-area_.left,area_.bottom-area_.top);
  const auto dim_clip=Rounded(device_.Get(),static_cast<float>(area_.right-area_.left),static_cast<float>(area_.bottom-area_.top),6*scale_);
  if (!dim_surface_ || !dim_clip) return false;
  dim->SetContent(dim_surface_.Get()); dim->SetClip(dim_clip.Get()); dim_opacity->SetOpacity(0.f); dim->SetEffect(dim_opacity.Get());
  source->AddVisual(dim.Get(),TRUE,nullptr);
  const auto color=WorkspaceBackgroundAt(card_.left+width/2.,card_.top+height/2.,area_.right,area_.bottom,parent_);
  const float gray=(GetRValue(color)+GetGValue(color)+GetBValue(color))/3<128 ? .14f : .94f;
  placeholder_=Solid(width,height,D2D1::ColorF(gray,gray,gray,1)); shadow_=Shadow(width,height);
  const auto clip=Rounded(device_.Get(),static_cast<float>(width),static_cast<float>(height),8*scale_);
  if (!placeholder_ || !shadow_ || !clip) return false;
  backdrop->SetContent(placeholder_.Get()); backdrop->SetClip(clip.Get()); cover->SetOpacity(1.f); backdrop->SetEffect(cover.Get());
  picture->SetClip(clip.Get());
  shadow->SetContent(shadow_.Get()); shadow->SetOffsetX(-36*scale_); shadow->SetOffsetY(-36*scale_);
  front->AddVisual(shadow.Get(),FALSE,nullptr); front->AddVisual(backdrop.Get(),FALSE,nullptr); front->AddVisual(picture.Get(),FALSE,nullptr);
  front->SetOffsetX(static_cast<float>(card_.left)); front->SetOffsetY(static_cast<float>(card_.top));
  front_scale->SetCenterX(static_cast<float>(origin_.x-card_.left)); front_scale->SetCenterY(static_cast<float>(origin_.y-card_.top));
  if (animate) { front_scale->SetScaleX(growth.Get()); front_scale->SetScaleY(growth.Get()); front_opacity->SetOpacity(fade.Get()); controls_opacity->SetOpacity(buttons.Get()); }
  else { front_scale->SetScaleX(1.f); front_scale->SetScaleY(1.f); front_opacity->SetOpacity(1.f); controls_opacity->SetOpacity(1.f); }
  front->SetTransform(front_scale.Get()); front->SetEffect(front_opacity.Get());
  controls->SetOffsetX(card_.right+8*scale_); controls->SetOffsetY(card_.top+16*scale_); controls->SetEffect(controls_opacity.Get());
  source_scene->SetClip(scene_clip.Get()); source_scene->AddVisual(background.Get(),FALSE,nullptr); source_scene->AddVisual(source.Get(),FALSE,nullptr);
  scene->SetClip(scene_clip.Get());
  scene->AddVisual(front.Get(),FALSE,nullptr); scene->AddVisual(controls.Get(),FALSE,nullptr);
  if (scene_) root_->RemoveVisual(scene_.Get());
  // Live pages have no frozen-source target/root. A geometry rebuild can
  // replace the detached source scene without trying to remove it from one.
  if (source_root_ && source_scene_) source_root_->RemoveVisual(source_scene_.Get());
  source_scene_=source_scene;
  scene_=scene; scene_clip_=scene_clip; front_=front; picture_=picture; controls_=controls; background_=background; source_picture_=source;
  source_scale_=source_scale; front_scale_=front_scale; front_opacity_=front_opacity; controls_opacity_=controls_opacity; live_opacity_=cover; dim_opacity_=dim_opacity;
  if (live_ && !BuildLiveFrame()) return false;
  if (!PaintControls() || !RefreshSource(false) || (source_root_ && FAILED(source_root_->AddVisual(source_scene_.Get(),FALSE,nullptr))) ||
      FAILED(root_->AddVisual(scene_.Get(),TRUE,nullptr))) return false;
  return true;
}
bool LinkPeekScene::Show(IDCompositionDevice* device,ID2D1DeviceContext* context,IDCompositionVisual* root,HWND parent,
  const std::vector<HWND>& sources,HWND preview,const RECT& area,const RECT& card,POINT origin,float scale,bool can_split,int clip_left,bool animate,bool page_ready,
  bool live) {
  if (returning_) return true;
  bool changed=sources.size()!=sources_.size(); if (!changed) for (size_t i=0;i<sources.size();++i) if (sources[i]!=sources_[i].handle) changed=true;
  if (changed || parent_!=parent || live!=live_) End();
  live_=live;
  const bool fresh=!scene_; device_=device; context_=context; root_=root; parent_=parent; origin_=origin;
  if (sources_.empty()) for (HWND handle:sources) { Window source; source.handle=handle; sources_.push_back(std::move(source)); }
  const bool geometry=fresh || !EqualRect(&area,&area_) || !EqualRect(&card,&card_) || scale_!=scale;
  const bool controls_changed=can_split_!=can_split; area_=area; card_=card; scale_=scale; can_split_=can_split; page_ready_=page_ready;
  preview_.handle=preview;
  if (!live_ && !PositionSourceTarget()) { End(); return false; }
  // Nothing to capture: the live pages are the source (DepthReady at once).
  if (live_ && fresh) { depth_ready_=true; depth_started_=GetTickCount64()-kMaskDelayMs; }
  if (fresh) {
    entry_delay_ms_=live_ && animate && NativeMotionEnabled() ? kLiveCardDelayMs : 0;
    opening_until_=GetTickCount64()+(animate && NativeMotionEnabled() ? kEntryMs+entry_delay_ms_ : 0);
  }
  if (geometry) { revealed_=false; if (!Build(fresh && animate && NativeMotionEnabled())) { End(); return false; } }
  if (!geometry && controls_changed) PaintControls();
  // The click glow starts in the top target (nothing else exists yet); once
  // the scene runs it moves under the Peek HWND, above the source pictures.
  if (pulse_ && source_root_ && pulse_root_.Get()!=source_root_.Get()) {
    pulse_root_->RemoveVisual(pulse_.Get());
    if (SUCCEEDED(source_root_->AddVisual(pulse_.Get(),TRUE,nullptr))) pulse_root_=source_root_; else pulse_.Reset();
  }
  // The card may overhang the page area at top and bottom; its shadow with it.
  const int shadow=static_cast<int>(std::lround(36*scale));
  scene_clip_->SetLeft(static_cast<float>(std::max(static_cast<int>(area.left),clip_left))); scene_clip_->SetTop(static_cast<float>(std::min(area.top,card.top-shadow)));
  scene_clip_->SetRight(static_cast<float>(area.right)); scene_clip_->SetBottom(static_cast<float>(std::max(area.bottom,card.bottom+shadow)));
  if (!Opening()) Settle();
  if (FAILED(device_->Commit())) { End(); return false; } return true;
}
void LinkPeekScene::Settle() { if (!Opening()) { opening_until_=0; Reveal(); } }
void LinkPeekScene::Reveal() {
  if (returning_ || revealed_ || Opening() || !page_ready_ || !IsWindow(preview_.handle) || !live_opacity_) return;
  // The retained source stays in the lower sibling target, unchanged from
  // entry. The ordinary Peek HWND is above it: no flattened source swap,
  // transparent hole or second coordinate/scale calculation is needed here.
  // The HWND stayed shown (with an empty region) during entry, so Chrome kept
  // painting it: the cover fades over current content, not a stale frame.
  // Only the first reveal fades; a rebuilt scene uncovers it at once.
  const bool warm=faded_; faded_=true;
  ShowWindow(preview_.handle,SW_SHOWNOACTIVATE);
  ComPtr<IDCompositionAnimation> fade;
  constexpr float duration=.12f;
  if (!warm && NativeMotionEnabled() && SUCCEEDED(device_->CreateAnimation(&fade)) && SUCCEEDED(fade->AddCubic(0,1,0,0,0)) &&
      SUCCEEDED(fade->AddCubic(.05f,1,-3/duration,3/(duration*duration),-1/(duration*duration*duration))) && SUCCEEDED(fade->End(.05f+duration,0))) live_opacity_->SetOpacity(fade.Get());
  else live_opacity_->SetOpacity(0.f);
  if (live_center_opacity_) live_center_opacity_->SetOpacity(0.f);
  revealed_=true; device_->Commit();
}
bool LinkPeekScene::Pulse(IDCompositionDevice* device,ID2D1DeviceContext* context,IDCompositionVisual* root,HWND parent,const RECT& area,POINT,float scale) {
  EndPulse();
  if (!NativeMotionEnabled() || !device || !context || !root || area.right<=area.left || area.bottom<=area.top) return false;
  // Iridescent palette around the theme's own hue (units of 60 degrees).
  const auto theme=WorkspaceBackgroundStops(parent)[1];
  const float r=GetRValue(theme)/255.f,g=GetGValue(theme)/255.f,b=GetBValue(theme)/255.f;
  const float high=std::max({r,g,b}),low=std::min({r,g,b}),range=high-low;
  float hue=0; if (range>.001f) hue=high==r ? std::fmod((g-b)/range,6.f) : high==g ? (b-r)/range+2 : (r-g)/range+4;
  const auto tint=[hue](float shift,float alpha,float white=0) {
    const float h=std::fmod(hue+shift+12.f,6.f),x=1-std::abs(std::fmod(h,2.f)-1),c=.85f*(1-white),m=.15f+.85f*white;
    const float rgb[6][3]={{c,x*c,0},{x*c,c,0},{0,c,x*c},{0,x*c,c},{x*c,0,c},{c,0,x*c}};
    const auto& p=rgb[static_cast<int>(h)%6]; return D2D1::ColorF(p[0]+m,p[1]+m,p[2]+m,alpha);
  };
  const float width=static_cast<float>(area.right-area.left),height=static_cast<float>(area.bottom-area.top);
  std::vector<ComPtr<IDCompositionSurface>> surfaces;
  // Paints one surface; |paint| draws in surface pixels.
  const auto surface=[&](int w,int h,const auto& paint) -> ComPtr<IDCompositionSurface> {
    ComPtr<IDCompositionSurface> result; ComPtr<IDXGISurface> dxgi; ComPtr<ID2D1Bitmap1> target; POINT offset{};
    if (w<1 || h<1 || w>16384 || h>16384 || FAILED(device->CreateSurface(w,h,DXGI_FORMAT_B8G8R8A8_UNORM,DXGI_ALPHA_MODE_PREMULTIPLIED,&result)) ||
        FAILED(result->BeginDraw(nullptr,__uuidof(IDXGISurface),&dxgi,&offset))) return nullptr;
    const auto properties=D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET|D2D1_BITMAP_OPTIONS_CANNOT_DRAW,D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED));
    bool ok=SUCCEEDED(context->CreateBitmapFromDxgiSurface(dxgi.Get(),&properties,&target));
    if (ok) {
      context->SetTarget(target.Get()); context->SetTransform(D2D1::Matrix3x2F::Translation(static_cast<float>(offset.x),static_cast<float>(offset.y)));
      context->BeginDraw(); context->PushAxisAlignedClip(D2D1::RectF(0,0,static_cast<float>(w),static_cast<float>(h)),D2D1_ANTIALIAS_MODE_ALIASED);
      context->Clear(D2D1::ColorF(0,0,0,0)); ok=paint(static_cast<float>(offset.x),static_cast<float>(offset.y));
      context->PopAxisAlignedClip(); ok=SUCCEEDED(context->EndDraw()) && ok; context->SetTarget(nullptr);
    }
    ok=SUCCEEDED(result->EndDraw()) && ok; if (!ok) return nullptr;
    surfaces.push_back(result); return result;
  };
  // A |w|x|h| band: horizontal hue sweep, vertical alpha profile |alpha|.
  const auto band=[&](int w,int h,const std::vector<D2D1_GRADIENT_STOP>& alpha,float white) {
    return surface(w,h,[&](float,float) {
      const D2D1_GRADIENT_STOP hues[]={{0,tint(-1.3f,1,white)},{.35f,tint(-.4f,1,white)},{.65f,tint(.5f,1,white)},{1,tint(1.4f,1,white)}};
      ComPtr<ID2D1GradientStopCollection> colors,mask_stops; ComPtr<ID2D1LinearGradientBrush> fill,mask;
      if (FAILED(context->CreateGradientStopCollection(hues,4,&colors)) || FAILED(context->CreateGradientStopCollection(alpha.data(),static_cast<UINT32>(alpha.size()),&mask_stops)) ||
          FAILED(context->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(D2D1::Point2F(0,0),D2D1::Point2F(static_cast<float>(w),0)),colors.Get(),&fill)) ||
          FAILED(context->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(D2D1::Point2F(0,0),D2D1::Point2F(0,static_cast<float>(h))),mask_stops.Get(),&mask))) return false;
      D2D1_LAYER_PARAMETERS1 layer{}; layer.contentBounds=D2D1::InfiniteRect(); layer.maskAntialiasMode=D2D1_ANTIALIAS_MODE_PER_PRIMITIVE;
      layer.maskTransform=D2D1::Matrix3x2F::Identity(); layer.opacity=1; layer.opacityBrush=mask.Get();
      context->PushLayer(&layer,nullptr); context->FillRectangle(D2D1::RectF(0,0,static_cast<float>(w),static_cast<float>(h)),fill.Get()); context->PopLayer();
      return true;
    });
  };
  const auto a=[](float at,float alpha) { return D2D1_GRADIENT_STOP{at,D2D1::ColorF(0,0,0,alpha)}; };
  // Scanline: a thin, slightly whitened line, and a soft trail above it.
  const auto line=band(512,32,{a(0,0),a(.35f,.45f),a(.5f,1),a(.65f,.45f),a(1,0)},.45f);
  const auto trail=band(512,256,{a(0,0),a(.6f,.12f),a(.92f,.35f),a(1,0)},0);
  // Edge glow: the area's border lights up faintly as the line passes.
  const int gw=static_cast<int>(width),gh=static_cast<int>(height);
  const auto edge=surface(gw,gh,[&](float x,float y) {
    ComPtr<ID2D1GradientStopCollection> collection; ComPtr<ID2D1LinearGradientBrush> brush; ComPtr<ID2D1CommandList> shape; ComPtr<ID2D1Effect> blur;
    const D2D1_GRADIENT_STOP stops[]={{0,tint(-1.2f,1)},{.35f,tint(-.3f,1)},{.65f,tint(.6f,1)},{1,tint(1.5f,1)}};
    if (FAILED(context->CreateGradientStopCollection(stops,4,&collection)) ||
        FAILED(context->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(D2D1::Point2F(0,0),D2D1::Point2F(width,height)),collection.Get(),&brush)) ||
        FAILED(context->CreateCommandList(&shape)) || FAILED(context->CreateEffect(CLSID_D2D1GaussianBlur,&blur))) return false;
    ComPtr<ID2D1Image> previous; context->GetTarget(&previous);
    const auto frame=D2D1::RoundedRect(D2D1::RectF(0,0,width,height),6*scale,6*scale);
    context->SetTarget(shape.Get()); context->SetTransform(D2D1::Matrix3x2F::Identity());
    brush->SetOpacity(.8f); context->DrawRoundedRectangle(frame,brush.Get(),14*scale);
    if (FAILED(shape->Close())) return false;
    blur->SetInput(0,shape.Get()); blur->SetValue(D2D1_GAUSSIANBLUR_PROP_STANDARD_DEVIATION,16*scale);
    blur->SetValue(D2D1_GAUSSIANBLUR_PROP_BORDER_MODE,D2D1_BORDER_MODE_HARD);
    context->SetTarget(previous.Get()); context->SetTransform(D2D1::Matrix3x2F::Translation(x,y));
    context->DrawImage(blur.Get());
    brush->SetOpacity(.55f); context->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(.75f*scale,.75f*scale,width-.75f*scale,height-.75f*scale),6*scale,6*scale),brush.Get(),1.5f*scale);
    return true;
  });
  if (!line || !trail || !edge) return false;

  // Animation curves: arbitrary easing as piecewise cubic Hermite segments.
  const auto curve=[&](float duration,const auto& f) -> ComPtr<IDCompositionAnimation> {
    ComPtr<IDCompositionAnimation> a; if (FAILED(device->CreateAnimation(&a))) return nullptr;
    constexpr int segments=12; const float h=duration/segments,e=h*.01f;
    const auto slope=[&](float t) { return (f(std::min(duration,t+e))-f(std::max(0.f,t-e)))/(std::min(duration,t+e)-std::max(0.f,t-e)); };
    for (int i=0;i<segments;++i) {
      const float t0=i*h,t1=t0+h,v0=f(t0),v1=f(t1),d0=slope(t0),d1=slope(t1);
      if (FAILED(a->AddCubic(t0,v0,d0,(3*(v1-v0)/h-2*d0-d1)/h,(d0+d1-2*(v1-v0)/h)/(h*h)))) return nullptr;
    }
    return SUCCEEDED(a->End(duration,f(duration))) ? a : nullptr;
  };
  const auto quart=[](float t) { t=std::clamp(t,0.f,1.f); return 1-std::pow(1-t,4.f); };
  const auto smooth=[](float t) { t=std::clamp(t,0.f,1.f); return t*t*(3-2*t); };
  // Opacity envelope: rise over |in|, hold, fall from |out| to |end| (seconds).
  const auto envelope=[&](float in,float out,float end,float peak) {
    return [=](float t) { return peak*(t<in ? smooth(t/in) : t<out ? 1.f : 1-smooth((t-out)/(end-out))); };
  };

  ComPtr<IDCompositionVisual> container; const auto clip=Rounded(device,width,height,6*scale);
  if (!clip || FAILED(device->CreateVisual(&container))) return false;
  container->SetOffsetX(static_cast<float>(area.left)); container->SetOffsetY(static_cast<float>(area.top)); container->SetClip(clip.Get());
  constexpr float duration=.62f;
  const float line_height=10*scale,trail_height=std::min(.4f*height,260*scale);
  ComPtr<IDCompositionVisual> sweep,line_visual,trail_visual,edge_visual; ComPtr<IDCompositionScaleTransform> line_size,trail_size;
  ComPtr<IDCompositionEffectGroup> sweep_opacity,edge_opacity;
  // Travel: from just above the area to just below it, fast then settling.
  const auto travel=curve(duration,[&](float t) { return -line_height+(height+trail_height+line_height)*quart(t/duration); });
  const auto sweep_fade=curve(duration,envelope(.05f,.38f,duration,.6f));
  // Only the border glow is shown (user choice), kept muted.
  const auto edge_fade=curve(.7f,envelope(.16f,.3f,.7f,.22f));
  bool built=travel && sweep_fade && edge_fade && SUCCEEDED(device->CreateVisual(&sweep)) && SUCCEEDED(device->CreateVisual(&line_visual)) &&
    SUCCEEDED(device->CreateVisual(&trail_visual)) && SUCCEEDED(device->CreateVisual(&edge_visual)) && SUCCEEDED(device->CreateScaleTransform(&line_size)) &&
    SUCCEEDED(device->CreateScaleTransform(&trail_size)) && SUCCEEDED(device->CreateEffectGroup(&sweep_opacity)) && SUCCEEDED(device->CreateEffectGroup(&edge_opacity));
  if (built) {
    edge_visual->SetContent(edge.Get()); edge_opacity->SetOpacity(edge_fade.Get()); edge_visual->SetEffect(edge_opacity.Get());
    line_size->SetScaleX(width/512); line_size->SetScaleY(line_height/32); line_visual->SetContent(line.Get()); line_visual->SetTransform(line_size.Get());
    line_visual->SetOffsetY(-line_height/2);
    trail_size->SetScaleX(width/512); trail_size->SetScaleY(trail_height/256); trail_visual->SetContent(trail.Get()); trail_visual->SetTransform(trail_size.Get());
    trail_visual->SetOffsetY(-trail_height);
    sweep->SetOffsetY(travel.Get()); sweep_opacity->SetOpacity(sweep_fade.Get()); sweep->SetEffect(sweep_opacity.Get());
    built=SUCCEEDED(sweep->AddVisual(trail_visual.Get(),FALSE,nullptr)) && SUCCEEDED(sweep->AddVisual(line_visual.Get(),TRUE,nullptr)) &&
      SUCCEEDED(container->AddVisual(edge_visual.Get(),FALSE,nullptr));
  }
  if (!built || FAILED(root->AddVisual(container.Get(),TRUE,nullptr))) return false;
  pulse_device_=device; pulse_root_=root; pulse_=container; pulse_surfaces_=std::move(surfaces); return SUCCEEDED(device->Commit());
}
void LinkPeekScene::EndPulse() {
  if (pulse_root_ && pulse_) pulse_root_->RemoveVisual(pulse_.Get());
  if (pulse_device_ && pulse_) pulse_device_->Commit();
  pulse_device_.Reset(); pulse_root_.Reset(); pulse_.Reset(); pulse_surfaces_.clear();
}
void LinkPeekScene::Hover(int index) { if (hover_!=index && controls_) { hover_=index; if (PaintControls() && device_) device_->Commit(); } }
int LinkPeekScene::BeginReturn() {
  // Before the site is revealed the neutral card returns; no frame needed.
  if (!Active() || (revealed_ && !closing_frame_) || !NativeMotionEnabled() || returning_) return 0;
  // The card's own time (it waits entry_delay_ms_ before growing).
  const float elapsed=Opening() ? std::max(0.f,static_cast<float>(kEntryMs-(opening_until_-GetTickCount64()))/1000.f) : 1.f;
  const float t=std::clamp(elapsed/kPeekGrowth,0.f,1.f),s=t*t*(3-2*t);
  const float phase=std::clamp((elapsed-kPeekGrowth)/kPeekSettle,0.f,1.f);
  const float entry=elapsed<kPeekGrowth ? .025f+(.975f+bounce_)*s : 1+bounce_-bounce_*phase*phase*(3-2*phase);
  constexpr float back=kReturnMs/1000.f;
  const auto shrink=Flight(device_.Get(),entry,0,back);
  const float depth_t=std::clamp(static_cast<float>(GetTickCount64()-depth_started_)/kReturnMs,0.f,1.f);
  const float eased=depth_ready_ ? 1-(1-depth_t)*(1-depth_t)*(1-depth_t) : 0.f;
  const auto restore=EaseNativeMotion(device_.Get(),1-.015f*eased,1,back),buttons=EaseNativeMotion(device_.Get(),1,0,.08f);
  // The dimmer leaves with the card: no abrupt brightening when the picture goes.
  const auto undim=EaseNativeMotion(device_.Get(),kVeil*eased,0,back);
  ComPtr<IDCompositionAnimation> fade; constexpr float tail=.12f;
  if (!shrink || !restore || !buttons || !undim || FAILED(device_->CreateAnimation(&fade)) || FAILED(fade->AddCubic(0,1,0,0,0)) ||
      FAILED(fade->AddCubic(back-tail,1,0,-3/(tail*tail),2/(tail*tail*tail))) || FAILED(fade->End(back,0))) return 0;
  if (IsWindow(preview_.handle)) ShowWindow(preview_.handle,SW_HIDE);
  picture_->SetContent(revealed_ ? closing_frame_.Get() : placeholder_.Get()); live_opacity_->SetOpacity(revealed_ ? 0.f : 1.f);
  front_scale_->SetScaleX(shrink.Get()); front_scale_->SetScaleY(shrink.Get()); source_scale_->SetScaleX(restore.Get()); source_scale_->SetScaleY(restore.Get());
  front_opacity_->SetOpacity(fade.Get()); controls_opacity_->SetOpacity(buttons.Get()); dim_opacity_->SetOpacity(undim.Get());
  if (live_center_opacity_) live_center_opacity_->SetOpacity(1.f);
  returning_=SUCCEEDED(device_->Commit()); opening_until_=0; return returning_ ? kReturnMs+32 : 0;
}
ComPtr<IDCompositionAnimation> LinkPeekScene::Eased(float from,float to,float seconds) {
  // gfx::Tween::FAST_OUT_SLOW_IN (cubic-bezier .4,0,.2,1) as 24 linear pieces.
  const auto ease=[](float x) {
    float lo=0,hi=1,t=x;
    for (int i=0;i<24;++i) { t=(lo+hi)/2; const float u=1-t; ((3*u*u*t*.4f+3*u*t*t*.2f+t*t*t)<x ? lo : hi)=t; }
    const float u=1-t; return 3*u*t*t+t*t*t;
  };
  ComPtr<IDCompositionAnimation> a; if (FAILED(device_->CreateAnimation(&a))) return nullptr;
  constexpr int pieces=24;
  for (int i=0;i<pieces;++i) {
    const float t0=seconds*i/pieces,t1=seconds*(i+1)/pieces;
    const float v0=from+(to-from)*ease(static_cast<float>(i)/pieces),v1=from+(to-from)*ease(static_cast<float>(i+1)/pieces);
    if (FAILED(a->AddCubic(t0,v0,(v1-v0)/(t1-t0),0,0))) return nullptr;
  }
  return SUCCEEDED(a->End(seconds,to)) ? a : nullptr;
}
// A |size| square of |color| outside a quarter circle (antialiased); |quadrant|
// 0..3 = the page corner it rounds: top-left, top-right, bottom-right, bottom-left.
ComPtr<IDCompositionSurface> LinkPeekScene::Corner(int size,COLORREF color,int quadrant) {
  auto surface=Solid(size,size,D2D1::ColorF(GetRValue(color)/255.f,GetGValue(color)/255.f,GetBValue(color)/255.f,1));
  if (!surface) return nullptr;
  ComPtr<IDXGISurface> dxgi; ComPtr<ID2D1Bitmap1> target; POINT offset{}; ComPtr<ID2D1SolidColorBrush> clear;
  if (FAILED(surface->BeginDraw(nullptr,__uuidof(IDXGISurface),&dxgi,&offset))) return nullptr;
  const auto properties=D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET|D2D1_BITMAP_OPTIONS_CANNOT_DRAW,D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED));
  bool ok=SUCCEEDED(context_->CreateBitmapFromDxgiSurface(dxgi.Get(),&properties,&target)) && SUCCEEDED(context_->CreateSolidColorBrush(D2D1::ColorF(0,0,0,0),&clear));
  if (ok) {
    const float s=static_cast<float>(size);
    context_->SetTarget(target.Get()); context_->SetTransform(D2D1::Matrix3x2F::Translation(static_cast<float>(offset.x),static_cast<float>(offset.y)));
    context_->BeginDraw(); context_->PushAxisAlignedClip(D2D1::RectF(0,0,s,s),D2D1_ANTIALIAS_MODE_ALIASED);
    context_->Clear(D2D1::ColorF(GetRValue(color)/255.f,GetGValue(color)/255.f,GetBValue(color)/255.f,1));
    const float cx=quadrant==0 || quadrant==3 ? s : 0,cy=quadrant<2 ? s : 0;
    context_->SetPrimitiveBlend(D2D1_PRIMITIVE_BLEND_COPY);
    context_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx,cy),s,s),clear.Get());
    context_->SetPrimitiveBlend(D2D1_PRIMITIVE_BLEND_SOURCE_OVER);
    context_->PopAxisAlignedClip(); ok=SUCCEEDED(context_->EndDraw()); context_->SetTarget(nullptr);
  }
  ok=SUCCEEDED(surface->EndDraw()) && ok; return ok ? surface : nullptr;
}
COLORREF LinkPeekScene::SourceVeilColor(HWND parent) {
  const auto tint=ThemeTint(parent,0,1);
  return RGB(std::lround(tint.r*255*.28f),std::lround(tint.g*255*.28f),std::lround(tint.b*255*.28f));
}
bool LinkPeekScene::BuildLiveFrame() {
  if (live_frame_ && scene_) scene_->RemoveVisual(live_frame_.Get());
  live_frame_.Reset(); live_surfaces_.clear();
#if defined(RED_CEF_PAGE_CONTAINER)
  // The native Chrome container owns its clip/veil. No painted source edges,
  // corner patches or shell dimmer may overlap it, including during return.
  live_center_opacity_.Reset();
  return true;
#endif
  ComPtr<IDCompositionVisual> frame,center; ComPtr<IDCompositionEffectGroup> center_opacity;
  if (FAILED(device_->CreateVisual(&frame)) || FAILED(device_->CreateVisual(&center)) || FAILED(device_->CreateEffectGroup(&center_opacity))) return false;
  center_opacity->SetOpacity(revealed_ && !returning_ ? 0.f : 1.f); center->SetEffect(center_opacity.Get());
  const auto background=Canvas(true,false); if (!background) return false;
  live_surfaces_.push_back(background);
  const float seconds=live_ms_/1000.f,overlap=1.5f*scale_,radius=std::round(6*scale_);
  const bool moving=live_ms_>0 && NativeMotionEnabled();
  // A value that follows the pages' scale: base + factor * (1 - scale).
  const auto follow=[&](float base,float factor) -> ComPtr<IDCompositionAnimation> {
    return moving ? Eased(base+factor*(1-live_from_),base+factor*(1-live_to_),seconds) : nullptr;
  };
  // |apply| takes a float or an animation (the DirectComposition setters are overloaded).
  const auto set=[&](const auto& apply,float base,float factor) {
    if (auto a=follow(base,factor)) apply(a.Get()); else apply(base+factor*(1-live_to_));
  };
  const float card_left=static_cast<float>(card_.left-area_.left),card_right=static_cast<float>(card_.right-area_.left);
  bool ok=true;
  for (const auto& source:sources_) {
    RECT window{}; if (!GetWindowRect(source.handle,&window)) continue;
    POINT at{window.left,window.top}; ScreenToClient(parent_,&at);
    const float l=static_cast<float>(at.x-area_.left),t=static_cast<float>(at.y-area_.top);
    const float w=static_cast<float>(window.right-window.left),h=static_cast<float>(window.bottom-window.top),r=l+w,b=t+h;
    const float max_dx=w*.5f*(1-std::min(live_from_,live_to_))+overlap,max_dy=h*.5f*(1-std::min(live_from_,live_to_))+overlap;
    // One edge piece: a static clip (its widest extent, outside the card,
    // which is a live window below this frame at rest) holding a visual whose
    // clip edge moves with the page.
    const auto piece=[&](float x0,float y0,float x1,float y1,int moving_edge,float base,float factor) {
      for (int side=0;side<3;++side) {
        // Split around the card's columns: left of it, right of it, and under it.
        const float a=side==2 ? std::max(x0,card_left) : side ? std::max(x0,card_right) : x0;
        const float z=side==2 ? std::min(x1,card_right) : side ? x1 : std::min(x1,card_left);
        if (z-a<.5f) continue;
        ComPtr<IDCompositionVisual> holder,band; ComPtr<IDCompositionRectangleClip> fixed,moving_clip;
        if (FAILED(device_->CreateVisual(&holder)) || FAILED(device_->CreateVisual(&band)) ||
            FAILED(device_->CreateRectangleClip(&fixed)) || FAILED(device_->CreateRectangleClip(&moving_clip))) { ok=false; return; }
        fixed->SetLeft(a); fixed->SetRight(z); fixed->SetTop(y0); fixed->SetBottom(y1); holder->SetClip(fixed.Get());
        moving_clip->SetLeft(x0); moving_clip->SetRight(x1); moving_clip->SetTop(y0); moving_clip->SetBottom(y1);
        auto* clip=moving_clip.Get();
        switch (moving_edge) {
          case 0: set([clip](auto v) { clip->SetRight(v); },base,factor); break;
          case 1: set([clip](auto v) { clip->SetLeft(v); },base,factor); break;
          case 2: set([clip](auto v) { clip->SetBottom(v); },base,factor); break;
          default: set([clip](auto v) { clip->SetTop(v); },base,factor);
        }
        band->SetContent(background.Get()); band->SetClip(clip);
        ok=ok && SUCCEEDED(holder->AddVisual(band.Get(),FALSE,nullptr)) && SUCCEEDED((side==2 ? center : frame)->AddVisual(holder.Get(),TRUE,nullptr));
      }
    };
    piece(l,t,l+max_dx,b,0,l+overlap,w*.5f);   // left edge: its right side moves in
    piece(r-max_dx,t,r,b,1,r-overlap,-w*.5f);  // right edge
    piece(l,t,r,t+max_dy,2,t+overlap,h*.5f);   // top edge
    piece(l,b-max_dy,r,b,3,b-overlap,-h*.5f);  // bottom edge
    // Rounded inner corners, riding on the moving edges (not under the card).
    const int size=static_cast<int>(radius);
    for (int q=0;q<4 && size>0;++q) {
      const bool left=q==0 || q==3,top=q<2;
      // The frame overlaps the page's edge; the rounding starts where the page shows.
      const float x=left ? l+overlap : r-size-overlap,y=top ? t+overlap : b-size-overlap;
      if ((left ? l+max_dx+size : r) > card_left && (left ? l : r-max_dx-size) < card_right) continue;
      const auto color=WorkspaceBackgroundAt(area_.left+x+size/2.,area_.top+y+size/2.,area_.right,area_.bottom,parent_);
      auto surface=Corner(size,color,q); ComPtr<IDCompositionVisual> corner;
      if (!surface || FAILED(device_->CreateVisual(&corner))) { ok=false; continue; }
      live_surfaces_.push_back(surface); corner->SetContent(surface.Get());
      const float sx=left ? w*.5f : -w*.5f,sy=top ? h*.5f : -h*.5f;
      auto* visual=corner.Get();
      set([visual](auto v) { visual->SetOffsetX(v); },x,sx);
      set([visual](auto v) { visual->SetOffsetY(v); },y,sy);
      ok=ok && SUCCEEDED(frame->AddVisual(corner.Get(),FALSE,nullptr));  // front
    }
  }
  // The live Peek window's rounded corners (its region, 6 DIP) uncover the
  // page edge under them: background patches there, under the animated card.
  const int card_size=static_cast<int>(std::round(6*scale_));
  for (int q=0;q<4 && card_size>0;++q) {
    const bool left=q==0 || q==3,top=q<2;
    const float x=left ? card_left : card_right-card_size;
    const float y=static_cast<float>(top ? card_.top-area_.top : card_.bottom-area_.top-card_size);
    const auto color=WorkspaceBackgroundAt(area_.left+x+card_size/2.,area_.top+y+card_size/2.,area_.right,area_.bottom,parent_);
    auto surface=Corner(card_size,color,q); ComPtr<IDCompositionVisual> patch;
    if (!surface || FAILED(device_->CreateVisual(&patch))) { ok=false; continue; }
    live_surfaces_.push_back(surface); patch->SetContent(surface.Get()); patch->SetOffsetX(x); patch->SetOffsetY(y);
    ok=ok && SUCCEEDED(frame->AddVisual(patch.Get(),FALSE,nullptr));  // front
  }
  // The veil (dimmer and theme hues, as over the source pictures): over the
  // pages, behind the frame; it deepens with their depth, on the same curve.
  if (dim_surface_) {
    const float depth=1-kLiveSourceScale;
    const auto veil_at=[&](float scale) { return std::clamp(kVeil*(1-scale)/depth,0.f,kVeil); };
    const float w=static_cast<float>(area_.right-area_.left),h=static_cast<float>(area_.bottom-area_.top);
    for (int side=0;side<3;++side) {
      const float a=side==2 ? card_left : side ? card_right : 0.f,z=side==2 ? card_right : side ? w : card_left;
      if (z-a<.5f) continue;
      ComPtr<IDCompositionVisual> veil; ComPtr<IDCompositionRectangleClip> clip; ComPtr<IDCompositionEffectGroup> fade;
      if (FAILED(device_->CreateVisual(&veil)) || FAILED(device_->CreateRectangleClip(&clip)) || FAILED(device_->CreateEffectGroup(&fade))) { ok=false; break; }
      clip->SetLeft(a); clip->SetRight(z); clip->SetTop(0.f); clip->SetBottom(h);
      veil->SetContent(dim_surface_.Get()); veil->SetClip(clip.Get());
      if (moving) { if (auto v=Eased(veil_at(live_from_),veil_at(live_to_),seconds)) fade->SetOpacity(v.Get()); }
      else fade->SetOpacity(veil_at(live_to_));
      veil->SetEffect(fade.Get());
      ok=ok && SUCCEEDED((side==2 ? center : frame)->AddVisual(veil.Get(),TRUE,nullptr));  // behind the frame
    }
  }
  ok=ok && SUCCEEDED(frame->AddVisual(center.Get(),TRUE,nullptr));  // behind the corners
  frame->SetOffsetX(static_cast<float>(area_.left)); frame->SetOffsetY(static_cast<float>(area_.top));
  if (!ok || !scene_) return ok;
  live_frame_=frame; live_center_opacity_=center_opacity;
  // DirectComposition: a null reference with insertAbove TRUE is behind all
  // siblings (FALSE is in front). The frame is behind the card.
  return SUCCEEDED(scene_->AddVisual(frame.Get(),TRUE,nullptr));
}
void LinkPeekScene::LiveDepth(float from,float to,int ms) {
  live_from_=from; live_to_=to; live_ms_=ms;
  if (!live_ || !scene_ || !device_) return;
  if (BuildLiveFrame()) device_->Commit();
}
void LinkPeekScene::End() {
  if (root_ && scene_) root_->RemoveVisual(scene_.Get());
  if (source_root_ && source_scene_) source_root_->RemoveVisual(source_scene_.Get());
  if (device_) device_->Commit();
  sources_.clear(); preview_={}; scene_.Reset(); scene_clip_.Reset(); front_.Reset(); picture_.Reset(); controls_.Reset();
  source_picture_.Reset(); background_.Reset(); source_surface_.Reset(); background_surface_.Reset();
  placeholder_.Reset(); shadow_.Reset(); controls_surface_.Reset(); closing_frame_.Reset(); source_scale_.Reset(); front_scale_.Reset();
  shade_.Reset(); front_opacity_.Reset(); controls_opacity_.Reset(); live_opacity_.Reset(); dim_opacity_.Reset(); dim_surface_.Reset(); source_scene_.Reset(); source_root_.Reset(); source_target_.Reset();
  live_frame_.Reset(); live_center_opacity_.Reset(); live_surfaces_.clear(); live_from_=live_to_=1; live_ms_=0;
  root_.Reset(); context_.Reset(); device_.Reset();
  if (source_host_ && IsWindow(source_host_)) DestroyWindow(source_host_);
  source_host_=parent_=nullptr; opening_until_=depth_started_=0; entry_delay_ms_=0; hover_=-1; returning_=page_ready_=revealed_=depth_ready_=faded_=live_=false;
}
}  // namespace red
