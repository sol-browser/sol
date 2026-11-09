#include "windows/page_corners.h"
#include "windows/motion.h"
#include <d2d1effects.h>
#include <cmath>
#include <algorithm>

using Microsoft::WRL::ComPtr;

namespace red {
namespace {
constexpr float kShadowBlurDip = 10.f, kShadowOffsetDip = 6.f;
ComPtr<IDCompositionSurface> Shadow(IDCompositionDevice* device, ID2D1DeviceContext* context,
                                   int width, int height, float scale, int margin) {
  ComPtr<IDCompositionSurface> surface;
  if (FAILED(device->CreateSurface(width + 2 * margin, height + 2 * margin,
      DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_ALPHA_MODE_PREMULTIPLIED, &surface))) return nullptr;
  ComPtr<IDXGISurface> dxgi; POINT offset{};
  if (FAILED(surface->BeginDraw(nullptr, __uuidof(IDXGISurface), &dxgi, &offset))) return nullptr;
  ComPtr<ID2D1Bitmap1> target;
  const auto props = D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
      D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
  ComPtr<ID2D1SolidColorBrush> brush; ComPtr<ID2D1CommandList> shape; ComPtr<ID2D1Effect> blur;
  bool ok = SUCCEEDED(context->CreateBitmapFromDxgiSurface(dxgi.Get(), &props, &target)) &&
      SUCCEEDED(context->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0, .32f), &brush)) &&
      SUCCEEDED(context->CreateCommandList(&shape)) && SUCCEEDED(context->CreateEffect(CLSID_D2D1GaussianBlur, &blur));
  if (ok) {
    context->SetTarget(shape.Get()); context->SetTransform(D2D1::Matrix3x2F::Identity()); context->BeginDraw();
    const float x = static_cast<float>(margin), y = x + kShadowOffsetDip * scale;
    context->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(x, y, x + width, y + height), 8 * scale, 8 * scale), brush.Get());
    ok = SUCCEEDED(context->EndDraw()) && SUCCEEDED(shape->Close());
    blur->SetInput(0, shape.Get());
    ok = SUCCEEDED(blur->SetValue(D2D1_GAUSSIANBLUR_PROP_STANDARD_DEVIATION, kShadowBlurDip * scale)) && ok;
    context->SetTarget(target.Get());
    context->SetTransform(D2D1::Matrix3x2F::Translation(static_cast<float>(offset.x), static_cast<float>(offset.y)));
    context->BeginDraw(); context->Clear(D2D1::ColorF(0, 0, 0, 0));
    context->DrawImage(blur.Get()); ok = SUCCEEDED(context->EndDraw()) && ok;
  }
  context->SetTarget(nullptr);
  if (FAILED(surface->EndDraw())) ok = false;
  return ok ? surface : nullptr;
}
}
bool PageCorners::BeginPopupAnimation(HWND parent, HWND popup, bool bottom, float origin_x, bool closing, float from_scale, float origin_y) {
  if (!IsWindow(popup) || !(GetWindowLongPtr(popup, GWL_EXSTYLE) & WS_EX_LAYERED) || !Init(parent)) return false;
  const auto ease = closing ? ClosePopupMotion(device_.Get(), std::clamp(from_scale, 0.f, 1.f)) :
      EaseNativeMotion(device_.Get(), 0.f, 1.f, kPopupOpeningMs / 1000.f);
  if (!ease) return false;
  // Reverse an in-flight entry without rebuilding its tree or committing a
  // frame with the full-size card. The viewport/content stay untouched.
  if (closing && popup_source_ == popup && popup_growth_ && popup_shadow_opacity_)
    return SUCCEEDED(popup_growth_->SetScaleX(ease.Get())) && SUCCEEDED(popup_growth_->SetScaleY(ease.Get())) &&
        SUCCEEDED(popup_shadow_opacity_->SetOpacity(1.f)) && SUCCEEDED(device_->Commit());
  RECT rect{}; if (!GetWindowRect(popup, &rect)) return false;
  const int width = rect.right - rect.left, height = rect.bottom - rect.top;
  if (width < 1 || height < 1 || width > 4096 || height > 4096) return false;
  POINT origin{rect.left, rect.top}; if (!ScreenToClient(parent, &origin)) return false;
  EndPopupAnimation(popup_source_);
  ComPtr<IUnknown> bitmap;
  ComPtr<IDCompositionVisual> anchor, picture, shadow;
  ComPtr<IDCompositionEffectGroup> shadow_opacity;
  ComPtr<IDCompositionScaleTransform> growth;
  ComPtr<IDCompositionRectangleClip> clip;
  const float scale = GetDpiForWindow(popup) / 96.f;
  const int margin = static_cast<int>(std::lround(kPopupShadowMarginDip * scale));
  const auto shadow_surface = Shadow(device_.Get(), d2d_.Get(), width, height, scale, margin);
  if (!shadow_surface || !ease || FAILED(device_->CreateSurfaceFromHwnd(popup, &bitmap)) ||
      FAILED(device_->CreateVisual(&anchor)) || FAILED(device_->CreateVisual(&picture)) ||
      FAILED(device_->CreateScaleTransform(&growth)) || FAILED(device_->CreateRectangleClip(&clip)) ||
      FAILED(picture->SetContent(bitmap.Get()))) return false;
  const float radius = 8.f * scale;
  clip->SetLeft(0.f); clip->SetTop(0.f); clip->SetRight(static_cast<float>(width)); clip->SetBottom(static_cast<float>(height));
  clip->SetTopLeftRadiusX(radius); clip->SetTopLeftRadiusY(radius);
  clip->SetTopRightRadiusX(radius); clip->SetTopRightRadiusY(radius);
  clip->SetBottomLeftRadiusX(radius); clip->SetBottomLeftRadiusY(radius);
  clip->SetBottomRightRadiusX(radius); clip->SetBottomRightRadiusY(radius);
  picture->SetClip(clip.Get());
  if (FAILED(device_->CreateVisual(&shadow)) || FAILED(device_->CreateEffectGroup(&shadow_opacity)) ||
      FAILED(shadow->SetEffect(shadow_opacity.Get())) || FAILED(shadow_opacity->SetOpacity(1.f)) ||
      FAILED(shadow->SetContent(shadow_surface.Get())) ||
      FAILED(shadow->SetOffsetX(static_cast<float>(-margin))) || FAILED(shadow->SetOffsetY(static_cast<float>(-margin))) ||
      FAILED(anchor->AddVisual(shadow.Get(), TRUE, nullptr))) return false;
  growth->SetCenterX(width * std::clamp(origin_x, 0.f, 1.f));
  growth->SetCenterY(height * (origin_y >= 0.f ? std::clamp(origin_y, 0.f, 1.f) : bottom ? 1.f : 0.f));
  if (FAILED(growth->SetScaleX(ease.Get())) || FAILED(growth->SetScaleY(ease.Get())) ||
      // With a null reference AddVisual(FALSE) is above all siblings; TRUE
      // inserts below all. Drawing the shadow above the bitmap darkens it.
      FAILED(anchor->SetTransform(growth.Get())) || FAILED(anchor->AddVisual(picture.Get(), FALSE, nullptr)) ||
      FAILED(anchor->SetOffsetX(static_cast<float>(origin.x))) || FAILED(anchor->SetOffsetY(static_cast<float>(origin.y))) ||
      FAILED(root_->AddVisual(anchor.Get(), FALSE, nullptr))) return false;
  popup_source_ = popup; popup_anchor_ = anchor; popup_picture_ = picture; popup_shadow_ = shadow;
  popup_shadow_opacity_ = shadow_opacity;
  popup_bitmap_ = bitmap; popup_growth_ = growth; popup_clip_ = clip; popup_shadow_surface_ = shadow_surface;
  if (FAILED(device_->Commit())) { EndPopupAnimation(popup); return false; }
  return true;
}
bool PageCorners::FadePopupShadow(HWND popup, int duration_ms) {
  if (popup_source_ != popup || !popup_shadow_opacity_ || duration_ms <= 0) return false;
  const auto fade = EaseNativeMotion(device_.Get(), 1.f, 0.f, duration_ms / 1000.f);
  return fade && SUCCEEDED(popup_shadow_opacity_->SetOpacity(fade.Get())) && SUCCEEDED(device_->Commit());
}
void PageCorners::EndPopupAnimation(HWND popup) {
  if (!popup_source_ || popup_source_ != popup) return;
  if (root_ && popup_anchor_) root_->RemoveVisual(popup_anchor_.Get());
  popup_anchor_.Reset(); popup_picture_.Reset(); popup_shadow_.Reset(); popup_bitmap_.Reset();
  popup_growth_.Reset(); popup_clip_.Reset(); popup_shadow_surface_.Reset(); popup_source_ = nullptr;
  popup_shadow_opacity_.Reset();
  if (device_) device_->Commit();
}
}  // namespace red
