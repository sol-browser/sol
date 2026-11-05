#pragma once
// Everything the installer window draws, in DIPs (Direct2D + DirectWrite).
// The same Draw() renders into the window and into PNG previews
// (sol_installer --render-preview <folder>).
#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>
#include <string>

namespace sol_installer {
inline constexpr float kWidth = 420, kHeight = 360;

enum class Phase { Checking, Downloading, Installing, Opening, UpToDate, Failed };

struct Model {
  Phase phase = Phase::Checking;
  std::wstring version;    // "0.1.1" once known.
  uint64_t received = 0, total = 0;
  std::wstring error;      // Failed only.
};

enum class Hit { None, Close, Primary, Drag };

// Per-frame animation values, advanced by the window (see Animate).
struct Motion {
  double time = 0;      // Seconds since the window appeared.
  float fill = 0;       // Water in the ring, 0..1: the download's progress.
  float morph = 0;      // 1 = the water has become the logo's wave (installing, done).
  float settle = 0;     // 1 = the exact Sol mark, no wave.
  float close_hover = 0, primary_hover = 0, primary_press = 0;
};

// Where the mark is heading for this model: the ring fills with water as Sol
// downloads; while it installs the water sinks into the logo's wave, which
// stops rippling once Sol is ready.
float TargetFill(const Model& model);
float TargetMorph(const Model& model);
// Advances |motion| by |dt| seconds toward the model's targets.
void Animate(Motion& motion, const Model& model, Hit hover, bool pressed, double dt);
Hit HitTest(const Model& model, float x, float y);

class View {
 public:
  View(ID2D1Factory* factory, IDWriteFactory* text);
  void Draw(ID2D1RenderTarget* target, const Model& model, const Motion& motion);

 private:
  Microsoft::WRL::ComPtr<ID2D1Factory> factory_;
  Microsoft::WRL::ComPtr<IDWriteTextFormat> title_, detail_, button_;
};
}  // namespace sol_installer
