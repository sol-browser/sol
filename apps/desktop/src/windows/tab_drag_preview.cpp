#include "windows/tab_drag_preview.h"
#include "windows/pointer.h"
#include <d2d1.h>
#include <dwrite.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <cmath>
#include "history/favicon_cache.h"
#include "workspace/controller.h"
#include "include/base/cef_callback.h"
#include "include/cef_parser.h"
#include "include/views/cef_display.h"
#include "include/wrapper/cef_closure_task.h"
using Microsoft::WRL::ComPtr;
namespace red {
namespace {
class Preview;
std::map<const State*, std::shared_ptr<Preview>> previews;  // CEF UI thread only.
LRESULT CALLBACK PreviewProc(HWND window, UINT message, WPARAM w, LPARAM l) {
  if (message == WM_NCHITTEST) return HTTRANSPARENT;
  if (message == WM_MOUSEACTIVATE) return MA_NOACTIVATE;
  return DefWindowProcW(window, message, w, l);
}
struct Dib {
  HDC dc = CreateCompatibleDC(nullptr);
  HBITMAP bitmap = nullptr; HGDIOBJ previous = nullptr;
  ~Dib() { if (previous) SelectObject(dc, previous); if (bitmap) DeleteObject(bitmap); if (dc) DeleteDC(dc); }
};
// Passive owned preview above native website children; never receives input.
class SplitPreview {
 public:
  ~SplitPreview() { if (window_) DestroyWindow(window_); }
  void Hide() { if (window_) ShowWindow(window_,SW_HIDE); }
  void Show(const std::shared_ptr<State>& state, const CefRect& rect, float scale) {
    if (rect.width < 1 || rect.height < 1 || static_cast<unsigned long long>(rect.width)*rect.height > 16*1024*1024) { Hide(); return; }
    if (!window_) {
      static const ATOM atom = [] {
        WNDCLASSW wc{}; wc.lpfnWndProc = PreviewProc; wc.hInstance = GetModuleHandleW(nullptr); wc.lpszClassName = L"RedSplitDropPreview";
        return RegisterClassW(&wc);
      }();
      if (!atom) return;
      window_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_LAYERED | WS_EX_TRANSPARENT,
        L"RedSplitDropPreview",L"",WS_POPUP,0,0,1,1,state->window->GetWindowHandle(),nullptr,GetModuleHandleW(nullptr),nullptr);
      if (!window_) return;
    }
    if (rect == bounds_ && scale == scale_) { ShowWindow(window_,SW_SHOWNA); return; }
    bounds_ = rect; scale_ = scale;
    ComPtr<IWICImagingFactory> images; ComPtr<IWICBitmap> image; ComPtr<ID2D1Factory> factory; ComPtr<ID2D1RenderTarget> target;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&images))) ||
        FAILED(images->CreateBitmap(rect.width,rect.height,GUID_WICPixelFormat32bppPBGRA,WICBitmapCacheOnLoad,&image)) ||
        FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,factory.GetAddressOf()))) { Hide(); return; }
    const auto props = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT,D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED));
    if (FAILED(factory->CreateWicBitmapRenderTarget(image.Get(),props,&target))) { Hide(); return; }
    const auto color = Themed({154,119,179},state->theme,state->theme_dark);
    const auto rgb = D2D1::ColorF(color.r/255.f,color.g/255.f,color.b/255.f,.22f);
    ComPtr<ID2D1SolidColorBrush> brush; if (FAILED(target->CreateSolidColorBrush(rgb,&brush))) { Hide(); return; }
    target->BeginDraw(); target->Clear(D2D1::ColorF(0,0.f));
    const float inset = 2*scale;
    const auto box = D2D1::RoundedRect(D2D1::RectF(inset,inset,rect.width-inset,rect.height-inset),8*scale,8*scale);
    target->FillRoundedRectangle(box,brush.Get()); brush->SetOpacity(1.f);
    brush->SetColor(D2D1::ColorF(color.r/255.f,color.g/255.f,color.b/255.f,.85f)); target->DrawRoundedRectangle(box,brush.Get(),2*scale);
    if (FAILED(target->EndDraw())) { Hide(); return; }
    Dib dib; BITMAPINFO info{}; info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER); info.bmiHeader.biWidth = rect.width;
    info.bmiHeader.biHeight = -rect.height; info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32; info.bmiHeader.biCompression = BI_RGB;
    void* pixels = nullptr; dib.bitmap = CreateDIBSection(dib.dc,&info,DIB_RGB_COLORS,&pixels,nullptr,0);
    if (!dib.bitmap || FAILED(image->CopyPixels(nullptr,rect.width*4,rect.width*rect.height*4,static_cast<BYTE*>(pixels)))) { Hide(); return; }
    dib.previous = SelectObject(dib.dc,dib.bitmap); SIZE size{rect.width,rect.height}; POINT point{rect.x,rect.y}, origin{};
    BLENDFUNCTION blend{AC_SRC_OVER,0,255,AC_SRC_ALPHA};
    if (!UpdateLayeredWindow(window_,nullptr,&point,&size,dib.dc,&origin,0,&blend,ULW_ALPHA)) { Hide(); return; }
    SetWindowPos(window_,HWND_TOP,rect.x,rect.y,0,0,SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
  }
 private:
  HWND window_ = nullptr; CefRect bounds_; float scale_ = 0;
};
class Preview : public std::enable_shared_from_this<Preview> {
 public:
  explicit Preview(std::weak_ptr<State> state) : state_(std::move(state)) {}
  ~Preview() { if (IsWindow(window_)) DestroyWindow(window_); }
  bool Show(const std::string& id) {
    id_ = id;
    auto state = state_.lock(); if (!state || !state->window || !state->workspace) return false;
    // CEF child values belong to their parent dictionary. Keep the snapshot
    // alive while reading its node list, rather than retaining a child of a
    // temporary dictionary (which becomes invalid immediately).
    const auto snapshot = state->workspace->Snapshot();
    auto rows = snapshot->GetList("nodes");
    auto diagnostic = CefDictionaryValue::Create(); diagnostic->SetInt("rows", static_cast<int>(rows->GetSize()));
    state->Record("tab_drag_preview_begin", diagnostic);
    for (size_t i = 0; i < rows->GetSize(); ++i) {
      auto row = rows->GetDictionary(i); if (row->GetString("id") != id) continue;
      title_ = row->GetString("title").ToWString();
      if (!row->GetBool("customTitle") && !row->GetString("pageTitle").empty()) title_ = row->GetString("pageTitle").ToWString();
      if (row->GetString("splitAnchor") == id) for (size_t j = 0; j < rows->GetSize(); ++j) {
        const auto other = rows->GetDictionary(j); if (other->GetString("id") != row->GetString("splitOther")) continue;
        const auto name = (!other->GetBool("customTitle") && !other->GetString("pageTitle").empty() ? other->GetString("pageTitle") : other->GetString("title")).ToWString();
        title_ = row->GetBool("splitReversed") ? name + L" | " + title_ : title_ + L" | " + name; break;
      }
      CefURLParts parts;
      if (state->favicons && CefParseURL(row->GetString("favicon"), parts))
        icon_ = state->favicons->PngForPath(CefString(&parts.path).ToString()).value_or("");
      break;
    }
    if (title_.empty()) return false;
    static const ATOM atom = [] {
      WNDCLASSW wc{}; wc.lpfnWndProc = PreviewProc; wc.hInstance = GetModuleHandleW(nullptr);
      wc.lpszClassName = L"RedTabDragPreview"; wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
      return RegisterClassW(&wc);
    }();
    if (!atom) return false;
    window_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST,
        L"RedTabDragPreview", L"", WS_POPUP, 0, 0, 1, 1,
        state->window->GetWindowHandle(), nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!window_) return false;
    state->Record("tab_drag_preview_created");
    Tick(); return true;
  }
 private:
  bool Paint(float scale, POINT point) {
    auto state = state_.lock(); if (!state) return false;
    constexpr float width = 284, height = 60;
    const int w = static_cast<int>(std::lround(width * scale)), h = static_cast<int>(std::lround(height * scale));
    paint_stage_ = 1;
    ComPtr<IWICImagingFactory> images;
    ComPtr<IWICBitmap> image;
    ComPtr<ID2D1Factory> factory;
    ComPtr<ID2D1RenderTarget> target;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&images))) ||
        FAILED(images->CreateBitmap(w, h, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, &image)) ||
        FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, factory.GetAddressOf()))) return false;
    const auto properties = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96 * scale, 96 * scale);
    paint_stage_ = 2;
    if (FAILED(factory->CreateWicBitmapRenderTarget(image.Get(), properties, &target))) return false;
    ComPtr<ID2D1SolidColorBrush> brush;
    ComPtr<IDWriteFactory> fonts; ComPtr<IDWriteTextFormat> text;
    paint_stage_ = 3;
    if (FAILED(target->CreateSolidColorBrush(D2D1::ColorF(0, 0.f), &brush)) ||
        FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(fonts.GetAddressOf()))) ||
        FAILED(fonts->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_MEDIUM, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, 12.5f, L"en-US", &text))) return false;
    text->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP); text->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    ComPtr<IDWriteInlineObject> ellipsis;
    if (SUCCEEDED(fonts->CreateEllipsisTrimmingSign(text.Get(), &ellipsis))) {
      DWRITE_TRIMMING trimming{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0}; text->SetTrimming(&trimming, ellipsis.Get());
    }
    target->BeginDraw(); target->Clear(D2D1::ColorF(0, 0.f));
    for (int ring = 9; ring >= 1; --ring) {
      brush->SetColor(D2D1::ColorF(0, .015f));
      const float r = static_cast<float>(ring);
      target->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(10-r, 12-r, width-10+r, 52+r/2), 8+r/2, 8+r/2), brush.Get());
    }
    const auto bg = Themed({237, 229, 242}, state->theme, state->theme_dark);
    brush->SetColor(D2D1::ColorF(bg.r/255.f, bg.g/255.f, bg.b/255.f, .97f));
    const auto card = D2D1::RoundedRect(D2D1::RectF(10, 8, width-10, 48), 8, 8);
    target->FillRoundedRectangle(card, brush.Get());
    brush->SetColor(D2D1::ColorF(1.f, 1.f, 1.f, state->theme_dark ? .14f : .65f));
    target->DrawRoundedRectangle(card, brush.Get(), .8f);
    bool painted_icon = false;
    if (!icon_.empty() && icon_.size() < 1024 * 1024) {
      ComPtr<IWICStream> stream; ComPtr<IWICBitmapDecoder> decoder; ComPtr<IWICBitmapFrameDecode> frame;
      ComPtr<IWICFormatConverter> converter; ComPtr<ID2D1Bitmap> bitmap;
      if (SUCCEEDED(images->CreateStream(&stream)) && SUCCEEDED(stream->InitializeFromMemory(
              reinterpret_cast<BYTE*>(icon_.data()), static_cast<DWORD>(icon_.size()))) &&
          SUCCEEDED(images->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnDemand, &decoder)) &&
          SUCCEEDED(decoder->GetFrame(0, &frame))) {
        UINT iw = 0, ih = 0; frame->GetSize(&iw, &ih);
        if (iw && ih && iw <= 512 && ih <= 512 && SUCCEEDED(images->CreateFormatConverter(&converter)) &&
            SUCCEEDED(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom)) &&
            SUCCEEDED(target->CreateBitmapFromWicBitmap(converter.Get(), nullptr, &bitmap))) {
          target->DrawBitmap(bitmap.Get(), D2D1::RectF(22, 20, 38, 36)); painted_icon = true;
        }
      }
    }
    const float fg = state->theme_dark ? .94f : .21f;
    brush->SetColor(D2D1::ColorF(fg, fg, fg, .9f));
    if (!painted_icon) {
      target->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(30, 28), 7, 7), brush.Get(), 1.f);
      target->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(30, 28), 3, 7), brush.Get(), .8f);
      target->DrawLine(D2D1::Point2F(23, 28), D2D1::Point2F(37, 28), brush.Get(), .8f);
    }
    target->DrawText(title_.c_str(), static_cast<UINT32>(title_.size()), text.Get(), D2D1::RectF(50, 8, width-23, 48), brush.Get());
    paint_stage_ = 4;
    if (FAILED(target->EndDraw())) return false;
    paint_stage_ = 5;
    Dib dib; if (!dib.dc) return false;
    BITMAPINFO info{}; info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER); info.bmiHeader.biWidth = w;
    info.bmiHeader.biHeight = -h; info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB; void* pixels = nullptr;
    dib.bitmap = CreateDIBSection(dib.dc, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
    if (!dib.bitmap || FAILED(image->CopyPixels(nullptr, w*4, w*h*4, static_cast<BYTE*>(pixels)))) return false;
    dib.previous = SelectObject(dib.dc, dib.bitmap);
    SIZE size{w,h}; POINT origin{};
    BLENDFUNCTION blend{AC_SRC_OVER, 0, 210, AC_SRC_ALPHA};
    paint_stage_ = 6;
    return UpdateLayeredWindow(window_, nullptr, &point, &size, dib.dc, &origin, 0, &blend, ULW_ALPHA) != FALSE;
  }
  void Tick() {
    auto state = state_.lock();
    if (!state || !state->window || !state->tab_dragging || !PrimaryButtonDown()) {
      if (state) HideTabDragPreview(state); return;
    }
    POINT point{}; GetCursorPos(&point);
    const auto display = CefDisplay::GetDisplayNearestPoint(CefPoint(point.x, point.y), true);
    const float scale = display ? display->GetDeviceScaleFactor() : 1.f;
    if (auto drop = state->workspace->SplitDropAt(id_,point)) drop_.Show(state,drop->preview,scale);
    else drop_.Hide();
    // Hold slightly left of center, with the card extending further right.
    // Include transparent shadow padding in the pointer offset.
    point.x -= static_cast<LONG>(116 * scale); point.y -= static_cast<LONG>(28 * scale);
    if (scale_ != scale) {
      scale_ = scale;
      const bool painted = Paint(scale, point);
      auto data = CefDictionaryValue::Create(); data->SetBool("painted", painted); data->SetInt("stage", paint_stage_);
      data->SetInt("error", static_cast<int>(GetLastError()));
      state->Record("tab_drag_preview_paint", data);
      if (!painted) { HideTabDragPreview(state); return; }
    }
    SetWindowPos(window_, HWND_TOPMOST, point.x, point.y, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    CefPostDelayedTask(TID_UI, base::BindOnce([](std::weak_ptr<Preview> weak) { if (auto self = weak.lock()) self->Tick(); }, weak_from_this()), 15);
  }
  std::weak_ptr<State> state_; HWND window_ = nullptr; float scale_ = 0;
  int paint_stage_ = 0;
  std::string id_; SplitPreview drop_;
  std::wstring title_; std::string icon_;
};
}
void ShowTabDragPreview(const std::shared_ptr<State>& state, const std::string& id) {
  HideTabDragPreview(state);
  auto preview = std::make_shared<Preview>(state); previews[state.get()] = preview;
  if (!preview->Show(id)) previews.erase(state.get());
}
void HideTabDragPreview(const std::shared_ptr<State>& state) { previews.erase(state.get()); }
}
