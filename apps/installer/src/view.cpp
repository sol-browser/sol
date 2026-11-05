#include "view.h"
#include "logo_shape.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <iterator>

namespace sol_installer {
namespace {
using Microsoft::WRL::ComPtr;
constexpr float kPi = 3.14159265f;
// Sol's sun yellow (#F9CA30), as in the Windows tile icons.
constexpr UINT32 kSun = 0xF9CA30;
constexpr D2D1_POINT_2F kMark{kWidth / 2, 122};
constexpr float kMarkRadius = 54;
constexpr D2D1_RECT_F kClose{kWidth - 46, 10, kWidth - 14, 42};
constexpr D2D1_RECT_F kPrimary{kWidth / 2 - 66, 288, kWidth / 2 + 66, 324};

bool Inside(const D2D1_RECT_F& r, float x, float y) { return x >= r.left && x < r.right && y >= r.top && y < r.bottom; }
float Ease(float t) { t = std::clamp(t, 0.f, 1.f); return 1 - (1 - t) * (1 - t) * (1 - t); }
float Approach(float value, float target, double rate, double dt) {
  return target + (value - target) * static_cast<float>(std::exp(-rate * dt));
}
D2D1_COLOR_F Color(UINT32 rgb, float alpha) { return D2D1::ColorF(rgb, alpha); }
D2D1_COLOR_F White(float alpha) { return D2D1::ColorF(1, 1, 1, alpha); }

// The logo's wave as a function of x (unit circle): the lowest point of the
// hole's edge at each x, or the ring's inner edge where there is no wave.
float LogoSurface(float x) {
  constexpr int kBins = 129;
  static const auto table = [] {
    std::array<float, kBins> lowest{};
    for (int i = 0; i < kBins; ++i) {
      const float bx = -1.f + 2.f * float(i) / (kBins - 1);
      lowest[i] = std::sqrt(std::max(0.f, logo::kRingInner * logo::kRingInner - bx * bx));
    }
    std::array<float, kBins> found;
    found.fill(-2.f);
    logo::Point from = logo::kInnerStart;
    for (const auto& c : logo::kInner) {
      for (int step = 0; step <= 64; ++step) {
        const float u = float(step) / 64, v = 1 - u;
        const float px = v * v * v * from.x + 3 * v * v * u * c[0].x + 3 * v * u * u * c[1].x + u * u * u * c[2].x;
        const float py = v * v * v * from.y + 3 * v * v * u * c[0].y + 3 * v * u * u * c[1].y + u * u * u * c[2].y;
        const int bin = int(std::lround((px + 1.f) / 2.f * (kBins - 1)));
        if (bin >= 0 && bin < kBins && py > 0) found[bin] = std::max(found[bin], py);
      }
      from = c[2];
    }
    for (int i = 0; i < kBins; ++i) if (found[i] > -2.f) lowest[i] = std::min(lowest[i], found[i]);
    return lowest;
  }();
  const float at = std::clamp((x + 1.f) / 2.f * (kBins - 1), 0.f, float(kBins - 1));
  const int i = std::min(int(at), kBins - 2);
  return table[i] + (table[i + 1] - table[i]) * (at - float(i));
}

std::wstring Megabytes(uint64_t bytes) { return std::to_wstring((bytes + (1 << 19)) >> 20); }

std::wstring Title(const Model& model) {
  switch (model.phase) {
    case Phase::Checking: return L"Getting Sol ready";
    case Phase::Downloading: return L"Downloading Sol";
    case Phase::Installing: return L"Installing Sol";
    case Phase::Opening: return L"Sol is ready";
    case Phase::UpToDate: return L"Sol is up to date";
    case Phase::Failed: return L"Couldn’t install Sol";
  }
  return {};
}

std::wstring Detail(const Model& model) {
  const std::wstring name = model.version.empty() ? L"Sol" : L"Sol " + model.version;
  switch (model.phase) {
    case Phase::Checking: return L"Looking for the latest version…";
    case Phase::Downloading:
      return model.total ? name + L"  ·  " + Megabytes(model.received) + L" of " + Megabytes(model.total) + L" MB"
                         : name + L"  ·  " + Megabytes(model.received) + L" MB";
    case Phase::Installing: return name + L"  ·  this takes a few seconds";
    case Phase::Opening:
    case Phase::UpToDate: return L"Opening " + name + L"…";
    case Phase::Failed: return model.error;
  }
  return {};
}
}  // namespace

float TargetFill(const Model& model) {
  switch (model.phase) {
    case Phase::Checking: return .05f;
    case Phase::Downloading: return .05f + .95f * (model.total ? std::min(1.f, float(double(model.received) / double(model.total))) : 0.f);
    case Phase::Installing:
    case Phase::Opening:
    case Phase::UpToDate: return 1;
    case Phase::Failed: return 0;
  }
  return 0;
}

float TargetMorph(const Model& model) {
  return model.phase == Phase::Installing || model.phase == Phase::Opening || model.phase == Phase::UpToDate ? 1.f : 0.f;
}

void Animate(Motion& motion, const Model& model, Hit hover, bool pressed, double dt) {
  motion.time += dt;
  motion.fill = Approach(motion.fill, TargetFill(model), 4, dt);
  // The water sinks into the logo only once the ring is full.
  const float morph = TargetMorph(model);
  motion.morph = Approach(motion.morph, morph > 0 && motion.fill < .97f ? motion.morph : morph, 1.6, dt);
  const bool done = model.phase == Phase::Opening || model.phase == Phase::UpToDate;
  motion.settle = Approach(motion.settle, done && motion.morph > .9f ? 1.f : 0.f, 2.2, dt);
  motion.close_hover = Approach(motion.close_hover, hover == Hit::Close ? 1.f : 0.f, 14, dt);
  motion.primary_hover = Approach(motion.primary_hover, hover == Hit::Primary ? 1.f : 0.f, 14, dt);
  motion.primary_press = Approach(motion.primary_press, hover == Hit::Primary && pressed ? 1.f : 0.f, 24, dt);
}

Hit HitTest(const Model& model, float x, float y) {
  const bool busy = model.phase == Phase::Installing || model.phase == Phase::Opening || model.phase == Phase::UpToDate;
  if (!busy && Inside(kClose, x, y)) return Hit::Close;
  if (model.phase == Phase::Failed && Inside(kPrimary, x, y)) return Hit::Primary;
  return Hit::Drag;
}

View::View(ID2D1Factory* factory, IDWriteFactory* text) : factory_(factory) {
  const auto format = [&](const wchar_t* family, DWRITE_FONT_WEIGHT weight, float size, ComPtr<IDWriteTextFormat>& out) {
    text->CreateTextFormat(family, nullptr, weight, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, size, L"en-us", &out);
    if (!out) return;
    out->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    out->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
  };
  format(L"Segoe UI Variable Display", DWRITE_FONT_WEIGHT_SEMI_BOLD, 21, title_);
  format(L"Segoe UI Variable Text", DWRITE_FONT_WEIGHT_NORMAL, 13, detail_);
  format(L"Segoe UI Variable Text", DWRITE_FONT_WEIGHT_SEMI_BOLD, 13.5f, button_);
  if (button_) button_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
}

void View::Draw(ID2D1RenderTarget* target, const Model& model, const Motion& motion) {
  target->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
  const float t = static_cast<float>(motion.time);
  const float intro = Ease(t / .8f);

  // Background: a warm near-black, slightly lighter at the top.
  ComPtr<ID2D1GradientStopCollection> stops;
  const D2D1_GRADIENT_STOP background[] = {{0, Color(0x1B1714, 1)}, {1, Color(0x0E0C0B, 1)}};
  target->CreateGradientStopCollection(background, 2, &stops);
  ComPtr<ID2D1LinearGradientBrush> fill;
  target->CreateLinearGradientBrush({{0, 0}, {0, kHeight}}, stops.Get(), &fill);
  target->FillRectangle({0, 0, kWidth, kHeight}, fill.Get());

  // The sun's glow, breathing slowly and growing as the mark fills.
  const float glow = (.09f + .09f * motion.fill * (1 - .5f * motion.morph)) * (.85f + .15f * std::sin(t * 1.3f)) * intro;
  ComPtr<ID2D1GradientStopCollection> halo;
  const D2D1_GRADIENT_STOP glow_stops[] = {{0, Color(kSun, glow)}, {.45f, Color(kSun, glow * .35f)}, {1, Color(kSun, 0)}};
  target->CreateGradientStopCollection(glow_stops, 3, &halo);
  ComPtr<ID2D1RadialGradientBrush> radial;
  target->CreateRadialGradientBrush(D2D1::RadialGradientBrushProperties(kMark, {0, 0}, 170, 170), halo.Get(), &radial);
  target->FillEllipse(D2D1::Ellipse(kMark, 170, 170), radial.Get());

  // The mark: Sol's logo (icons/logo_sol.svg via logo_shape.h), a hairline ring
  // with a wave at its foot. Water fills the ring as Sol downloads; while it
  // installs the water sinks into the logo's wave, which stops rippling once
  // Sol is ready.
  const float radius = kMarkRadius * (.9f + .1f * intro);
  ComPtr<ID2D1SolidColorBrush> sun, back;
  target->CreateSolidColorBrush(Color(kSun, intro), &sun);
  target->CreateSolidColorBrush(Color(kSun, .38f * intro), &back);
  const auto place = [&](float x, float y) { return D2D1::Point2F(kMark.x + radius * x, kMark.y + radius * y); };
  const auto combine = [&](ID2D1Geometry* a, ID2D1Geometry* b, D2D1_COMBINE_MODE mode) {
    ComPtr<ID2D1PathGeometry> path;
    ComPtr<ID2D1GeometrySink> sink;
    if (!a || !b || FAILED(factory_->CreatePathGeometry(&path)) || FAILED(path->Open(&sink))) return ComPtr<ID2D1PathGeometry>();
    a->CombineWithGeometry(b, mode, nullptr, sink.Get());
    sink->Close();
    return path;
  };
  ComPtr<ID2D1PathGeometry> outer;
  ComPtr<ID2D1GeometrySink> sink;
  if (SUCCEEDED(factory_->CreatePathGeometry(&outer)) && SUCCEEDED(outer->Open(&sink))) {
    sink->BeginFigure(place(logo::kOuterStart.x, logo::kOuterStart.y), D2D1_FIGURE_BEGIN_FILLED);
    for (const auto& c : logo::kOuter) sink->AddBezier({place(c[0].x, c[0].y), place(c[1].x, c[1].y), place(c[2].x, c[2].y)});
    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    sink->Close();
  }
  ComPtr<ID2D1EllipseGeometry> inside;
  factory_->CreateEllipseGeometry(D2D1::Ellipse(kMark, radius * logo::kRingInner, radius * logo::kRingInner), &inside);
  if (const auto ring = combine(outer.Get(), inside.Get(), D2D1_COMBINE_MODE_EXCLUDE)) target->FillGeometry(ring.Get(), sun.Get());

  // The water's surface across the ring: level from the download, ripples,
  // blended into the logo's wave by |morph|. A paler second surface behind it,
  // out of phase, gives the water depth.
  const float level = 1 - 2 * motion.fill;
  const float ripple = .05f * (1 - motion.settle);
  const auto water = [&](float phase, float lift) {
    ComPtr<ID2D1PathGeometry> path;
    ComPtr<ID2D1GeometrySink> into;
    if (FAILED(factory_->CreatePathGeometry(&path)) || FAILED(path->Open(&into))) return ComPtr<ID2D1PathGeometry>();
    constexpr int kSteps = 72;
    for (int i = 0; i <= kSteps; ++i) {
      const float x = -1.f + 2.f * float(i) / kSteps;
      const float wave = ripple * (std::sin(x * 3.3f - t * 2.4f + phase) + .5f * std::sin(x * 5.4f + t * 1.7f + phase * 2));
      const float flat = level - lift;
      const float y = flat + (LogoSurface(x) - flat) * motion.morph + wave;
      if (i == 0) into->BeginFigure(place(x, y), D2D1_FIGURE_BEGIN_FILLED);
      else into->AddLine(place(x, y));
    }
    into->AddLine(place(1.1f, 1.2f));
    into->AddLine(place(-1.1f, 1.2f));
    into->EndFigure(D2D1_FIGURE_END_CLOSED);
    into->Close();
    return combine(path.Get(), inside.Get(), D2D1_COMBINE_MODE_INTERSECT);
  };
  if (motion.fill > .001f) {
    if (motion.settle < .999f)
      if (const auto behind = water(2.1f, .04f * (1 - motion.morph))) target->FillGeometry(behind.Get(), back.Get());
    if (const auto front = water(0, 0)) target->FillGeometry(front.Get(), sun.Get());
  }

  // Title and detail.
  ComPtr<ID2D1SolidColorBrush> ink, quiet;
  target->CreateSolidColorBrush(White(.94f * intro), &ink);
  target->CreateSolidColorBrush(White(.56f * intro), &quiet);
  const auto title = Title(model), detail = Detail(model);
  if (title_) target->DrawText(title.c_str(), UINT32(title.size()), title_.Get(), {30, 198, kWidth - 30, 228}, ink.Get());
  if (detail_) target->DrawText(detail.c_str(), UINT32(detail.size()), detail_.Get(), {44, 234, kWidth - 44, 274}, quiet.Get());

  // "Try again" after a failure.
  if (model.phase == Phase::Failed) {
    const float lift = .06f * motion.primary_hover - .05f * motion.primary_press;
    ComPtr<ID2D1SolidColorBrush> pill, label;
    target->CreateSolidColorBrush(D2D1::ColorF(std::min(1.f, .976f + lift), std::min(1.f, .792f + lift), std::min(1.f, .188f + lift)), &pill);
    target->CreateSolidColorBrush(Color(0x1A1408, 1), &label);
    const float h = kPrimary.bottom - kPrimary.top;
    target->FillRoundedRectangle({kPrimary, h / 2, h / 2}, pill.Get());
    static constexpr wchar_t kRetry[] = L"Try again";
    if (button_) target->DrawText(kRetry, UINT32(std::size(kRetry) - 1), button_.Get(), kPrimary, label.Get());
  }

  // Close (cancels a download); hidden while Velopack installs.
  if (HitTest(model, (kClose.left + kClose.right) / 2, (kClose.top + kClose.bottom) / 2) == Hit::Close) {
    const D2D1_POINT_2F c{(kClose.left + kClose.right) / 2, (kClose.top + kClose.bottom) / 2};
    if (motion.close_hover > .01f) {
      ComPtr<ID2D1SolidColorBrush> hover;
      target->CreateSolidColorBrush(White(.09f * motion.close_hover), &hover);
      target->FillEllipse(D2D1::Ellipse(c, 16, 16), hover.Get());
    }
    ComPtr<ID2D1SolidColorBrush> cross;
    target->CreateSolidColorBrush(White((.45f + .45f * motion.close_hover) * intro), &cross);
    ComPtr<ID2D1StrokeStyle> round;
    const auto style = D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND);
    factory_->CreateStrokeStyle(style, nullptr, 0, &round);
    target->DrawLine({c.x - 5, c.y - 5}, {c.x + 5, c.y + 5}, cross.Get(), 1.4f, round.Get());
    target->DrawLine({c.x + 5, c.y - 5}, {c.x - 5, c.y + 5}, cross.Get(), 1.4f, round.Get());
  }
}
}  // namespace sol_installer
