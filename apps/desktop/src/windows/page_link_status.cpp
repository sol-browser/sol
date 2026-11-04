#include "windows/page_corners.h"
#include "windows/motion.h"
#include <dwrite.h>
#include <algorithm>
#include <cmath>

using Microsoft::WRL::ComPtr;
namespace red {
bool PageCorners::ShowLinkStatus(HWND parent, const RECT& area, float scale,
                                const std::wstring& text, const Theme& theme) {
  const int inset = static_cast<int>(std::lround(6 * scale));
  const int height = static_cast<int>(std::lround(28 * scale));
  const int limit = std::min(static_cast<int>(std::lround(560 * scale)),
                             static_cast<int>(area.right - area.left) - 2 * inset);
  if (text.empty() || limit < 64 * scale || area.bottom - area.top < height + 2 * inset) {
    HideLinkStatus(); return false;
  }
  if (!Init(parent)) return false;
  if (!link_status_) {
    if (FAILED(device_->CreateVisual(&link_status_)) ||
        FAILED(device_->CreateEffectGroup(&link_status_opacity_)) ||
        FAILED(link_status_->SetEffect(link_status_opacity_.Get()))) return false;
  }
  const bool opening = !link_status_visible_;
  if (!link_status_surface_ || text != link_status_text_ || theme != link_status_theme_ ||
      scale != link_status_scale_ || limit != link_status_limit_) {
    ComPtr<IDWriteFactory> fonts;
    ComPtr<IDWriteTextFormat> format;
    ComPtr<IDWriteTextLayout> layout;
    ComPtr<IDWriteInlineObject> ellipsis;
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
            reinterpret_cast<IUnknown**>(fonts.GetAddressOf()))) ||
        FAILED(fonts->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
            DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 11.f * scale, L"en-US", &format)) ||
        FAILED(fonts->CreateEllipsisTrimmingSign(format.Get(), &ellipsis))) return false;
    DWRITE_TRIMMING trimming{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
    format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    format->SetReadingDirection(DWRITE_READING_DIRECTION_LEFT_TO_RIGHT);
    format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    format->SetTrimming(&trimming, ellipsis.Get());
    const float padding = 10 * scale;
    if (FAILED(fonts->CreateTextLayout(text.c_str(), static_cast<UINT32>(text.size()),
            format.Get(), static_cast<float>(limit) - 2 * padding, static_cast<float>(height), &layout))) return false;
    DWRITE_TEXT_METRICS metrics{};
    if (FAILED(layout->GetMetrics(&metrics))) return false;
    const int width = std::clamp(static_cast<int>(std::ceil(metrics.widthIncludingTrailingWhitespace + 2 * padding)),
                                 static_cast<int>(48 * scale), limit);
    ComPtr<IDCompositionSurface> surface;
    if (FAILED(device_->CreateSurface(width, height, DXGI_FORMAT_B8G8R8A8_UNORM,
                                     DXGI_ALPHA_MODE_PREMULTIPLIED, &surface))) return false;
    ComPtr<IDXGISurface> dxgi; POINT offset{};
    if (FAILED(surface->BeginDraw(nullptr, __uuidof(IDXGISurface), &dxgi, &offset))) return false;
    const auto properties = D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
    ComPtr<ID2D1Bitmap1> bitmap;
    ComPtr<ID2D1SolidColorBrush> brush;
    bool ok = SUCCEEDED(d2d_->CreateBitmapFromDxgiSurface(dxgi.Get(), &properties, &bitmap)) &&
              SUCCEEDED(d2d_->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0), &brush));
    if (ok) {
      // The same authored surface as Panel/Menu popups; always dark and tinted
      // by the current space. Text stays neutral, matching their default ink.
      const auto background = Themed({38, 34, 44}, theme, false);
      d2d_->SetTarget(bitmap.Get()); d2d_->BeginDraw();
      d2d_->SetTransform(D2D1::Matrix3x2F::Translation(static_cast<float>(offset.x), static_cast<float>(offset.y)));
      d2d_->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
      d2d_->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
      d2d_->PushAxisAlignedClip(D2D1::RectF(0, 0, static_cast<float>(width), static_cast<float>(height)), D2D1_ANTIALIAS_MODE_ALIASED);
      d2d_->Clear(D2D1::ColorF(0, 0, 0, 0));
      const auto card = D2D1::RoundedRect(D2D1::RectF(.5f * scale, .5f * scale,
          width - .5f * scale, height - .5f * scale), 12 * scale, 12 * scale);
      brush->SetColor(D2D1::ColorF(background.r / 255.f, background.g / 255.f, background.b / 255.f));
      d2d_->FillRoundedRectangle(card, brush.Get());
      brush->SetColor(D2D1::ColorF(1, 1, 1, .12f)); d2d_->DrawRoundedRectangle(card, brush.Get(), scale);
      brush->SetColor(D2D1::ColorF(.91f, .91f, .91f));
      d2d_->DrawText(text.c_str(), static_cast<UINT32>(text.size()), format.Get(),
          D2D1::RectF(padding, 0, width - padding, static_cast<float>(height)), brush.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
      d2d_->PopAxisAlignedClip(); ok = SUCCEEDED(d2d_->EndDraw()); d2d_->SetTarget(nullptr);
    }
    ok = SUCCEEDED(surface->EndDraw()) && ok;
    if (!ok) return false;
    link_status_surface_ = surface; link_status_text_ = text; link_status_theme_ = theme;
    link_status_scale_ = scale; link_status_limit_ = limit; link_status_width_ = width;
  }
  LONG x = area.left + inset, y = area.bottom - inset - height;
  POINT cursor{}; GetCursorPos(&cursor); ScreenToClient(parent, &cursor);
  RECT bounds{x, y, x + link_status_width_, y + height};
  if (PtInRect(&bounds, cursor)) {
    x = area.right - inset - link_status_width_;
    bounds = {x, y, x + link_status_width_, y + height};
    if (PtInRect(&bounds, cursor)) y -= height + inset;
  }
  link_status_bounds_ = {x, y, x + link_status_width_, y + height};
  link_status_area_ = area;
  link_status_->SetOffsetX(static_cast<float>(x)); link_status_->SetOffsetY(static_cast<float>(y));
  if (FAILED(link_status_->SetContent(link_status_surface_.Get()))) return false;
  // A reused label must appear above a subsequently created Peek scene too.
  if (opening) {
    root_->RemoveVisual(link_status_.Get());
    if (FAILED(root_->AddVisual(link_status_.Get(), TRUE, nullptr))) return false;
    const auto fade = NativeMotionEnabled() ? EaseNativeMotion(device_.Get(), 0, 1, .10f) : nullptr;
    if (fade) link_status_opacity_->SetOpacity(fade.Get());
    else link_status_opacity_->SetOpacity(1.f);
  }
  link_status_visible_ = SUCCEEDED(device_->Commit());
  return link_status_visible_;
}
void PageCorners::HideLinkStatus() {
  if (!link_status_visible_) return;
  if (link_status_) link_status_->SetContent(nullptr);
  link_status_visible_ = false;
  if (device_) device_->Commit();
}
}  // namespace red
