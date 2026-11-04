#pragma once
#include <windows.h>
#include <d2d1_1.h>
#include <dcomp.h>
#include <wrl/client.h>

namespace red {
// Passive sidebar decoration in the existing main-root composition tree.
// Its panel-shaped hole keeps it off the Chrome UI; no HWND/input/padded child.
class SidebarShadow {
 public:
  static int Margin(float scale);
  bool Size(IDCompositionDevice* device, ID2D1DeviceContext* context,
            IDCompositionVisual* root, int width, int height, float scale);
  void Place(int x, int y, double progress);
  void Hide();
  void Reset();
 private:
  bool Init(IDCompositionDevice* device, ID2D1DeviceContext* context, IDCompositionVisual* root);
  Microsoft::WRL::ComPtr<ID2D1DeviceContext> context_;
  Microsoft::WRL::ComPtr<IDCompositionDevice> device_;
  Microsoft::WRL::ComPtr<IDCompositionVisual> root_;
  Microsoft::WRL::ComPtr<IDCompositionVisual> visual_;
  Microsoft::WRL::ComPtr<IDCompositionEffectGroup> opacity_;
  Microsoft::WRL::ComPtr<IDCompositionSurface> surface_;
  int width_ = 0, height_ = 0;
  float scale_ = 0, opacity_value_ = -1;
  int x_ = 0, y_ = 0;
};
}  // namespace red
