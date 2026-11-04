#include "windows/page_corners.h"
#include "windows/motion.h"
#include <dwrite.h>
#include <dcompanimation.h>
#include <d2d1effects.h>
#include <algorithm>
#include <cmath>

using Microsoft::WRL::ComPtr;

namespace red {
namespace {
float TextWidth(IDWriteFactory* fonts, IDWriteTextFormat* format, const std::wstring& text) {
  ComPtr<IDWriteTextLayout> layout; DWRITE_TEXT_METRICS metrics{};
  if (text.empty() || FAILED(fonts->CreateTextLayout(text.c_str(), static_cast<UINT32>(text.size()), format, 4096.f, 64.f, &layout)) ||
      FAILED(layout->GetMetrics(&metrics))) return 0;
  return metrics.widthIncludingTrailingWhitespace;
}
}  // namespace
bool PageCorners::ShowNotification(HWND parent, const RECT& area, float scale,
                                  const std::wstring& title, const std::wstring& detail,
                                  const std::vector<std::wstring>& keys, const std::wstring& tail, bool animate) {
  if (!Init(parent)) return false;
  // An in-place update swaps the content in one commit (no blank frame).
  if (animate) HideNotification();
  int width = std::min(static_cast<int>(std::lround(384 * scale)),
                       static_cast<int>(area.right - area.left - 12 * scale));
  // Without a title it is a compact single-line toast (the full-screen hint),
  // as wide as its text (measured below).
  const bool single = title.empty();
  const int height = static_cast<int>(std::lround((single ? 54 : 80) * scale));
  if (width < 160 || area.bottom - area.top < height + 32 * scale) return false;
  if (!notification_) {
    ComPtr<IDCompositionVisual> anchor, motion;
    ComPtr<IDCompositionEffectGroup> opacity;
    ComPtr<IDCompositionScaleTransform> growth;
    if (FAILED(device_->CreateVisual(&anchor)) || FAILED(device_->CreateVisual(&motion)) ||
        FAILED(device_->CreateEffectGroup(&opacity)) || FAILED(motion->SetEffect(opacity.Get())) ||
        FAILED(device_->CreateScaleTransform(&growth)) || FAILED(motion->SetTransform(growth.Get())) ||
        FAILED(anchor->AddVisual(motion.Get(), TRUE, nullptr)) || FAILED(root_->AddVisual(anchor.Get(), TRUE, nullptr))) return false;
    notification_ = anchor; notification_motion_ = motion; notification_opacity_ = opacity; notification_growth_ = growth;
  }
  ComPtr<IDWriteFactory> fonts;
  ComPtr<IDWriteTextFormat> heading, body;
  if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(fonts.GetAddressOf()))) ||
      FAILED(fonts->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD,
          DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 12.5f * scale, L"en-US", &heading)) ||
      FAILED(fonts->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
          DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 11.f * scale, L"en-US", &body))) return false;
  ComPtr<IDWriteInlineObject> ellipsis;
  if (FAILED(fonts->CreateEllipsisTrimmingSign(body.Get(), &ellipsis))) return false;
  DWRITE_TRIMMING trimming{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
  for (auto* format : {heading.Get(), body.Get()}) {
    format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    format->SetTrimming(&trimming, ellipsis.Get());
  }
  // With |keys| the detail line reads "<detail> [key] [key] <tail>", the keys
  // drawn as caps; same card, place and size as every toast.
  ComPtr<IDWriteTextFormat> key_text;
  std::vector<float> key_w; float detail_w = 0;
  const float gap = 5 * scale, key_pad = 6 * scale;
  if (single) body->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
  if (!keys.empty()) {
    if (FAILED(fonts->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, 10.f * scale, L"en-US", &key_text))) return false;
    key_text->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER); key_text->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    detail_w = TextWidth(fonts.Get(), body.Get(), detail);
    for (const auto& key : keys) key_w.push_back(std::max(22 * scale, TextWidth(fonts.Get(), key_text.Get(), key) + 2 * key_pad));
  }
  if (single) {
    float content = (detail_w ? detail_w + gap : 0) + TextWidth(fonts.Get(), body.Get(), keys.empty() ? detail : tail);
    for (const float cap : key_w) content += cap + gap;
    width = std::min(width, static_cast<int>(std::lround(content + 56 * scale)));  // 14 shadow + 14 padding per side.
  }
  ComPtr<IDCompositionSurface> surface;
  if (FAILED(device_->CreateSurface(width, height, DXGI_FORMAT_B8G8R8A8_UNORM,
                                    DXGI_ALPHA_MODE_PREMULTIPLIED, &surface))) return false;
  ComPtr<IDXGISurface> dxgi_surface; POINT offset{};
  if (FAILED(surface->BeginDraw(nullptr, __uuidof(IDXGISurface), &dxgi_surface, &offset))) return false;
  ComPtr<ID2D1Bitmap1> bitmap;
  const auto props = D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
      D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
  bool ok = SUCCEEDED(d2d_->CreateBitmapFromDxgiSurface(dxgi_surface.Get(), &props, &bitmap));
  ComPtr<ID2D1CommandList> shadow;
  ComPtr<ID2D1Effect> blur;
  ComPtr<ID2D1SolidColorBrush> brush;
  const float w = static_cast<float>(width), h = static_cast<float>(height);
  const auto card = D2D1::RoundedRect(D2D1::RectF(14 * scale, 12 * scale, w - 14 * scale, h - 12 * scale), 10 * scale, 10 * scale);
  ok = ok && SUCCEEDED(d2d_->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0, .30f), &brush)) &&
      SUCCEEDED(d2d_->CreateCommandList(&shadow)) && SUCCEEDED(d2d_->CreateEffect(CLSID_D2D1GaussianBlur, &blur));
  if (ok) {
    d2d_->SetTarget(shadow.Get()); d2d_->SetTransform(D2D1::Matrix3x2F::Translation(0, 3 * scale));
    d2d_->BeginDraw(); d2d_->FillRoundedRectangle(card, brush.Get());
    ok = SUCCEEDED(d2d_->EndDraw()) && SUCCEEDED(shadow->Close());
    blur->SetInput(0, shadow.Get());
    ok = SUCCEEDED(blur->SetValue(D2D1_GAUSSIANBLUR_PROP_STANDARD_DEVIATION, 4.f * scale)) && ok;
  }
  if (ok) {
    d2d_->SetTarget(bitmap.Get()); d2d_->BeginDraw();
    d2d_->SetTransform(D2D1::Matrix3x2F::Translation(static_cast<float>(offset.x), static_cast<float>(offset.y)));
    d2d_->PushAxisAlignedClip(D2D1::RectF(0, 0, static_cast<float>(width), static_cast<float>(height)), D2D1_ANTIALIAS_MODE_ALIASED);
    d2d_->Clear(D2D1::ColorF(0, 0, 0, 0));
    d2d_->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    d2d_->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    if (ok) {
      d2d_->DrawImage(blur.Get());
      brush->SetColor(D2D1::ColorF(.12f, .115f, .13f, .98f)); d2d_->FillRoundedRectangle(card, brush.Get());
      brush->SetColor(D2D1::ColorF(1, 1, 1, .14f)); d2d_->DrawRoundedRectangle(card, brush.Get(), scale);
      brush->SetColor(D2D1::ColorF(.97f, .96f, .98f));
      d2d_->DrawText(title.c_str(), static_cast<UINT32>(title.size()), heading.Get(),
          D2D1::RectF(28 * scale, 21 * scale, w - 28 * scale, 39 * scale), brush.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
      brush->SetColor(D2D1::ColorF(.73f, .70f, .78f));
      const D2D1_RECT_F line = single ? D2D1::RectF(28 * scale, 12 * scale, w - 28 * scale, h - 12 * scale)
                                      : D2D1::RectF(28 * scale, 42 * scale, w - 28 * scale, h - 19 * scale);
      if (keys.empty()) {
        d2d_->DrawText(detail.c_str(), static_cast<UINT32>(detail.size()), body.Get(), line, brush.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
      } else {
        float x = line.left;
        if (detail_w) { d2d_->DrawText(detail.c_str(), static_cast<UINT32>(detail.size()), body.Get(), D2D1::RectF(x, line.top, x + detail_w + 1, line.bottom), brush.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP); x += detail_w + gap; }
        const float key_h = 17 * scale, key_top = (line.top + line.bottom) / 2 - key_h / 2 - scale;
        for (size_t i = 0; i < keys.size(); ++i) {
          const auto cap = D2D1::RoundedRect(D2D1::RectF(x, key_top, x + key_w[i], key_top + key_h), 4 * scale, 4 * scale);
          brush->SetColor(D2D1::ColorF(1, 1, 1, .08f)); d2d_->FillRoundedRectangle(cap, brush.Get());
          brush->SetColor(D2D1::ColorF(1, 1, 1, .26f)); d2d_->DrawRoundedRectangle(cap, brush.Get(), scale);
          brush->SetColor(D2D1::ColorF(.92f, .9f, .95f));
          d2d_->DrawText(keys[i].c_str(), static_cast<UINT32>(keys[i].size()), key_text.Get(), D2D1::RectF(x, key_top, x + key_w[i], key_top + key_h), brush.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
          x += key_w[i] + gap;
        }
        brush->SetColor(D2D1::ColorF(.73f, .70f, .78f));
        d2d_->DrawText(tail.c_str(), static_cast<UINT32>(tail.size()), body.Get(), D2D1::RectF(x, line.top, line.right, line.bottom), brush.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
      }
    }
    d2d_->PopAxisAlignedClip();
    ok = SUCCEEDED(d2d_->EndDraw()) && ok; d2d_->SetTarget(nullptr);
  }
  if (FAILED(surface->EndDraw())) ok = false;
  d2d_->SetTarget(nullptr);
  if (!ok || FAILED(notification_motion_->SetContent(surface.Get()))) return false;
  notification_surface_ = surface; notification_width_ = width; notification_height_ = height;
  notification_scale_ = scale; notification_title_ = title; notification_detail_ = detail; notification_keys_ = keys; notification_tail_ = tail;
  notification_margin_ = static_cast<int>(std::lround(2 * scale)); notification_visible_ = true;
  notification_motion_->SetOffsetY(0.f); notification_opacity_->SetOpacity(1.f);
  // A uniform scale(0 -> 1) around the middle of the card's upper edge.
  // No translation or independent stretching: the entire card grows downward.
  notification_growth_->SetCenterX(width / 2.f); notification_growth_->SetCenterY(12 * scale);
  notification_growth_->SetScaleX(1.f); notification_growth_->SetScaleY(1.f);
  if (animate && NativeMotionEnabled()) {
    // The compact toast lands with a small bounce.
    const auto growth = single ? PeekEntryMotion(device_.Get(), .05f) : EaseNativeMotion(device_.Get(), 0.f, 1.f, kPopupOpeningMs / 1000.f);
    if (growth) {
      notification_growth_->SetScaleX(growth.Get()); notification_growth_->SetScaleY(growth.Get());
    }
  }
  PositionNotification(area);
  if (FAILED(device_->Commit())) { HideNotification(); return false; }
  return true;
}
void PageCorners::PositionNotification(const RECT& area) {
  if (!notification_visible_) return;
  const int width = std::min(static_cast<int>(std::lround(384 * notification_scale_)),
                             static_cast<int>(area.right - area.left - 12 * notification_scale_));
  // A compact toast keeps its own width (re-laid out only if it no longer fits).
  if (notification_title_.empty() ? notification_width_ > width : width != notification_width_) {
    ShowNotification(parent_, area, notification_scale_, notification_title_, notification_detail_, notification_keys_, notification_tail_, false); return;
  }
  const LONG x = area.right - notification_width_ - notification_margin_, y = area.top + notification_margin_;
  notification_bounds_ = {x, y, x + notification_width_, y + notification_height_};
  notification_->SetOffsetX(static_cast<float>(x)); notification_->SetOffsetY(static_cast<float>(y));
  device_->Commit();
}
int PageCorners::DismissNotification() {
  if (!notification_visible_ || !NativeMotionEnabled()) return 0;
  const auto opacity = EaseNativeMotion(device_.Get(), 1, 0, .16f);
  const auto growth = EaseNativeMotion(device_.Get(), 1.f, 0.f, .16f);
  if (!opacity || !growth) return 0;
  notification_opacity_->SetOpacity(opacity.Get());
  notification_growth_->SetScaleX(growth.Get()); notification_growth_->SetScaleY(growth.Get());
  return SUCCEEDED(device_->Commit()) ? 160 : 0;
}
void PageCorners::HideNotification() {
  if (notification_motion_) notification_motion_->SetContent(nullptr);
  notification_surface_.Reset(); notification_visible_ = false;
  if (device_) device_->Commit();
}
}  // namespace red
