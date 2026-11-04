#include "windows/sidebar_shadow.h"
#include <d2d1effects.h>
#include <algorithm>
#include <cmath>

using Microsoft::WRL::ComPtr;

namespace red {
int SidebarShadow::Margin(float scale) { return std::max(1, static_cast<int>(std::lround(24 * scale))); }

bool SidebarShadow::Init(IDCompositionDevice* device, ID2D1DeviceContext* context, IDCompositionVisual* root) {
  if (!device || !context || !root) return false;
  if (visual_ && device_.Get() == device && root_.Get() == root) return true;
  Reset(); device_ = device; context_ = context; root_ = root;
  if (FAILED(device_->CreateVisual(&visual_)) || FAILED(device_->CreateEffectGroup(&opacity_)) ||
      FAILED(visual_->SetEffect(opacity_.Get())) || FAILED(opacity_->SetOpacity(0.f)) ||
      FAILED(root_->AddVisual(visual_.Get(), TRUE, nullptr))) { Reset(); return false; }
  opacity_value_ = 0;
  return true;
}

bool SidebarShadow::Size(IDCompositionDevice* device, ID2D1DeviceContext* context,
                         IDCompositionVisual* root, int width, int height, float scale) {
  if (width < 1 || height < 1 || scale <= 0 || !Init(device, context, root)) return false;
  if (width == width_ && height == height_ && scale == scale_ && surface_) return true;
  const int margin = Margin(scale), w = width + margin, h = height + 2 * margin;
  if (w > 16384 || h > 16384) return false;
  ComPtr<IDCompositionSurface> surface;
  if (FAILED(device_->CreateSurface(w, h, DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_ALPHA_MODE_PREMULTIPLIED, &surface))) return false;
  ComPtr<IDXGISurface> dxgi; POINT offset{};
  if (FAILED(surface->BeginDraw(nullptr, __uuidof(IDXGISurface), &dxgi, &offset))) return false;
  const auto properties = D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
      D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
  ComPtr<ID2D1Bitmap1> bitmap; ComPtr<ID2D1SolidColorBrush> brush;
  ComPtr<ID2D1CommandList> shape; ComPtr<ID2D1Effect> blur;
  bool ok = SUCCEEDED(context_->CreateBitmapFromDxgiSurface(dxgi.Get(), &properties, &bitmap)) &&
      SUCCEEDED(context_->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0, .28f), &brush)) &&
      SUCCEEDED(context_->CreateCommandList(&shape)) && SUCCEEDED(context_->CreateEffect(CLSID_D2D1GaussianBlur, &blur));
  if (ok) {
    context_->SetTarget(shape.Get()); context_->SetTransform(D2D1::Matrix3x2F::Identity()); context_->BeginDraw();
    const float y = static_cast<float>(margin) + 3 * scale;
    context_->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(0, y, static_cast<float>(width), y + height),
        6 * scale, 6 * scale), brush.Get());
    ok = SUCCEEDED(context_->EndDraw()) && SUCCEEDED(shape->Close());
    blur->SetInput(0, shape.Get());
    ok = SUCCEEDED(blur->SetValue(D2D1_GAUSSIANBLUR_PROP_STANDARD_DEVIATION, 7.f * scale)) && ok;
    context_->SetTarget(bitmap.Get()); context_->BeginDraw();
    context_->SetTransform(D2D1::Matrix3x2F::Translation(static_cast<float>(offset.x), static_cast<float>(offset.y)));
    context_->PushAxisAlignedClip(D2D1::RectF(0, 0, static_cast<float>(w), static_cast<float>(h)), D2D1_ANTIALIAS_MODE_ALIASED);
    context_->Clear(D2D1::ColorF(0, 0, 0, 0)); context_->DrawImage(blur.Get());
    // The root's upper composition tree is above both site and sidebar HWNDs.
    // Remove the panel interior so only its outside shadow is painted.
    context_->SetPrimitiveBlend(D2D1_PRIMITIVE_BLEND_COPY);
    brush->SetColor(D2D1::ColorF(0, 0, 0, 0));
    context_->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(0, static_cast<float>(margin),
        static_cast<float>(width), static_cast<float>(margin + height)), 6 * scale, 6 * scale), brush.Get());
    context_->SetPrimitiveBlend(D2D1_PRIMITIVE_BLEND_SOURCE_OVER);
    context_->PopAxisAlignedClip(); ok = SUCCEEDED(context_->EndDraw()) && ok;
  }
  context_->SetTarget(nullptr);
  if (FAILED(surface->EndDraw())) ok = false;
  if (!ok || FAILED(visual_->SetContent(surface.Get())) || FAILED(device_->Commit())) return false;
  surface_ = surface; width_ = width; height_ = height; scale_ = scale;
  return true;
}

void SidebarShadow::Place(int x, int y, double progress) {
  // Soften the last part of the shadow too: its blur must not remain as a
  // dark stripe when only a sliver of the actual panel is still visible.
  const float opacity = static_cast<float>(std::clamp(progress * 3, 0.0, 1.0));
  if (!visual_ || !device_ || !opacity_) return;
  const int top = y - Margin(scale_);
  if (opacity == opacity_value_ && x == x_ && top == y_) return;
  if (SUCCEEDED(visual_->SetOffsetX(static_cast<float>(x))) && SUCCEEDED(visual_->SetOffsetY(static_cast<float>(top))) &&
      SUCCEEDED(opacity_->SetOpacity(opacity)) && SUCCEEDED(device_->Commit())) {
    opacity_value_ = opacity; x_ = x; y_ = top;
  }
}

void SidebarShadow::Hide() {
  if (device_ && opacity_ && opacity_value_ != 0 && SUCCEEDED(opacity_->SetOpacity(0.f)) && SUCCEEDED(device_->Commit()))
    opacity_value_ = 0;
}

void SidebarShadow::Reset() {
  if (root_ && visual_) root_->RemoveVisual(visual_.Get());
  if (device_) device_->Commit();
  surface_.Reset(); opacity_.Reset(); visual_.Reset(); root_.Reset();
  device_.Reset(); context_.Reset();
  width_ = height_ = 0; scale_ = 0; opacity_value_ = -1; x_ = y_ = 0;
}
}  // namespace red
