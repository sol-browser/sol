#include "windows/page_corners.h"
#include <dcompanimation.h>
#include <wincodec.h>

using Microsoft::WRL::ComPtr;
namespace red {
namespace {
D2D1::ColorF Color(COLORREF c) { return D2D1::ColorF(GetRValue(c) / 255.f, GetGValue(c) / 255.f, GetBValue(c) / 255.f); }
}  // namespace
namespace {
ComPtr<IDCompositionAnimation> Settle(IDCompositionDevice* device, float from, float to, float duration) {
  ComPtr<IDCompositionAnimation> animation;
  const float d = to - from;
  if (FAILED(device->CreateAnimation(&animation)) ||
      FAILED(animation->AddCubic(0, from, 0, 3 * d / (duration * duration), -2 * d / (duration * duration * duration))) ||
      FAILED(animation->End(duration, to))) return nullptr;
  return animation;
}
}
bool PageCorners::BeginPageTransition(HWND parent, const RECT& area) {
  EndPageTransition();
  const int width = area.right - area.left, height = area.bottom - area.top;
  if (width < 1 || height < 1 || width > 8192 || height > 8192 || !Init(parent)) return false;
  if (!page_transition_) {
    ComPtr<IDCompositionVisual> anchor, picture;
    ComPtr<IDCompositionEffectGroup> opacity;
    ComPtr<IDCompositionRectangleClip> clip;
    if (FAILED(device_->CreateVisual(&anchor)) || FAILED(device_->CreateVisual(&picture)) ||
        FAILED(device_->CreateEffectGroup(&opacity)) || FAILED(picture->SetEffect(opacity.Get())) ||
        FAILED(device_->CreateRectangleClip(&clip)) || FAILED(picture->SetClip(clip.Get())) ||
        FAILED(anchor->AddVisual(picture.Get(), TRUE, nullptr)) ||
        FAILED(root_->AddVisual(anchor.Get(), FALSE, visuals_[0].Get()))) return false;
    page_transition_ = anchor; page_picture_ = picture; page_opacity_ = opacity; page_card_clip_ = clip;
  }
  page_width_ = width; page_height_ = height;
  const float radius = 6.f * GetDpiForWindow(parent) / 96.f;
  page_card_clip_->SetLeft(0.f); page_card_clip_->SetTop(0.f);
  page_card_clip_->SetRight(static_cast<float>(width)); page_card_clip_->SetBottom(static_cast<float>(height));
  page_card_clip_->SetTopLeftRadiusX(radius); page_card_clip_->SetTopLeftRadiusY(radius);
  page_card_clip_->SetTopRightRadiusX(radius); page_card_clip_->SetTopRightRadiusY(radius);
  page_card_clip_->SetBottomLeftRadiusX(radius); page_card_clip_->SetBottomLeftRadiusY(radius);
  page_card_clip_->SetBottomRightRadiusX(radius); page_card_clip_->SetBottomRightRadiusY(radius);
  page_transition_->SetOffsetX(static_cast<float>(area.left)); page_transition_->SetOffsetY(static_cast<float>(area.top));
  page_transition_->SetClip(D2D1::RectF(0, 0, static_cast<float>(width), static_cast<float>(height)));
  page_opacity_->SetOpacity(1.f);
  // Empty until the outgoing frame arrives. The old live HWND stays on top.
  return true;
}
ComPtr<IDCompositionSurface> PageCorners::DecodePageSnapshot(const void* png, size_t size, int width, int height) {
  if (!device_ || !png || !size || size > 16 * 1024 * 1024 || width < 1 || height < 1 ||
      static_cast<unsigned long long>(width) * height > 16 * 1024 * 1024) return nullptr;
  ComPtr<IWICImagingFactory> factory;
  ComPtr<IWICStream> stream;
  ComPtr<IWICBitmapDecoder> decoder;
  ComPtr<IWICBitmapFrameDecode> frame;
  ComPtr<IWICFormatConverter> converter;
  if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))) ||
      FAILED(factory->CreateStream(&stream)) ||
      FAILED(stream->InitializeFromMemory(static_cast<BYTE*>(const_cast<void*>(png)), static_cast<DWORD>(size))) ||
      FAILED(factory->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnLoad, &decoder)) ||
      FAILED(decoder->GetFrame(0, &frame))) return nullptr;
  UINT image_width = 0, image_height = 0;
  if (FAILED(frame->GetSize(&image_width, &image_height)) || !image_width || !image_height ||
      static_cast<unsigned long long>(image_width) * image_height > 16 * 1024 * 1024) return nullptr;
  if (FAILED(factory->CreateFormatConverter(&converter)) ||
      FAILED(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone,
          nullptr, 0, WICBitmapPaletteTypeCustom))) return nullptr;
  ComPtr<ID2D1Bitmap1> image;
  if (FAILED(d2d_->CreateBitmapFromWicBitmap(converter.Get(), nullptr, &image))) return nullptr;
  ComPtr<IDCompositionSurface> surface;
  if (FAILED(device_->CreateSurface(width, height, DXGI_FORMAT_B8G8R8A8_UNORM,
      DXGI_ALPHA_MODE_PREMULTIPLIED, &surface))) return nullptr;
  ComPtr<IDXGISurface> dxgi; POINT offset{};
  if (FAILED(surface->BeginDraw(nullptr, __uuidof(IDXGISurface), &dxgi, &offset))) return nullptr;
  ComPtr<ID2D1Bitmap1> target;
  const auto props = D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
      D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
  bool ok = SUCCEEDED(d2d_->CreateBitmapFromDxgiSurface(dxgi.Get(), &props, &target));
  if (ok) {
    d2d_->SetTarget(target.Get()); d2d_->BeginDraw();
    d2d_->SetTransform(D2D1::Matrix3x2F::Translation(static_cast<float>(offset.x), static_cast<float>(offset.y)));
    const auto bounds = D2D1::RectF(0, 0, static_cast<float>(width), static_cast<float>(height));
    d2d_->PushAxisAlignedClip(bounds, D2D1_ANTIALIAS_MODE_ALIASED); d2d_->Clear(Color(WorkspaceBackgroundStops(parent_)[1]));
    d2d_->DrawBitmap(image.Get(), bounds, 1.f, D2D1_INTERPOLATION_MODE_LINEAR);
    d2d_->PopAxisAlignedClip(); ok = SUCCEEDED(d2d_->EndDraw()); d2d_->SetTarget(nullptr);
  }
  if (FAILED(surface->EndDraw())) ok = false;
  return ok ? surface : nullptr;
}
bool PageCorners::HoldOutgoingPage(const void* png, size_t size) {
  if (!page_picture_) return false;
  const auto surface = DecodePageSnapshot(png, size, page_width_, page_height_);
  if (!surface || FAILED(page_picture_->SetContent(surface.Get())) || FAILED(page_opacity_->SetOpacity(1.f))) return false;
  page_snapshot_ = surface;
  // The old picture must cover the target before its live HWND is promoted.
  return SUCCEEDED(device_->Commit()) && SUCCEEDED(device_->WaitForCommitCompletion());
}
bool PageCorners::FadeOutgoingPage() {
  if (!page_snapshot_) return false;
  const auto opacity = Settle(device_.Get(), 1.f, 0.f, kPageTransitionDurationMs / 1000.f);
  if (!opacity || FAILED(page_opacity_->SetOpacity(opacity.Get()))) return false;
  return SUCCEEDED(device_->Commit());
}
void PageCorners::EndPageTransition() {
  if (!page_transition_) return;
  page_picture_->SetContent(nullptr); page_transition_->SetContent(nullptr);
  page_snapshot_.Reset();
  device_->Commit();
}
}  // namespace red
