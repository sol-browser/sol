#include "windows/page_corners.h"
#include <algorithm>
#include <cmath>

using Microsoft::WRL::ComPtr;

namespace red {
namespace {
struct Stop { double at; double r, g, b; };
constexpr Rgb kAuthoredStops[] = {{0xee, 0xdf, 0xe8}, {0xe5, 0xdf, 0xf0}, {0xdb, 0xde, 0xee}};
std::array<Stop, 3> default_stops = {{{0, 0xee, 0xdf, 0xe8}, {.55, 0xe5, 0xdf, 0xf0}, {1, 0xdb, 0xde, 0xee}}};
std::map<HWND, std::array<Stop, 3>> window_stops;
std::map<HWND,std::pair<Theme,bool>> spatial_themes;
const std::array<Stop, 3>& StopsFor(HWND parent) {
  const auto found = window_stops.find(parent);
  return found == window_stops.end() ? default_stops : found->second;
}
}  // namespace

std::array<COLORREF, 3> WorkspaceBackgroundStops(HWND parent) {
  const auto& stops = StopsFor(parent);
  std::array<COLORREF, 3> result{};
  for (size_t i = 0; i < result.size(); ++i)
    result[i] = RGB(static_cast<BYTE>(stops[i].r), static_cast<BYTE>(stops[i].g), static_cast<BYTE>(stops[i].b));
  return result;
}
void SetWorkspaceBackgroundTheme(const Theme& theme, bool dark, HWND parent) {
  if(theme.palette&&theme.palette->version==2)spatial_themes[parent]={theme,dark};else spatial_themes.erase(parent);
  auto& colors = parent ? window_stops.try_emplace(parent, default_stops).first->second : default_stops;
  const auto palette=theme.palette?PaletteBackgroundStops(theme,dark):std::array<Rgb,3>{};
  for (size_t i = 0; i < colors.size(); ++i) {
    const auto c = theme.palette?palette[i]:Themed(kAuthoredStops[i], theme, dark, true);
    colors[i].r = c.r; colors[i].g = c.g; colors[i].b = c.b;
  }
}

void ForgetWorkspaceBackgroundTheme(HWND parent) { window_stops.erase(parent);spatial_themes.erase(parent); }
bool WorkspaceUsesSpatialPalette(HWND parent) {return spatial_themes.contains(parent);}
COLORREF WorkspaceBackgroundAt(double x, double y, double width, double height, HWND parent) {
  if(const auto found=spatial_themes.find(parent);found!=spatial_themes.end()){
    const auto c=PaletteBackgroundAt(found->second.first,found->second.second,width>0?x/width:.5,height>0?y/height:.5);return RGB(c.r,c.g,c.b);}
  const auto& stops = StopsFor(parent);
  // CSS linear-gradient(155deg): direction (sin 155°, -cos 155°); the gradient
  // line spans |w sin| + |h cos| through the box center.
  constexpr double kRadians = 155.0 * 3.14159265358979323846 / 180.0;
  const double dx = std::sin(kRadians), dy = -std::cos(kRadians);
  const double length = std::abs(width * dx) + std::abs(height * dy);
  const double t = std::clamp(length > 0 ? ((x - width / 2) * dx + (y - height / 2) * dy) / length + .5 : .5, 0.0, 1.0);
  const auto& a = t <= stops[1].at ? stops[0] : stops[1];
  const auto& b = t <= stops[1].at ? stops[1] : stops[2];
  const double f = (t - a.at) / (b.at - a.at);
  auto mix = [f](double p, double q) { return static_cast<BYTE>(std::lround(p + (q - p) * f)); };
  return RGB(mix(a.r, b.r), mix(a.g, b.g), mix(a.b, b.b));
}

bool PageCorners::Init(HWND parent) {
  if (failed_) return false;
  if (device_ && parent_ == parent) return true;
  failed_ = true;  // Until fully initialized.
  UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
  if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, nullptr, 0, D3D11_SDK_VERSION, &d3d_, nullptr, nullptr)) &&
      FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags, nullptr, 0, D3D11_SDK_VERSION, &d3d_, nullptr, nullptr)))
    return false;
  ComPtr<IDXGIDevice> dxgi;
  if (FAILED(d3d_.As(&dxgi))) return false;
  if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory1), nullptr, &d2d_factory_))) return false;
  ComPtr<ID2D1Device> d2d_device;
  if (FAILED(d2d_factory_->CreateDevice(dxgi.Get(), &d2d_device)) ||
      FAILED(d2d_device->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &d2d_))) return false;
  if (FAILED(DCompositionCreateDevice(dxgi.Get(), __uuidof(IDCompositionDevice), &device_))) return false;
  // topmost = TRUE: composed above every child window, including Chromium's.
  if (FAILED(device_->CreateTargetForHwnd(parent, TRUE, &target_)) || FAILED(device_->CreateVisual(&root_)) ||
      FAILED(target_->SetRoot(root_.Get()))) return false;
  for (auto& visual : visuals_) if (FAILED(device_->CreateVisual(&visual)) || FAILED(root_->AddVisual(visual.Get(), FALSE, nullptr))) return false;
  parent_ = parent; radius_ = 0; failed_ = false;
  return true;
}

bool PageCorners::Paint(int corner, int radius, COLORREF color, COLORREF border, float border_width) {
  ComPtr<IDCompositionSurface> surface;
  if (FAILED(device_->CreateSurface(radius, radius, DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_ALPHA_MODE_PREMULTIPLIED, &surface))) return false;
  ComPtr<IDXGISurface> dxgi_surface; POINT offset{};
  if (FAILED(surface->BeginDraw(nullptr, __uuidof(IDXGISurface), &dxgi_surface, &offset))) return false;
  const auto props = D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
                                             D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
  ComPtr<ID2D1Bitmap1> bitmap;
  bool ok = SUCCEEDED(d2d_->CreateBitmapFromDxgiSurface(dxgi_surface.Get(), &props, &bitmap));
  if (ok) {
    // The corner square minus the quarter circle of the page's rounded corner.
    const float r = static_cast<float>(radius);
    const int orientation = corner % 4;
    const float cx = (orientation == 0 || orientation == 2) ? r : 0.f, cy = (orientation == 0 || orientation == 1) ? r : 0.f;
    ComPtr<ID2D1RectangleGeometry> square; ComPtr<ID2D1EllipseGeometry> circle; ComPtr<ID2D1PathGeometry> outside;
    ComPtr<ID2D1GeometrySink> sink; ComPtr<ID2D1SolidColorBrush> brush;
    ok = SUCCEEDED(d2d_factory_->CreateRectangleGeometry(D2D1::RectF(0, 0, r, r), &square)) &&
         SUCCEEDED(d2d_factory_->CreateEllipseGeometry(D2D1::Ellipse(D2D1::Point2F(cx, cy), r, r), &circle)) &&
         SUCCEEDED(d2d_factory_->CreatePathGeometry(&outside)) && SUCCEEDED(outside->Open(&sink)) &&
         SUCCEEDED(square->CombineWithGeometry(circle.Get(), D2D1_COMBINE_MODE_EXCLUDE, nullptr, sink.Get())) &&
         SUCCEEDED(sink->Close());
    if (ok) {
      d2d_->SetTarget(bitmap.Get());
      d2d_->BeginDraw();
      d2d_->SetTransform(D2D1::Matrix3x2F::Translation(static_cast<float>(offset.x), static_cast<float>(offset.y)));
      d2d_->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
      d2d_->PushAxisAlignedClip(D2D1::RectF(0, 0, r, r), D2D1_ANTIALIAS_MODE_ALIASED);
      d2d_->Clear(D2D1::ColorF(0, 0, 0, 0));
      ok = SUCCEEDED(d2d_->CreateSolidColorBrush(D2D1::ColorF(GetRValue(color) / 255.f, GetGValue(color) / 255.f, GetBValue(color) / 255.f), &brush));
      // Split frames (drawn with a border) carry no matte: the engine clips the
      // page itself, so an opaque corner square would only show on Acrylic,
      // Mica or grain. Plain page corners (no border) keep their matte.
      if (ok && border_width <= 0) d2d_->FillGeometry(outside.Get(), brush.Get());
      // The rectangular native page covers the CSS border's curved inner edge.
      // Restore that quarter-ring above it, in the same antialiased surface.
      if (ok && border_width > 0) {
        brush->SetColor(D2D1::ColorF(GetRValue(border)/255.f,GetGValue(border)/255.f,GetBValue(border)/255.f));
        const float middle = r-border_width/2;
        d2d_->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx,cy),middle,middle),brush.Get(),border_width);
      }
      d2d_->PopAxisAlignedClip();
      ok = SUCCEEDED(d2d_->EndDraw()) && ok;
      d2d_->SetTarget(nullptr);
    }
  }
  if (FAILED(surface->EndDraw())) ok = false;
  if (!ok || FAILED(visuals_[corner]->SetContent(surface.Get()))) return false;
  surfaces_[corner] = surface;
  return true;
}

bool PageCorners::Show(HWND parent, const RECT& rect, int radius) {
  return ShowFrames(parent,std::span<const RECT>(&rect,1),radius);
}
bool PageCorners::ShowFrames(HWND parent, std::span<const RECT> frames, int radius, std::span<const COLORREF> borders, float border_width) {
  if (frames.size() > 2 || (!borders.empty() && borders.size() != frames.size()) || border_width < 0 || border_width >= radius) return false;
  if (frames.empty() || radius < 2) { Hide(); return true; }
  if (!Init(parent)) return false;
  RECT client{}; GetClientRect(parent, &client);
  for (size_t i = 0; i < visuals_.size(); ++i) {
    if (i / 4 >= frames.size()) { visuals_[i]->SetContent(nullptr); continue; }
    const auto& rect = frames[i / 4];
    if (rect.right-rect.left < 2*radius || rect.bottom-rect.top < 2*radius) { visuals_[i]->SetContent(nullptr); continue; }
    const POINT at[4] = {{rect.left, rect.top}, {rect.right - radius, rect.top}, {rect.left, rect.bottom - radius}, {rect.right - radius, rect.bottom - radius}};
    const auto point = at[i % 4];
    const auto border = borders.empty() ? COLORREF{} : borders[i/4];
    const COLORREF color = WorkspaceBackgroundAt(point.x + radius / 2.0, point.y + radius / 2.0, client.right, client.bottom, parent);
    if (radius != radius_ || color != colors_[i] || border != borders_[i] || border_width != border_width_ || !surfaces_[i]) {
      colors_[i] = color; borders_[i] = border;
      if (!Paint(static_cast<int>(i), radius, color, border, border_width)) { failed_ = true; return false; }
    } else if (FAILED(visuals_[i]->SetContent(surfaces_[i].Get()))) { failed_ = true; return false; }
    visuals_[i]->SetOffsetX(static_cast<float>(point.x));
    visuals_[i]->SetOffsetY(static_cast<float>(point.y));
  }
  radius_ = radius;
  border_width_ = border_width;
  if (FAILED(device_->Commit())) { failed_ = true; return false; }
  visible_ = true;
  return true;
}

void PageCorners::Hide() {
  if (!visible_ || !device_) { visible_ = false; return; }
  for (auto& visual : visuals_) visual->SetContent(nullptr);
  device_->Commit();
  visible_ = false;
}
bool PageCorners::ShowSidebarShadow(HWND parent, const RECT& panel, float scale, double progress) {
  if (!Init(parent) || !sidebar_shadow_.Size(device_.Get(), d2d_.Get(), root_.Get(),
      panel.right - panel.left, panel.bottom - panel.top, scale)) return false;
  sidebar_shadow_.Place(panel.left, panel.top, progress);
  return true;
}
bool PageCorners::ShowLinkPeekShadow(HWND parent,const RECT& panel,float scale) {
  if (!Init(parent) || !link_peek_shadow_.Size(device_.Get(),d2d_.Get(),root_.Get(),panel.right-panel.left,panel.bottom-panel.top,scale)) return false;
  link_peek_shadow_.Place(panel.left,panel.top,1); return true;
}
}  // namespace red
