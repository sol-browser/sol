#include "windows/image_scale.h"
#include <windows.h>
#include <objbase.h>
#include <shlwapi.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <algorithm>
#include <cmath>
#include <cstring>

using Microsoft::WRL::ComPtr;

namespace red {
namespace {
// COM for the length of one call; a thread that already has another apartment keeps it.
class Com {
 public:
  Com() : result_(CoInitializeEx(nullptr, COINIT_MULTITHREADED)) {}
  ~Com() { if (SUCCEEDED(result_)) CoUninitialize(); }
  bool usable() const { return SUCCEEDED(result_) || result_ == RPC_E_CHANGED_MODE; }
 private:
  HRESULT result_;
};

ComPtr<IWICBitmapSource> Decode(IWICImagingFactory* factory, const std::string& encoded) {
  ComPtr<IWICStream> stream; ComPtr<IWICBitmapDecoder> decoder; ComPtr<IWICBitmapFrameDecode> frame;
  if (encoded.empty() || encoded.size() > 64u * 1024 * 1024 || FAILED(factory->CreateStream(&stream)) ||
      FAILED(stream->InitializeFromMemory(reinterpret_cast<BYTE*>(const_cast<char*>(encoded.data())), static_cast<DWORD>(encoded.size()))) ||
      FAILED(factory->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnLoad, &decoder)) || FAILED(decoder->GetFrame(0, &frame)))
    return nullptr;
  UINT width = 0, height = 0;
  if (FAILED(frame->GetSize(&width, &height)) || !width || !height || static_cast<unsigned long long>(width) * height > 64ull * 1024 * 1024) return nullptr;
  return frame;
}

std::optional<std::string> Encode(IWICImagingFactory* factory, IWICBitmapSource* source, REFGUID container, float quality) {
  ComPtr<IStream> memory(SHCreateMemStream(nullptr, 0));
  ComPtr<IWICBitmapEncoder> encoder; ComPtr<IWICBitmapFrameEncode> frame; ComPtr<IPropertyBag2> options;
  if (!memory || FAILED(factory->CreateEncoder(container, nullptr, &encoder)) || FAILED(encoder->Initialize(memory.Get(), WICBitmapEncoderNoCache)) ||
      FAILED(encoder->CreateNewFrame(&frame, &options)))
    return std::nullopt;
  if (container == GUID_ContainerFormatJpeg && options) {
    PROPBAG2 name{}; name.pstrName = const_cast<LPOLESTR>(L"ImageQuality");
    VARIANT value{}; value.vt = VT_R4; value.fltVal = std::clamp(quality, 0.f, 1.f);
    options->Write(1, &name, &value);
  }
  UINT width = 0, height = 0;
  WICPixelFormatGUID format = container == GUID_ContainerFormatJpeg ? GUID_WICPixelFormat24bppBGR : GUID_WICPixelFormat32bppBGRA;
  ComPtr<IWICFormatConverter> converter;
  if (FAILED(frame->Initialize(options.Get())) || FAILED(source->GetSize(&width, &height)) || FAILED(frame->SetSize(width, height)) ||
      FAILED(frame->SetPixelFormat(&format)) || FAILED(factory->CreateFormatConverter(&converter)) ||
      FAILED(converter->Initialize(source, format, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom)) ||
      FAILED(frame->WriteSource(converter.Get(), nullptr)) || FAILED(frame->Commit()) || FAILED(encoder->Commit()))
    return std::nullopt;
  STATSTG stat{};
  if (FAILED(memory->Stat(&stat, STATFLAG_NONAME)) || stat.cbSize.QuadPart > 64ull * 1024 * 1024) return std::nullopt;
  std::string out(static_cast<size_t>(stat.cbSize.QuadPart), '\0');
  LARGE_INTEGER start{}; ULONG read = 0;
  if (FAILED(memory->Seek(start, STREAM_SEEK_SET, nullptr)) || FAILED(memory->Read(out.data(), static_cast<ULONG>(out.size()), &read)) || read != out.size())
    return std::nullopt;
  return out;
}

ComPtr<IWICImagingFactory> Factory() {
  ComPtr<IWICImagingFactory> factory;
  CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
  return factory;
}
}  // namespace

std::optional<std::string> ScaledJpeg(const std::string& encoded, unsigned max_width, float quality) {
  const Com com;
  const auto factory = com.usable() ? Factory() : nullptr;
  const auto source = factory ? Decode(factory.Get(), encoded) : nullptr;
  if (!source || !max_width) return std::nullopt;
  UINT width = 0, height = 0;
  source->GetSize(&width, &height);
  if (width <= max_width) return Encode(factory.Get(), source.Get(), GUID_ContainerFormatJpeg, quality);
  const UINT scaled_height = std::max<UINT>(1, static_cast<UINT>(std::lround(double(height) * max_width / width)));
  ComPtr<IWICBitmapScaler> scaler;
  if (FAILED(factory->CreateBitmapScaler(&scaler)) ||
      FAILED(scaler->Initialize(source.Get(), max_width, scaled_height, WICBitmapInterpolationModeHighQualityCubic)))
    return std::nullopt;
  return Encode(factory.Get(), scaler.Get(), GUID_ContainerFormatJpeg, quality);
}

std::optional<std::string> CroppedPng(const std::string& encoded, double left, double top, double fraction_width, double fraction_height) {
  const Com com;
  const auto factory = com.usable() ? Factory() : nullptr;
  const auto source = factory ? Decode(factory.Get(), encoded) : nullptr;
  if (!source || !std::isfinite(left) || !std::isfinite(top) || !std::isfinite(fraction_width) || !std::isfinite(fraction_height)) return std::nullopt;
  UINT full_width = 0, full_height = 0;
  source->GetSize(&full_width, &full_height);
  int x = static_cast<int>(std::lround(left * full_width)), y = static_cast<int>(std::lround(top * full_height));
  int width = static_cast<int>(std::lround(fraction_width * full_width)), height = static_cast<int>(std::lround(fraction_height * full_height));
  // Rounding at the edges may reach one pixel past the picture: keep inside it.
  x = std::clamp(x, 0, static_cast<int>(full_width) - 1); y = std::clamp(y, 0, static_cast<int>(full_height) - 1);
  width = std::min(width, static_cast<int>(full_width) - x); height = std::min(height, static_cast<int>(full_height) - y);
  if (width < 1 || height < 1) return std::nullopt;
  ComPtr<IWICBitmapClipper> clipper;
  const WICRect rect{x, y, width, height};
  if (FAILED(factory->CreateBitmapClipper(&clipper)) || FAILED(clipper->Initialize(source.Get(), &rect))) return std::nullopt;
  return Encode(factory.Get(), clipper.Get(), GUID_ContainerFormatPng, 1);
}

std::optional<std::string> PackedDib(const std::string& encoded, unsigned long long max_pixels) {
  const Com com;
  const auto factory = com.usable() ? Factory() : nullptr;
  const auto source = factory ? Decode(factory.Get(), encoded) : nullptr;
  if (!source) return std::nullopt;
  UINT width = 0, height = 0;
  source->GetSize(&width, &height);
  if (static_cast<unsigned long long>(width) * height > max_pixels) return std::nullopt;
  ComPtr<IWICBitmapSource> bgra;
  if (FAILED(WICConvertBitmapSource(GUID_WICPixelFormat32bppBGRA, source.Get(), &bgra))) return std::nullopt;
  const UINT stride = width * 4;
  std::string pixels(static_cast<size_t>(stride) * height, '\0');
  if (FAILED(bgra->CopyPixels(nullptr, stride, static_cast<UINT>(pixels.size()), reinterpret_cast<BYTE*>(pixels.data())))) return std::nullopt;
  BITMAPINFOHEADER header{};
  header.biSize = sizeof(header); header.biWidth = static_cast<LONG>(width); header.biHeight = static_cast<LONG>(height);  // Bottom-up.
  header.biPlanes = 1; header.biBitCount = 32; header.biCompression = BI_RGB; header.biSizeImage = static_cast<DWORD>(pixels.size());
  std::string dib(sizeof(header) + pixels.size(), '\0');
  std::memcpy(dib.data(), &header, sizeof(header));
  for (UINT row = 0; row < height; ++row) {
    char* line = dib.data() + sizeof(header) + static_cast<size_t>(height - 1 - row) * stride;
    std::memcpy(line, pixels.data() + static_cast<size_t>(row) * stride, stride);
    for (UINT x = 0; x < width; ++x) line[x * 4 + 3] = static_cast<char>(0xff);  // A page picture is opaque.
  }
  return dib;
}
}  // namespace red
