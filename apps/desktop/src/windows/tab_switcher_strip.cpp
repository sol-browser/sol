// The Ctrl+Tab strip (Arc's Tab Switcher), drawn by the browser in its own composition tree above the
// page, like toasts: no HWND, no focus, no hit-testing, and nothing in any website. Up to five cards, each
// with the tab's picture (or its first letter), icon and title; the selected card has a yellow outline.
#include "windows/page_corners.h"
#include "windows/motion.h"
#include <dwrite.h>
#include <d2d1effects.h>
#include <wincodec.h>
#include <algorithm>
#include <cmath>

using Microsoft::WRL::ComPtr;

namespace red {
namespace {
// Sizes in DIPs.
constexpr float kCardWidth = 236, kThumbWidth = 220, kThumbHeight = 138, kCardInset = 8, kGap = 8, kPad = 12;
constexpr float kCardHeight = kCardInset + kThumbHeight + 10 + 18 + 16 + 9;  // Picture, title row, "Current".
constexpr float kRadius = 20, kCardRadius = 13, kThumbRadius = 8, kShadow = 34;

D2D1_COLOR_F Rgba(uint32_t rgb, float alpha) {
  return D2D1::ColorF(((rgb >> 16) & 255) / 255.f, ((rgb >> 8) & 255) / 255.f, (rgb & 255) / 255.f, alpha);
}

// PNG or JPEG bytes into a bitmap of this device context; null when they are not a picture.
ComPtr<ID2D1Bitmap1> Decode(ID2D1DeviceContext* d2d, const std::string& bytes) {
  if (bytes.empty() || bytes.size() > 8u * 1024 * 1024) return nullptr;
  ComPtr<IWICImagingFactory> factory; ComPtr<IWICStream> stream; ComPtr<IWICBitmapDecoder> decoder;
  ComPtr<IWICBitmapFrameDecode> frame; ComPtr<IWICFormatConverter> converter; ComPtr<ID2D1Bitmap1> bitmap;
  UINT width = 0, height = 0;
  if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))) || FAILED(factory->CreateStream(&stream)) ||
      FAILED(stream->InitializeFromMemory(reinterpret_cast<BYTE*>(const_cast<char*>(bytes.data())), static_cast<DWORD>(bytes.size()))) ||
      FAILED(factory->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnLoad, &decoder)) || FAILED(decoder->GetFrame(0, &frame)) ||
      FAILED(frame->GetSize(&width, &height)) || !width || !height || static_cast<unsigned long long>(width) * height > 4u * 1024 * 1024 ||
      FAILED(factory->CreateFormatConverter(&converter)) ||
      FAILED(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom)) ||
      FAILED(d2d->CreateBitmapFromWicBitmap(converter.Get(), nullptr, &bitmap)))
    return nullptr;
  return bitmap;
}

ComPtr<IDWriteTextFormat> Format(IDWriteFactory* fonts, DWRITE_FONT_WEIGHT weight, float size) {
  ComPtr<IDWriteTextFormat> format;
  if (FAILED(fonts->CreateTextFormat(L"Segoe UI", nullptr, weight, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, size, L"en-US", &format)))
    return nullptr;
  format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
  return format;
}
}  // namespace

bool PageCorners::ShowTabSwitcher(HWND parent, const RECT& area, float scale, const std::vector<SwitcherCard>& cards, int selected) {
  if (cards.empty() || !Init(parent)) return false;
  if (!switcher_) {
    ComPtr<IDCompositionVisual> visual; ComPtr<IDCompositionEffectGroup> opacity;
    if (FAILED(device_->CreateVisual(&visual)) || FAILED(device_->CreateEffectGroup(&opacity)) || FAILED(visual->SetEffect(opacity.Get())) ||
        FAILED(root_->AddVisual(visual.Get(), TRUE, nullptr)))
      return false;
    switcher_ = visual; switcher_opacity_ = opacity;
  }
  const bool fresh = !switcher_visible_;
  switcher_cards_ = cards;
  switcher_thumbs_.clear(); switcher_icons_.clear();
  for (const auto& card : switcher_cards_) {
    switcher_thumbs_.push_back(Decode(d2d_.Get(), card.thumb));
    switcher_icons_.push_back(Decode(d2d_.Get(), card.icon));
  }
  switcher_area_ = area; switcher_scale_ = scale;
  switcher_selected_ = std::clamp(selected, 0, static_cast<int>(cards.size()) - 1);
  if (!PaintTabSwitcher()) { HideTabSwitcher(); return false; }
  switcher_visible_ = true;
  // Appears quickly; a reselection or a removed card only repaints.
  if (fresh && NativeMotionEnabled()) {
    if (const auto fade = EaseNativeMotion(device_.Get(), 0, 1, .12f)) switcher_opacity_->SetOpacity(fade.Get());
    else switcher_opacity_->SetOpacity(1.f);
  } else if (fresh) switcher_opacity_->SetOpacity(1.f);
  return SUCCEEDED(device_->Commit());
}

bool PageCorners::SelectTabSwitcher(int selected) {
  if (!switcher_visible_ || switcher_cards_.empty()) return false;
  switcher_selected_ = std::clamp(selected, 0, static_cast<int>(switcher_cards_.size()) - 1);
  return PaintTabSwitcher() && SUCCEEDED(device_->Commit());
}

void PageCorners::HideTabSwitcher() {
  if (switcher_) switcher_->SetContent(nullptr);
  switcher_surface_.Reset();
  switcher_cards_.clear(); switcher_thumbs_.clear(); switcher_icons_.clear();
  switcher_visible_ = false; switcher_bounds_ = {};
  if (device_) device_->Commit();
}

bool PageCorners::PaintTabSwitcher() {
  const auto count = static_cast<float>(switcher_cards_.size());
  const float strip_width = 2 * kPad + count * kCardWidth + (count - 1) * kGap, strip_height = 2 * kPad + kCardHeight;
  // A narrow window gets smaller cards rather than a strip wider than the page.
  const float room = static_cast<float>(switcher_area_.right - switcher_area_.left) - 32 * switcher_scale_;
  const float s = switcher_scale_ * std::clamp(room / (strip_width * switcher_scale_), .55f, 1.f);
  const int width = static_cast<int>(std::ceil((strip_width + 2 * kShadow) * s)), height = static_cast<int>(std::ceil((strip_height + 2 * kShadow) * s));
  ComPtr<IDWriteFactory> fonts;
  if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(fonts.GetAddressOf())))) return false;
  const auto title = Format(fonts.Get(), DWRITE_FONT_WEIGHT_SEMI_BOLD, 14 * s), letter = Format(fonts.Get(), DWRITE_FONT_WEIGHT_SEMI_BOLD, 36 * s),
             label = Format(fonts.Get(), DWRITE_FONT_WEIGHT_SEMI_BOLD, 10.5f * s);
  ComPtr<IDWriteInlineObject> ellipsis;
  if (!title || !letter || !label || FAILED(fonts->CreateEllipsisTrimmingSign(title.Get(), &ellipsis))) return false;
  const DWRITE_TRIMMING trimming{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
  title->SetTrimming(&trimming, ellipsis.Get()); title->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
  letter->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER); letter->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

  ComPtr<IDCompositionSurface> surface;
  if (FAILED(device_->CreateSurface(width, height, DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_ALPHA_MODE_PREMULTIPLIED, &surface))) return false;
  ComPtr<IDXGISurface> dxgi; POINT offset{};
  if (FAILED(surface->BeginDraw(nullptr, __uuidof(IDXGISurface), &dxgi, &offset))) return false;
  ComPtr<ID2D1Bitmap1> target;
  const auto props = D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
      D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
  bool ok = SUCCEEDED(d2d_->CreateBitmapFromDxgiSurface(dxgi.Get(), &props, &target));
  const auto strip = D2D1::RoundedRect(D2D1::RectF(kShadow * s, kShadow * s, (kShadow + strip_width) * s, (kShadow + strip_height) * s), kRadius * s, kRadius * s);
  ComPtr<ID2D1SolidColorBrush> brush; ComPtr<ID2D1CommandList> shadow; ComPtr<ID2D1Effect> blur;
  ok = ok && SUCCEEDED(d2d_->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0, .5f), &brush)) && SUCCEEDED(d2d_->CreateCommandList(&shadow)) &&
       SUCCEEDED(d2d_->CreateEffect(CLSID_D2D1GaussianBlur, &blur));
  if (ok) {
    d2d_->SetTarget(shadow.Get()); d2d_->SetTransform(D2D1::Matrix3x2F::Translation(0, 10 * s));
    d2d_->BeginDraw(); d2d_->FillRoundedRectangle(strip, brush.Get());
    ok = SUCCEEDED(d2d_->EndDraw()) && SUCCEEDED(shadow->Close());
    blur->SetInput(0, shadow.Get());
    ok = ok && SUCCEEDED(blur->SetValue(D2D1_GAUSSIANBLUR_PROP_STANDARD_DEVIATION, 14.f * s));
  }
  if (ok) {
    d2d_->SetTarget(target.Get()); d2d_->BeginDraw();
    const auto origin = D2D1::Matrix3x2F::Translation(static_cast<float>(offset.x), static_cast<float>(offset.y));
    d2d_->SetTransform(origin);
    d2d_->PushAxisAlignedClip(D2D1::RectF(0, 0, static_cast<float>(width), static_cast<float>(height)), D2D1_ANTIALIAS_MODE_ALIASED);
    d2d_->Clear(D2D1::ColorF(0, 0, 0, 0));
    d2d_->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    d2d_->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    d2d_->DrawImage(blur.Get());
    brush->SetColor(Rgba(0x1b1714, .94f)); d2d_->FillRoundedRectangle(strip, brush.Get());
    brush->SetColor(Rgba(0xffffff, .11f)); d2d_->DrawRoundedRectangle(strip, brush.Get(), s);
    for (size_t i = 0; i < switcher_cards_.size(); ++i) {
      const auto& card = switcher_cards_[i];
      const bool selected = static_cast<int>(i) == switcher_selected_;
      const float left = (kShadow + kPad + i * (kCardWidth + kGap)) * s, top = (kShadow + kPad) * s;
      const auto box = D2D1::RoundedRect(D2D1::RectF(left, top, left + kCardWidth * s, top + kCardHeight * s), kCardRadius * s, kCardRadius * s);
      if (selected) {
        brush->SetColor(Rgba(0xffffff, .12f)); d2d_->FillRoundedRectangle(box, brush.Get());
        auto edge = box; const float half = .75f * s;
        edge.rect = D2D1::RectF(box.rect.left + half, box.rect.top + half, box.rect.right - half, box.rect.bottom - half);
        brush->SetColor(Rgba(0xf9ca30, 1)); d2d_->DrawRoundedRectangle(edge, brush.Get(), 1.5f * s);
      }
      const D2D1_RECT_F thumb = D2D1::RectF(left + kCardInset * s, top + kCardInset * s, left + (kCardInset + kThumbWidth) * s, top + (kCardInset + kThumbHeight) * s);
      const auto thumb_box = D2D1::RoundedRect(thumb, kThumbRadius * s, kThumbRadius * s);
      if (const auto& picture = switcher_thumbs_[i]) {
        // The picture covers the frame (cropped, never stretched), with its corners rounded.
        const auto size = picture->GetSize();
        const float fit = std::max((thumb.right - thumb.left) / size.width, (thumb.bottom - thumb.top) / size.height);
        const float x = thumb.left + ((thumb.right - thumb.left) - size.width * fit) / 2, y = thumb.top + ((thumb.bottom - thumb.top) - size.height * fit) / 2;
        ComPtr<ID2D1BitmapBrush1> fill;
        const auto brush_props = D2D1::BitmapBrushProperties1(D2D1_EXTEND_MODE_CLAMP, D2D1_EXTEND_MODE_CLAMP, D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC);
        if (SUCCEEDED(d2d_->CreateBitmapBrush(picture.Get(), &brush_props, nullptr, &fill))) {
          fill->SetTransform(D2D1::Matrix3x2F::Scale(fit, fit) * D2D1::Matrix3x2F::Translation(x, y));
          d2d_->FillRoundedRectangle(thumb_box, fill.Get());
        }
      } else {
        brush->SetColor(Rgba(0xffffff, .06f)); d2d_->FillRoundedRectangle(thumb_box, brush.Get());
        brush->SetColor(Rgba(0xffffff, .7f));
        d2d_->DrawText(card.letter.c_str(), static_cast<UINT32>(card.letter.size()), letter.Get(), thumb, brush.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
      }
      const float row = thumb.bottom + 10 * s;
      const auto icon = D2D1::RectF(left + kCardInset * s, row + 1 * s, left + (kCardInset + 16) * s, row + 17 * s);
      if (const auto& image = switcher_icons_[i]) d2d_->DrawBitmap(image.Get(), icon, 1.f, D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC);
      else { brush->SetColor(Rgba(0xffffff, .15f)); d2d_->FillRoundedRectangle(D2D1::RoundedRect(icon, 4 * s, 4 * s), brush.Get()); }
      brush->SetColor(selected ? Rgba(0xffffff, 1) : Rgba(0xffffff, .85f));
      d2d_->DrawText(card.title.c_str(), static_cast<UINT32>(card.title.size()), title.Get(),
          D2D1::RectF(icon.right + 8 * s, row - 1 * s, left + (kCardWidth - kCardInset) * s, row + 19 * s), brush.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
      if (card.current) {
        static constexpr wchar_t kCurrent[] = L"CURRENT";
        brush->SetColor(Rgba(0xf9ca30, 1));
        d2d_->DrawText(kCurrent, static_cast<UINT32>(std::size(kCurrent) - 1), label.Get(),
            D2D1::RectF(left + kCardInset * s, row + 21 * s, left + (kCardWidth - kCardInset) * s, row + 35 * s), brush.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
      }
    }
    d2d_->PopAxisAlignedClip();
    ok = SUCCEEDED(d2d_->EndDraw());
    d2d_->SetTarget(nullptr);
  }
  if (FAILED(surface->EndDraw())) ok = false;
  d2d_->SetTarget(nullptr);
  if (!ok || FAILED(switcher_->SetContent(surface.Get()))) return false;
  switcher_surface_ = surface;
  // Centred over the page area.
  const LONG x = switcher_area_.left + ((switcher_area_.right - switcher_area_.left) - width) / 2;
  const LONG y = switcher_area_.top + ((switcher_area_.bottom - switcher_area_.top) - height) / 2;
  switcher_->SetOffsetX(static_cast<float>(x)); switcher_->SetOffsetY(static_cast<float>(y));
  const LONG margin = std::lround(kShadow * s);
  switcher_bounds_ = {x + margin, y + margin, x + width - margin, y + height - margin};
  return true;
}
}  // namespace red
