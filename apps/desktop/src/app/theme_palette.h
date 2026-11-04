#pragma once
#include <array>
#include <charconv>
#include <optional>
#include <string>
#include <string_view>
namespace red {
// Integer, size-independent coordinates. Absence preserves the legacy theme.
struct ThemeCircle {
  int hue = 0, distance = 700, size = 43;
  bool operator==(const ThemeCircle&) const = default;
};
struct ThemePalette {
  int version = 1, count = 3, radius = 380, lightness = 66;
  int harmony = 0, kind = 0, fill = 62;  // soft/contrast/triad; color/free/mono
  std::array<ThemeCircle,3> circles{};
  int rotation = 0;  // v2 composition orientation, independent of hue
  bool operator==(const ThemePalette&) const = default;
};
constexpr bool ValidPalette(const ThemePalette& p) {
  if(p.rotation<0||p.rotation>359||(p.version!=2&&p.rotation!=0))return false;
  if (p.version==2) for(const auto& c:p.circles) if(c.hue<0||c.hue>359||c.distance<0||c.distance>1000||c.size<18||c.size>62)return false;
  return (p.version == 1 || p.version == 2) && p.count >= 1 && p.count <= 3 && p.radius >= 20 && p.radius <= 950 &&
    p.lightness >= 12 && p.lightness <= 88 && p.harmony >= 0 && p.harmony <= 2 &&
    p.kind >= 0 && p.kind <= 2 && p.fill >= 0 && p.fill <= 100;
}
inline std::string EncodePalette(const std::optional<ThemePalette>& palette) {
  if (!palette) return {};
  const auto& p = *palette;
  auto result=std::to_string(p.version)+","+std::to_string(p.count)+","+std::to_string(p.radius)+","+
    std::to_string(p.lightness)+","+std::to_string(p.harmony)+","+std::to_string(p.kind)+","+std::to_string(p.fill);
  if(p.version==2)for(const auto& c:p.circles)result+=","+std::to_string(c.hue)+","+std::to_string(c.distance)+","+std::to_string(c.size);
  if(p.version==2&&p.rotation!=0)result+=","+std::to_string(p.rotation);
  return result;
}
inline bool DecodePalette(std::string_view text, std::optional<ThemePalette>& palette) {
  if (text.empty()) { palette.reset(); return true; }
  if (text.size() > 64) return false;
  ThemePalette p;
  const std::array<int*,17> fields{&p.version,&p.count,&p.radius,&p.lightness,&p.harmony,&p.kind,&p.fill,
    &p.circles[0].hue,&p.circles[0].distance,&p.circles[0].size,&p.circles[1].hue,&p.circles[1].distance,&p.circles[1].size,&p.circles[2].hue,&p.circles[2].distance,&p.circles[2].size,&p.rotation};
  size_t count=7;
  for (size_t i = 0; i < count; ++i) {
    const auto end = text.find(',');
    const auto token = text.substr(0,end);
    const auto result = std::from_chars(token.data(),token.data()+token.size(),*fields[i]);
    if (result.ec != std::errc{} || result.ptr != token.data()+token.size()) return false;
    if(i==0&&p.version==2)count=16;
    if(i==15&&p.version==2&&end!=std::string_view::npos)count=17;
    if ((end == std::string_view::npos) != (i == count-1)) return false;
    if (end != std::string_view::npos) text.remove_prefix(end+1);
  }
  if (!ValidPalette(p)) return false;
  palette = p; return true;
}
}  // namespace red
