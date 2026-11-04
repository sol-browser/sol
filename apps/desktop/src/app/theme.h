#pragma once
#include <cstdint>
#include <filesystem>
#include <optional>
#include <array>
#include <string>
#include "app/theme_palette.h"
namespace red {
// The shell's color theme (Arc-style "theme color"). The UI's palette is
// authored around kThemeBaseHue; the theme rotates every themed color to
// |hue| and scales its OKLCH chroma by |intensity| percent, keeping lightness.
// The web UI does the same in CSS (style.css: --theme-hue, --theme-chroma).
constexpr int kThemeBaseHue = 307;
constexpr int kThemeMaxIntensity = 160;
enum class ThemeMode { Light = 0, Dark = 1, System = 2 };
struct Theme {
  int hue = kThemeBaseHue;  // 0..359
  int intensity = 100;      // 0..kThemeMaxIntensity, percent of the authored chroma
  ThemeMode mode = ThemeMode::Light;
  int noise = 0;            // 0..100, grain over the workspace background
  std::optional<ThemePalette> palette;
  bool operator==(const Theme&) const = default;
};
// The chroma multiplier of an intensity: linear up to the authored palette (100), then
// steeper to 10x at kThemeMaxIntensity (vivid colours at the wheel's rim). Keep in sync
// with chromaScale (apps/browser-ui/src/theme-model.ts).
constexpr double ThemeChromaScale(int intensity) {
  return intensity <= 100 ? intensity / 100.0 : 1 + (intensity - 100) * 9.0 / (kThemeMaxIntensity - 100);
}
constexpr bool ValidTheme(int hue, int intensity, int mode, int noise) {
  return hue >= 0 && hue < 360 && intensity >= 0 && intensity <= kThemeMaxIntensity && mode >= 0 && mode <= 2 &&
         noise >= 0 && noise <= 100;
}
// Whether Windows apps are set to dark ("Choose your app mode").
bool SystemPrefersDark();
constexpr bool ResolveDark(ThemeMode mode, bool system_dark) {
  return mode == ThemeMode::Dark || (mode == ThemeMode::System && system_dark);
}
struct Rgb { uint8_t r = 0, g = 0, b = 0; };
// Legacy uses three stops; v2 uses a continuous normalized color field shared
// by CSS textures, DComp and GDI. Coordinates are relative to the whole chrome.
std::array<Rgb, 3> PaletteBackgroundStops(const Theme& theme, bool dark);
Rgb PaletteBackgroundAt(const Theme& theme, bool dark, double x, double y);
std::string PaletteCss(const Theme& theme, bool dark);
// |authored| (an opaque light-mode palette color) under |theme|: hue and
// chroma for palette hues, and in |dark| mode the same lightness mapping as
// the CSS (surfaces stay ordered, text and accents invert). Out-of-gamut
// results reduce chroma.
// |surface|: a workspace background colour; vivid themes (intensity above 100)
// move its lightness to a mid tone, as style.css --theme-vivid does.
Rgb Themed(Rgb authored, const Theme& theme, bool dark, bool surface = false);
// Popovers and menus are neutral, whatever the theme (the Orbit theme editor's
// surface, theme-editor.css / popup.css); the window colour behind the UI.
inline constexpr Rgb kPopupSurface{36, 37, 43};
// The last applied theme, so the window and shell start in it before the
// workspace database has loaded (no light flash at startup).
std::optional<Theme> ReadThemeCache(const std::filesystem::path& file);
void WriteThemeCache(const std::filesystem::path& file, const Theme& theme);
}  // namespace red
