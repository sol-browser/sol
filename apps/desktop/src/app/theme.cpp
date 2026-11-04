#include "app/theme.h"
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>
namespace red {
namespace {
constexpr double kPi = 3.14159265358979323846;
double ToLinear(double c) { c /= 255; return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4); }
double FromLinear(double c) { return c <= 0.0031308 ? c * 12.92 : 1.055 * std::pow(c, 1 / 2.4) - 0.055; }
struct Lab { double L, a, b; };
Lab ToOklab(Rgb c) {
  const double r = ToLinear(c.r), g = ToLinear(c.g), b = ToLinear(c.b);
  const double l = std::cbrt(0.4122214708 * r + 0.5363325363 * g + 0.0514459929 * b);
  const double m = std::cbrt(0.2119034982 * r + 0.6806995451 * g + 0.1073969566 * b);
  const double s = std::cbrt(0.0883024619 * r + 0.2817188376 * g + 0.6299787005 * b);
  return {0.2104542553 * l + 0.7936177850 * m - 0.0040720468 * s,
          1.9779984951 * l - 2.4285922050 * m + 0.4505937099 * s,
          0.0259040371 * l + 0.7827717662 * m - 0.8086757660 * s};
}
// Linear sRGB; components may fall outside [0, 1] when out of gamut.
void ToLinearRgb(Lab c, double out[3]) {
  const double l = std::pow(c.L + 0.3963377774 * c.a + 0.2158037573 * c.b, 3);
  const double m = std::pow(c.L - 0.1055613458 * c.a - 0.0638541728 * c.b, 3);
  const double s = std::pow(c.L - 0.0894841775 * c.a - 1.2914855480 * c.b, 3);
  out[0] = 4.0767416621 * l - 3.3077115913 * m + 0.2309699292 * s;
  out[1] = -1.2684380046 * l + 2.6097574011 * m - 0.3413193965 * s;
  out[2] = -0.0041960863 * l - 0.7034186147 * m + 1.7076147010 * s;
}
}  // namespace
std::optional<Theme> ReadThemeCache(const std::filesystem::path& file) {
  std::ifstream input(file);
  int hue = -1, intensity = -1, mode = -1, noise = -1;
  if (!(input >> hue >> intensity >> mode >> noise) || !ValidTheme(hue, intensity, mode, noise)) return std::nullopt;
  Theme theme{hue, intensity, static_cast<ThemeMode>(mode), noise};
  std::string rest, encoded; std::getline(input, rest);
  if (std::getline(input, encoded) && !DecodePalette(encoded, theme.palette)) return std::nullopt;
  return theme;
}
void WriteThemeCache(const std::filesystem::path& file, const Theme& theme) {
  std::ofstream output(file, std::ios::trunc);
  output << theme.hue << ' ' << theme.intensity << ' ' << static_cast<int>(theme.mode) << ' ' << theme.noise << '\n';
  output << EncodePalette(theme.palette) << '\n';
}
std::array<Rgb,3> PaletteBackgroundStops(const Theme& theme, bool dark) {
  const auto p = theme.palette.value_or(ThemePalette{});
  const double radius = p.radius / 1000.0;
  const double saturation = p.kind == 2 ? 0 : p.kind == 1 ? .9 + .1*radius : 1-radius;
  const double point_lightness = p.kind == 0 ? p.lightness : 100*radius;
  const double lightness = (dark ? 12+point_lightness*.25 : 72+point_lightness*.25)/100;
  const Rgb base = dark ? Rgb{35,32,41} : Rgb{241,238,243};
  auto make = [&](int offset) {
    const auto circle=p.circles[static_cast<size_t>(p.version==2?offset:0)];
    const double hue = (p.version==2?circle.hue:(theme.hue+offset+360)%360)/60.0;
    const double sat=p.version==2?(p.kind==2?0:std::pow((circle.size-18)/44.0,.65)):saturation;
    const double lit=p.version==2?.55:lightness;
    const double c = (1-std::abs(2*lit-1))*sat;
    const double x = c*(1-std::abs(std::fmod(hue,2)-1)), m = lit-c/2;
    const std::array<std::array<double,3>,6> sectors{{{c,x,0},{x,c,0},{0,c,x},{0,x,c},{x,0,c},{c,0,x}}};
    const auto rgb = sectors[static_cast<size_t>(hue)];
    std::array<double,3> channels{255*(rgb[0]+m),255*(rgb[1]+m),255*(rgb[2]+m)};
    auto mix = [&](double v, int b) { return static_cast<uint8_t>(std::lround(b+(v-b)*(p.version==2?1:p.fill/100.0))); };
    return Rgb{mix(channels[0],base.r),mix(channels[1],base.g),mix(channels[2],base.b)};
  };
  const int second = p.harmony == 0 ? (p.count == 2 ? 45 : 315) : p.harmony == 1 ? (p.count == 2 ? 180 : 150) : 120;
  const int third = p.harmony == 0 ? 45 : p.harmony == 1 ? 210 : 240;
  const auto a = make(0), b = make(p.version==2?1:second), c = make(p.version==2?2:third);
  if (p.count == 1) return {a,a,a};
  if (p.count == 3) return {a,b,c};
  auto middle=[](uint8_t u,uint8_t v){return static_cast<uint8_t>(std::lround(u+(v-u)*.55));};
  return {a,Rgb{middle(a.r,b.r),middle(a.g,b.g),middle(a.b,b.b)},b};
}
namespace {
struct PaletteAppearance { Rgb base; double color_alpha; double exposure; };
PaletteAppearance Appearance(const Theme& theme,bool dark) {
  const double exposure=dark?(std::min)(theme.palette->fill/65.0,1.0):1;
  const auto gray=static_cast<uint8_t>(std::lround(std::lround(255*(dark?.035+.105*theme.palette->fill/100.0:.84+.14*theme.palette->fill/100.0))*exposure));
  return {{gray,gray,gray},dark?.31:.25,exposure};
}
Rgb ExposedRgb(Rgb color,double exposure) {
  auto channel=[&](uint8_t v){return static_cast<uint8_t>(std::lround(v*exposure));};
  return {channel(color.r),channel(color.g),channel(color.b)};
}
struct PaletteSpot { double x,y,weight; Rgb color; };
std::array<PaletteSpot,3> PaletteSpots(const Theme& theme,bool dark) {
  const auto& p=*theme.palette;const auto colors=PaletteBackgroundStops(theme,dark);
  std::array<PaletteSpot,3> spots{};
  for(int i=0;i<p.count;++i){const auto c=p.circles[i];const double angle=(c.hue-170+p.rotation)*kPi/180,r=c.distance/1000.0;
    spots[i]={std::cos(angle)*r,std::sin(angle)*r,double(c.size*c.size),colors[i]};}
  // With two colors, PaletteBackgroundStops inserts an interpolated middle stop.
  if(p.count==2)spots[1].color=colors[2];
  return spots;
}
Rgb SampleField(const std::array<PaletteSpot,3>& spots,int count,double x,double y) {
  double weight=0,r=0,g=0,b=0;
  for(int i=0;i<count;++i){const auto s=spots[i];const double w=s.weight*std::exp(3*(s.x*(2*x-1)+s.y*(2*y-1)));
    weight+=w;r+=s.color.r*w;g+=s.color.g*w;b+=s.color.b*w;}
  return {static_cast<uint8_t>(std::lround(r/weight)),static_cast<uint8_t>(std::lround(g/weight)),static_cast<uint8_t>(std::lround(b/weight))};
}
// Same small top-down BGR texture as palette-model.ts. No disk edges, viewport
// dependent CSS angles or external assets; native paints sample the same field.
std::string PaletteFieldImage(const Theme& theme,bool dark) {
  constexpr int size=32,offset=54;
  std::array<uint8_t,offset+size*size*3> bytes{};
  auto u16=[&](size_t at,uint16_t value){for(size_t i=0;i<2;++i)bytes[at+i]=static_cast<uint8_t>(value>>(i*8));};
  auto u32=[&](size_t at,uint32_t value){for(size_t i=0;i<4;++i)bytes[at+i]=static_cast<uint8_t>(value>>(i*8));};
  bytes[0]='B';bytes[1]='M';u32(2,static_cast<uint32_t>(bytes.size()));u32(10,offset);u32(14,40);
  u32(18,size);u32(22,static_cast<uint32_t>(-size));u16(26,1);u16(28,24);u32(34,size*size*3);
  const auto spots=PaletteSpots(theme,dark);
  const double exposure=Appearance(theme,dark).exposure;
  for(int y=0;y<size;++y)for(int x=0;x<size;++x){const auto c=ExposedRgb(SampleField(spots,theme.palette->count,(x+.5)/size,(y+.5)/size),exposure);
    const size_t at=offset+(y*size+x)*3;bytes[at]=c.b;bytes[at+1]=c.g;bytes[at+2]=c.r;}
  constexpr char alphabet[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string encoded;encoded.reserve((bytes.size()+2)/3*4);
  for(size_t i=0;i<bytes.size();i+=3){const uint32_t value=(uint32_t(bytes[i])<<16)|(i+1<bytes.size()?uint32_t(bytes[i+1])<<8:0)|(i+2<bytes.size()?bytes[i+2]:0);
    encoded+=alphabet[(value>>18)&63];encoded+=alphabet[(value>>12)&63];
    encoded+=i+1<bytes.size()?alphabet[(value>>6)&63]:'=';encoded+=i+2<bytes.size()?alphabet[value&63]:'=';}
  return "-webkit-cross-fade(linear-gradient(transparent,transparent),url(\"data:image/bmp;base64,"+encoded+"\"),"+std::to_string(Appearance(theme,dark).color_alpha)+") 0 0 / 100% 100% no-repeat";
}
}
Rgb PaletteBackgroundAt(const Theme& theme,bool dark,double x,double y) {
  if(!theme.palette||theme.palette->version!=2)return PaletteBackgroundStops(theme,dark)[1];
  const auto appearance=Appearance(theme,dark);const auto source=ExposedRgb(SampleField(PaletteSpots(theme,dark),theme.palette->count,x,y),appearance.exposure);
  auto mix=[&](uint8_t v,uint8_t base){return static_cast<uint8_t>(std::lround(v*appearance.color_alpha+base*(1-appearance.color_alpha)));};
  return {mix(source.r,appearance.base.r),mix(source.g,appearance.base.g),mix(source.b,appearance.base.b)};
}
std::string PaletteCss(const Theme& theme, bool dark) {
  if (!theme.palette) return "--theme-palette-gradient:initial;--theme-palette-base:initial;--theme-palette-material-alpha:initial;";
  const auto stops = PaletteBackgroundStops(theme,dark);
  std::ostringstream css;
  if(theme.palette->version==2){
    const auto center=PaletteBackgroundAt(theme,dark,.5,.5);
    const auto appearance=Appearance(theme,dark);const auto base=appearance.base;
    const auto wrapper="rgb("+std::to_string(base.r)+" "+std::to_string(base.g)+" "+std::to_string(base.b)+" / var(--theme-material-alpha,1))";
    css<<"--theme-palette-fill:"<<theme.palette->fill/100.0<<";--theme-palette-material-alpha:var(--backdrop-alpha,1);--theme-palette-gradient:";
    css<<PaletteFieldImage(theme,dark)<<",linear-gradient("<<wrapper<<','<<wrapper<<");--theme-palette-base:rgb("<<int(center.r)<<' '<<int(center.g)<<' '<<int(center.b)<<");";return css.str();
  }
  css << "--theme-palette-material-alpha:initial;--theme-palette-fill:" << theme.palette->fill/100.0 << ";--theme-palette-gradient:linear-gradient(155deg in srgb,";
  for (size_t i=0;i<stops.size();++i) { const auto c=stops[i];
    if (i) css << ',';
    css << "rgb(" << int(c.r) << ' ' << int(c.g) << ' ' << int(c.b) << " / var(--theme-material-alpha,1)) " << (i==0?0:i==1?55:100) << '%';
  }
  css << ");--theme-palette-base:rgb(" << int(stops[1].r) << ' ' << int(stops[1].g) << ' ' << int(stops[1].b) << ");";
  return css.str();
}
bool SystemPrefersDark() {
  DWORD light = 1, size = sizeof(light);
  if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                   L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &light, &size) != ERROR_SUCCESS) return false;
  return light == 0;
}
Rgb Themed(Rgb authored, const Theme& theme, bool dark, bool surface) {
  const auto lab = ToOklab(authored);
  const double chroma = std::hypot(lab.a, lab.b);
  // Same band as the CSS conversion: neutrals and non-palette hues keep hue.
  const double hue = std::fmod(std::atan2(lab.b, lab.a) * 180 / kPi + 360, 360);
  const bool palette = chroma >= 0.006 && hue >= 230;
  if (!palette && !dark) return authored;
  // Dark lightness: themify2.py dark() for opaque colors.
  const double base = !dark ? lab.L : lab.L >= 0.85 ? std::clamp(0.205 + (lab.L - 0.91) * 0.9, 0.14, 0.30)
                                                    : 0.98 - 0.75 * std::pow(lab.L, 1.4);
  const double vivid = surface && palette ? (std::max)(0, theme.intensity - 100) / double(kThemeMaxIntensity - 100) : 0;
  const double L = base + vivid * ((dark ? 0.46 : 0.74) - base);
  const double rotated = (palette ? hue - kThemeBaseHue + theme.hue : hue) * kPi / 180;
  double target = palette ? chroma * ThemeChromaScale(theme.intensity) : chroma, rgb[3];
  for (int step = 0; step < 24; ++step, target *= 0.9) {
    ToLinearRgb({L, target * std::cos(rotated), target * std::sin(rotated)}, rgb);
    if (std::all_of(rgb, rgb + 3, [](double v) { return v >= -1e-4 && v <= 1 + 1e-4; })) break;
  }
  auto channel = [](double v) { return static_cast<uint8_t>(std::lround(std::clamp(FromLinear(std::clamp(v, 0.0, 1.0)), 0.0, 1.0) * 255)); };
  return {channel(rgb[0]), channel(rgb[1]), channel(rgb[2])};
}
}  // namespace red
