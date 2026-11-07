#include "windows/page_corners.h"
#include <wincodec.h>

using Microsoft::WRL::ComPtr;
namespace red {
namespace {
D2D1::ColorF Color(COLORREF c) { return D2D1::ColorF(GetRValue(c) / 255.f, GetGValue(c) / 255.f, GetBValue(c) / 255.f); }
}  // namespace
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
}  // namespace red
