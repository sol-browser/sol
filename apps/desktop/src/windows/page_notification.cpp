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
D2D1_COLOR_F Color(Rgb c, float alpha = 1) { return D2D1::ColorF(c.r / 255.f, c.g / 255.f, c.b / 255.f, alpha); }
// The card's inset from the page is independent of the space reserved for its shadow.
constexpr float kCardInset = 12, kTextInset = 12, kShadow = 16, kTopShadow = 14, kBottomShadow = 18;
constexpr float kMaxWidth = 480, kSingleHeight = 78, kTwoLineHeight = 96, kRadius = 8;
Rgb Mix(Rgb a, Rgb b, float t) {
  auto m = [t](uint8_t x, uint8_t y) { return static_cast<uint8_t>(std::lround(x + (y - x) * t)); };
  return {m(a.r, b.r), m(a.g, b.g), m(a.b, b.b)};
}
float Luminance(Rgb c) { return (.2126f * c.r + .7152f * c.g + .0722f * c.b) / 255.f; }
// A snappy landing: scale overshoots to |peak| quickly (ease-out), then settles to 1.
ComPtr<IDCompositionAnimation> SnapMotion(IDCompositionDevice* device, float from, float peak, float rise, float settle) {
  ComPtr<IDCompositionAnimation> animation;
  const float up = peak - from, back = 1 - peak;
  if (FAILED(device->CreateAnimation(&animation)) ||
      FAILED(animation->AddCubic(0, from, 3 * up / rise, -3 * up / (rise * rise), up / (rise * rise * rise))) ||
      FAILED(animation->AddCubic(rise, peak, 0, 3 * back / (settle * settle), -2 * back / (settle * settle * settle))) ||
      FAILED(animation->End(rise + settle, 1.f))) return nullptr;
  return animation;
}
}  // namespace
bool PageCorners::ShowNotification(HWND parent, const RECT& area, float scale,
                                  const std::wstring& title, const std::wstring& detail,
                                  const std::vector<std::wstring>& keys, const std::wstring& tail, bool animate,
                                  bool* animation_started) {
  if (animation_started) *animation_started = false;
  if (!Init(parent)) return false;
  int width = std::min(static_cast<int>(std::lround(kMaxWidth * scale)),
                       static_cast<int>(area.right - area.left - 12 * scale));
  // Without a title it is a compact single-line toast (the full-screen hint),
  // as wide as its text (measured below).
  const bool single = title.empty();
  const int height = static_cast<int>(std::lround((single ? kSingleHeight : kTwoLineHeight) * scale));
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
          DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 14.f * scale, L"en-US", &heading)) ||
      FAILED(fonts->CreateTextFormat(L"Segoe UI", nullptr, single ? DWRITE_FONT_WEIGHT_SEMI_BOLD : DWRITE_FONT_WEIGHT_NORMAL,
          DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 16.f * scale, L"en-US", &body))) return false;
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
  const float gap = 6 * scale, key_pad = 7 * scale;
  if (single) body->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
  if (!keys.empty()) {
    if (FAILED(fonts->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, 13.5f * scale, L"en-US", &key_text))) return false;
    key_text->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER); key_text->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    detail_w = TextWidth(fonts.Get(), body.Get(), detail);
    for (const auto& key : keys) key_w.push_back(std::max(29 * scale, TextWidth(fonts.Get(), key_text.Get(), key) + 2 * key_pad));
  }
  if (single) {
    float content = (detail_w ? detail_w + gap : 0) + TextWidth(fonts.Get(), body.Get(), keys.empty() ? detail : tail);
    for (const float cap : key_w) content += cap + gap;
    // A few pixels of slack: measured and drawn text can differ by a subpixel.
    width = std::min(width, static_cast<int>(std::lround(content + (2 * (kShadow + kTextInset) + 6) * scale)));
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
  const float edge = kShadow * scale, text_left = (kShadow + kTextInset) * scale;
  const auto card = D2D1::RoundedRect(D2D1::RectF(edge, kTopShadow * scale, w - edge, h - kBottomShadow * scale), kRadius * scale, kRadius * scale);
  // The space's own colour: the theme background where the toast sits (top
  // right of the window), so it reads like a piece of the sidebar and top bar.
  // Text and key caps are tints of it, dark or light by its brightness.
  const auto& theme = notification_theme_; const bool dark = notification_dark_;
  const auto fill = PaletteBackgroundAt(theme, dark, .9, .06);
  const bool light = Luminance(fill) > .5f;
  const auto ink = Mix(fill, light ? Rgb{0, 0, 0} : Rgb{255, 255, 255}, light ? .84f : .92f),
             muted = Mix(fill, ink, .62f),
             cap_fill = Mix(fill, Rgb{255, 255, 255}, light ? .45f : .1f), cap_line = Mix(fill, ink, .24f),
             glow = Mix(fill, Rgb{0, 0, 0}, .7f);
  ok = ok && SUCCEEDED(d2d_->CreateSolidColorBrush(light ? Color(glow, .34f) : D2D1::ColorF(0, 0, 0, .55f), &brush)) &&
      SUCCEEDED(d2d_->CreateCommandList(&shadow)) && SUCCEEDED(d2d_->CreateEffect(CLSID_D2D1GaussianBlur, &blur));
  if (ok) {
    d2d_->SetTarget(shadow.Get()); d2d_->SetTransform(D2D1::Matrix3x2F::Translation(0, 5 * scale));
    d2d_->BeginDraw(); d2d_->FillRoundedRectangle(card, brush.Get());
    ok = SUCCEEDED(d2d_->EndDraw()) && SUCCEEDED(shadow->Close());
    blur->SetInput(0, shadow.Get());
    ok = SUCCEEDED(blur->SetValue(D2D1_GAUSSIANBLUR_PROP_STANDARD_DEVIATION, 6.f * scale)) && ok;
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
      // No outline: the shadow alone sets the card off the page.
      brush->SetColor(Color(fill)); d2d_->FillRoundedRectangle(card, brush.Get());
      brush->SetColor(Color(ink));
      d2d_->DrawText(title.c_str(), static_cast<UINT32>(title.size()), heading.Get(),
          D2D1::RectF(text_left, card.rect.top + 11 * scale, w - text_left, card.rect.top + 31 * scale), brush.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
      // A single line is the card's main text: full ink. Under a title it is the detail.
      brush->SetColor(Color(single ? ink : muted));
      const D2D1_RECT_F line = single ? D2D1::RectF(text_left, card.rect.top, w - text_left, card.rect.bottom)
                                      : D2D1::RectF(text_left, card.rect.top + 33 * scale, w - text_left, card.rect.bottom - 9 * scale);
      if (keys.empty()) {
        d2d_->DrawText(detail.c_str(), static_cast<UINT32>(detail.size()), body.Get(), line, brush.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
      } else {
        float x = line.left;
        if (detail_w) { d2d_->DrawText(detail.c_str(), static_cast<UINT32>(detail.size()), body.Get(), D2D1::RectF(x, line.top, x + detail_w + 1, line.bottom), brush.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP); x += detail_w + gap; }
        const float key_h = 25 * scale, key_top = (line.top + line.bottom) / 2 - key_h / 2;
        for (size_t i = 0; i < keys.size(); ++i) {
          const auto cap = D2D1::RoundedRect(D2D1::RectF(x, key_top, x + key_w[i], key_top + key_h), 5 * scale, 5 * scale);
          brush->SetColor(Color(cap_fill)); d2d_->FillRoundedRectangle(cap, brush.Get());
          brush->SetColor(Color(cap_line)); d2d_->DrawRoundedRectangle(cap, brush.Get(), scale);
          brush->SetColor(Color(ink));
          d2d_->DrawText(keys[i].c_str(), static_cast<UINT32>(keys[i].size()), key_text.Get(), D2D1::RectF(x, key_top, x + key_w[i], key_top + key_h), brush.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
          x += key_w[i] + gap;
        }
        brush->SetColor(Color(single ? ink : muted));
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
  notification_visible_ = true;
  // Replace the content and restart entry together, even during a previous
  // entrance or dismissal. A layout-only update keeps the current motion.
  if (animate) {
    notification_motion_->SetOffsetY(0.f); notification_opacity_->SetOpacity(1.f);
    notification_growth_->SetScaleX(1.f); notification_growth_->SetScaleY(1.f);
  }
  notification_growth_->SetCenterX(width / 2.f); notification_growth_->SetCenterY(kTopShadow * scale);
  bool animated = false;
  if (animate && NativeMotionEnabled()) {
    // Snaps in: drops a little from above while it pops past full size and
    // settles back, and is opaque almost at once.
    const auto growth = SnapMotion(device_.Get(), .72f, 1.07f, .15f, .14f);
    const auto drop = EaseNativeMotion(device_.Get(), -12 * scale, 0, .17f);
    const auto fade = EaseNativeMotion(device_.Get(), 0, 1, .09f);
    if (growth) { notification_growth_->SetScaleX(growth.Get()); notification_growth_->SetScaleY(growth.Get()); }
    if (drop) notification_motion_->SetOffsetY(drop.Get());
    if (fade) notification_opacity_->SetOpacity(fade.Get());
    animated = growth && drop && fade;
  }
  PositionNotification(area);
  if (FAILED(device_->Commit())) { HideNotification(); return false; }
  if (animation_started) *animation_started = animated;
  return true;
}
void PageCorners::PositionNotification(const RECT& area) {
  if (!notification_visible_) return;
  const int width = std::min(static_cast<int>(std::lround(kMaxWidth * notification_scale_)),
                             static_cast<int>(area.right - area.left - 12 * notification_scale_));
  // A compact toast keeps its own width (re-laid out only if it no longer fits).
  if (notification_title_.empty() ? notification_width_ > width : width != notification_width_) {
    ShowNotification(parent_, area, notification_scale_, notification_title_, notification_detail_, notification_keys_, notification_tail_, false); return;
  }
  const auto scale = notification_scale_;
  const LONG inset = std::lround(kCardInset * scale), shadow = std::lround(kShadow * scale);
  const LONG x = area.right - notification_width_ + shadow - inset;
  const LONG y = area.top + inset - std::lround(kTopShadow * scale);
  notification_bounds_ = {x + shadow, area.top + inset, area.right - inset,
                          y + notification_height_ - std::lround(kBottomShadow * scale)};
  notification_->SetOffsetX(static_cast<float>(x)); notification_->SetOffsetY(static_cast<float>(y));
  device_->Commit();
}
int PageCorners::DismissNotification() {
  if (!notification_visible_ || !NativeMotionEnabled()) return 0;
  // Leaves quickly: lifts away, shrinks a little and fades.
  const auto opacity = EaseNativeMotion(device_.Get(), 1, 0, .14f);
  const auto growth = EaseNativeMotion(device_.Get(), 1.f, .9f, .14f);
  const auto lift = EaseNativeMotion(device_.Get(), 0, -8 * notification_scale_, .14f);
  if (!opacity || !growth) return 0;
  notification_opacity_->SetOpacity(opacity.Get());
  notification_growth_->SetScaleX(growth.Get()); notification_growth_->SetScaleY(growth.Get());
  if (lift) notification_motion_->SetOffsetY(lift.Get());
  return SUCCEEDED(device_->Commit()) ? 140 : 0;
}
void PageCorners::HideNotification() {
  if (notification_motion_) notification_motion_->SetContent(nullptr);
  notification_surface_.Reset(); notification_visible_ = false;
  if (device_) device_->Commit();
}
}  // namespace red
