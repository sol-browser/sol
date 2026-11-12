#pragma once
#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <map>
#include <string>
#include <vector>

namespace red {
// A passive owned layered window above the website child windows (never takes
// input or activation), painting Split View previews with Direct2D straight into
// a persistent bitmap (no per-frame factories, decoding or pixel copies), so the
// layout animations stay smooth. Screen pixels.
struct OverlayCard {
  RECT rect{};
  COLORREF fill = 0, border = 0;
  float fill_alpha = 1, border_alpha = 0, border_width = 0, radius = 0;
  // A pane card: a header strip with the page's icon and title, and the icon in its middle.
  bool pane = false, accent = false;
  COLORREF header = 0, text = 0;
  std::wstring title;
  std::string icon;  // PNG bytes.
};
class SplitOverlay {
 public:
  SplitOverlay() = default;
  SplitOverlay(const SplitOverlay&) = delete;
  SplitOverlay& operator=(const SplitOverlay&) = delete;
  ~SplitOverlay();
  // |background_alpha| 0 leaves everything but the cards transparent.
  // |hole| (screen pixels, optional): left uncovered, e.g. the floating sidebar.
  bool Show(HWND owner, const RECT& bounds, COLORREF background, float background_alpha,
            const std::vector<OverlayCard>& cards, float scale, const RECT* hole = nullptr);
  void Hide();
  bool visible() const { return visible_; }

 private:
  bool Prepare(int width, int height, float scale);
  ID2D1Bitmap* Icon(const std::string& png);
  void ReleaseSurface();
  HWND window_ = nullptr;
  bool visible_ = false;
  RECT hole_{};  // The current window-region hole (window coordinates); empty when none.
  Microsoft::WRL::ComPtr<ID2D1Factory> factory_;
  Microsoft::WRL::ComPtr<IWICImagingFactory> images_;
  Microsoft::WRL::ComPtr<IDWriteFactory> fonts_;
  Microsoft::WRL::ComPtr<IDWriteTextFormat> label_;
  Microsoft::WRL::ComPtr<ID2D1DCRenderTarget> target_;
  Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush_;
  HDC dc_ = nullptr;
  HBITMAP bitmap_ = nullptr;
  HGDIOBJ previous_ = nullptr;
  int width_ = 0, height_ = 0;
  float scale_ = 0;
  // Decoded icons belong to |target_|; at most a few dozen pages.
  std::map<std::string, Microsoft::WRL::ComPtr<ID2D1Bitmap>> icons_;
};
}  // namespace red
