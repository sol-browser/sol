#include "windows/split_overlay.h"
#include <algorithm>
#include <cmath>
using Microsoft::WRL::ComPtr;

namespace red {
namespace {
LRESULT CALLBACK OverlayProc(HWND window, UINT message, WPARAM w, LPARAM l) {
  if (message == WM_NCHITTEST) return HTTRANSPARENT;
  if (message == WM_MOUSEACTIVATE) return MA_NOACTIVATE;
  return DefWindowProcW(window, message, w, l);
}
D2D1_COLOR_F Color(COLORREF color, float alpha) {
  return D2D1::ColorF(GetRValue(color) / 255.f, GetGValue(color) / 255.f, GetBValue(color) / 255.f, alpha);
}
// A globe for pages without a cached icon (as the sidebar's site mark).
void DrawGlobe(ID2D1RenderTarget* target, ID2D1SolidColorBrush* brush, D2D1_POINT_2F center, float radius, float stroke) {
  target->DrawEllipse(D2D1::Ellipse(center, radius, radius), brush, stroke);
  target->DrawEllipse(D2D1::Ellipse(center, radius * .42f, radius), brush, stroke * .8f);
  target->DrawLine(D2D1::Point2F(center.x - radius, center.y), D2D1::Point2F(center.x + radius, center.y), brush, stroke * .8f);
}
}  // namespace

SplitOverlay::~SplitOverlay() {
  ReleaseSurface();
  if (window_) DestroyWindow(window_);
}

void SplitOverlay::ReleaseSurface() {
  icons_.clear(); brush_.Reset(); target_.Reset();
  if (dc_ && previous_) SelectObject(dc_, previous_);
  if (bitmap_) DeleteObject(bitmap_);
  if (dc_) DeleteDC(dc_);
  dc_ = nullptr; bitmap_ = nullptr; previous_ = nullptr; width_ = height_ = 0;
}

void SplitOverlay::Hide() {
  if (window_ && visible_) ShowWindow(window_, SW_HIDE);
  visible_ = false;
}

// The 32-bit premultiplied surface and its render target, kept while the size
// and scale stay the same (the whole animation).
bool SplitOverlay::Prepare(int width, int height, float scale) {
  if (!factory_ && FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, factory_.GetAddressOf()))) return false;
  if (!images_) CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&images_));
  if (!fonts_ && SUCCEEDED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(fonts_.GetAddressOf()))) &&
      SUCCEEDED(fonts_->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_MEDIUM, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 12.f, L"en-US", &label_))) {
    label_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP); label_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    ComPtr<IDWriteInlineObject> ellipsis;
    if (SUCCEEDED(fonts_->CreateEllipsisTrimmingSign(label_.Get(), &ellipsis))) {
      DWRITE_TRIMMING trimming{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0}; label_->SetTrimming(&trimming, ellipsis.Get());
    }
  }
  if (target_ && dc_ && width == width_ && height == height_ && scale == scale_) return true;
  ReleaseSurface();
  dc_ = CreateCompatibleDC(nullptr); if (!dc_) return false;
  BITMAPINFO info{}; info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER); info.bmiHeader.biWidth = width; info.bmiHeader.biHeight = -height;
  info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32; info.bmiHeader.biCompression = BI_RGB;
  void* pixels = nullptr; bitmap_ = CreateDIBSection(dc_, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
  if (!bitmap_) { ReleaseSurface(); return false; }
  previous_ = SelectObject(dc_, bitmap_);
  // Device-independent drawing: 1 unit = 1 DIP.
  const auto properties = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT,
      D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96 * scale, 96 * scale);
  const RECT whole{0, 0, width, height};
  if (FAILED(factory_->CreateDCRenderTarget(&properties, &target_)) || FAILED(target_->BindDC(dc_, &whole)) ||
      FAILED(target_->CreateSolidColorBrush(D2D1::ColorF(0, 0.f), &brush_))) { ReleaseSurface(); return false; }
  width_ = width; height_ = height; scale_ = scale;
  return true;
}

ID2D1Bitmap* SplitOverlay::Icon(const std::string& png) {
  if (png.empty() || png.size() > 1024 * 1024 || !images_) return nullptr;
  if (const auto found = icons_.find(png); found != icons_.end()) return found->second.Get();
  ComPtr<IWICStream> stream; ComPtr<IWICBitmapDecoder> decoder; ComPtr<IWICBitmapFrameDecode> frame;
  ComPtr<IWICFormatConverter> converter; ComPtr<ID2D1Bitmap> bitmap;
  UINT width = 0, height = 0;
  if (FAILED(images_->CreateStream(&stream)) ||
      FAILED(stream->InitializeFromMemory(reinterpret_cast<BYTE*>(const_cast<char*>(png.data())), static_cast<DWORD>(png.size()))) ||
      FAILED(images_->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnDemand, &decoder)) ||
      FAILED(decoder->GetFrame(0, &frame)) || FAILED(frame->GetSize(&width, &height)) || !width || !height || width > 512 || height > 512 ||
      FAILED(images_->CreateFormatConverter(&converter)) ||
      FAILED(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom)) ||
      FAILED(target_->CreateBitmapFromWicBitmap(converter.Get(), nullptr, &bitmap))) bitmap = nullptr;
  if (icons_.size() > 64) icons_.clear();
  return (icons_[png] = bitmap).Get();
}

bool SplitOverlay::Show(HWND owner, const RECT& bounds, COLORREF background, float background_alpha,
                        const std::vector<OverlayCard>& cards, float scale, const RECT* hole) {
  const int width = bounds.right - bounds.left, height = bounds.bottom - bounds.top;
  if (width < 1 || height < 1 || static_cast<unsigned long long>(width) * height > 16ull * 1024 * 1024) { Hide(); return false; }
  if (!window_) {
    static const ATOM atom = [] {
      WNDCLASSW wc{}; wc.lpfnWndProc = OverlayProc; wc.hInstance = GetModuleHandleW(nullptr); wc.lpszClassName = L"SolSplitOverlay";
      return RegisterClassW(&wc);
    }();
    if (!atom) return false;
    window_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_LAYERED | WS_EX_TRANSPARENT, L"SolSplitOverlay", L"", WS_POPUP,
                              0, 0, 1, 1, owner, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!window_) return false;
  }
  if (!Prepare(width, height, scale)) { Hide(); return false; }
  auto* target = target_.Get(); auto* brush = brush_.Get();
  target->BeginDraw();
  target->Clear(Color(background, background_alpha));
  const float x0 = static_cast<float>(bounds.left), y0 = static_cast<float>(bounds.top);
  for (const auto& card : cards) {
    const float left = (card.rect.left - x0) / scale, top = (card.rect.top - y0) / scale;
    const float right = (card.rect.right - x0) / scale, bottom = (card.rect.bottom - y0) / scale;
    if (right - left < 1 || bottom - top < 1) continue;
    const auto box = D2D1::RoundedRect(D2D1::RectF(left, top, right, bottom), card.radius, card.radius);
    brush->SetColor(Color(card.fill, card.fill_alpha)); target->FillRoundedRectangle(box, brush);
    if (card.pane) {
      // Header strip (30 DIP, like the real pane header): the card's rounded
      // top corners with a square bottom edge.
      const float header = std::min(30.f, bottom - top), r = std::min(card.radius, header / 2);
      brush->SetColor(Color(card.header, 1.f));
      target->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(left, top, right, top + header), r, r), brush);
      target->FillRectangle(D2D1::RectF(left, top + header - r, right, top + header), brush);
      auto* icon = Icon(card.icon);
      brush->SetColor(Color(card.text, .9f));
      if (right - left > 40) {
        const float iy = top + (header - 16) / 2;
        if (icon) target->DrawBitmap(icon, D2D1::RectF(left + 10, iy, left + 26, iy + 16));
        else DrawGlobe(target, brush, D2D1::Point2F(left + 18, top + header / 2), 6.5f, 1.f);
        if (label_ && right - left > 70)
          target->DrawText(card.title.c_str(), static_cast<UINT32>(card.title.size()), label_.Get(),
                           D2D1::RectF(left + 34, top, right - 10, top + header), brush);
      }
      // The page's icon in the middle of its body: which page lands where.
      const float body = bottom - top - header, size = std::min({40.f, body * .4f, (right - left) * .4f});
      if (size >= 12) {
        const float cx = (left + right) / 2, cy = top + header + body / 2;
        if (icon) target->DrawBitmap(icon, D2D1::RectF(cx - size / 2, cy - size / 2, cx + size / 2, cy + size / 2), card.accent ? 1.f : .55f);
        else { brush->SetColor(Color(card.text, card.accent ? .8f : .4f)); DrawGlobe(target, brush, D2D1::Point2F(cx, cy), size / 2, 1.6f); }
      }
    }
    if (card.border_width > 0 && card.border_alpha > 0) {
      const float inset = card.border_width / 2;
      brush->SetColor(Color(card.border, card.border_alpha));
      target->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(left + inset, top + inset, right - inset, bottom - inset), card.radius, card.radius),
                                   brush, card.border_width);
    }
  }
  if (FAILED(target->EndDraw())) { ReleaseSurface(); Hide(); return false; }
  SIZE size{width, height}; POINT point{bounds.left, bounds.top}, origin{};
  BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
  if (!UpdateLayeredWindow(window_, nullptr, &point, &size, dc_, &origin, 0, &blend, ULW_ALPHA)) { Hide(); return false; }
  // A window region keeps a hole (the open floating sidebar) uncovered and out of hit-testing.
  RECT cut{};
  if (hole && IntersectRect(&cut, hole, &bounds)) OffsetRect(&cut, -bounds.left, -bounds.top); else cut = {};
  if (!EqualRect(&cut, &hole_)) {
    HRGN region = nullptr;
    if (!IsRectEmpty(&cut)) {
      region = CreateRectRgn(0, 0, width, height);
      if (const HRGN gap = CreateRectRgnIndirect(&cut)) { CombineRgn(region, region, gap, RGN_DIFF); DeleteObject(gap); }
    }
    SetWindowRgn(window_, region, TRUE);  // The system owns |region| afterwards.
    hole_ = cut;
  }
  if (!visible_) SetWindowPos(window_, HWND_TOP, bounds.left, bounds.top, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
  visible_ = true;
  return true;
}
}  // namespace red
