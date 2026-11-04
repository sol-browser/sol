#include "app/theme.h"
#include <cstdio>
#include <cstdlib>
#include <fstream>
using red::Rgb;
static int failures = 0;
static void Check(bool ok, const char* what) { if (!ok) { std::printf("FAIL: %s\n", what); ++failures; } }
static bool Near(Rgb a, Rgb b) { return std::abs(a.r - b.r) <= 1 && std::abs(a.g - b.g) <= 1 && std::abs(a.b - b.b) <= 1; }
int main(int argc,char** argv) {
  if(argc==2&&std::string_view(argv[1])=="--orbit-css-dump"){
    for(int dark=0;dark<2;++dark)for(int count=1;count<=3;++count)for(int rotation:{0,90,359})for(int fill:{0,32,65,100}){
      red::ThemePalette p{2,count,380,66,0,0,fill};p.circles={red::ThemeCircle{32,850,43},red::ThemeCircle{154,650,38},red::ThemeCircle{274,950,37}};
      p.rotation=rotation;
      std::printf("%d,%d,%d,%d,%s\n",dark,count,rotation,fill,red::PaletteCss(red::Theme{32,70,red::ThemeMode::Dark,31,p},dark!=0).c_str());
    }return 0;
  }
  if(argc==2&&std::string_view(argv[1])=="--orbit-turn-dump"){
    for(int dark=0;dark<2;++dark)for(int rotation:{0,45,90,180,270,359}){
      red::ThemePalette p{2,3,380,66,0,0,65};p.circles={red::ThemeCircle{32,850,43},red::ThemeCircle{154,650,38},red::ThemeCircle{274,950,37}};p.rotation=rotation;
      for(const auto point:std::array<std::array<double,2>,5>{{{0,0},{.5,0},{.5,.5},{0,1},{1,1}}}){
        const auto c=red::PaletteBackgroundAt(red::Theme{32,70,red::ThemeMode::Dark,31,p},dark!=0,point[0],point[1]);
        std::printf("%d,%d,%.1f,%.1f,%u,%u,%u\n",dark,rotation,point[0],point[1],unsigned(c.r),unsigned(c.g),unsigned(c.b));
      }
    }return 0;
  }
  if(argc==2&&std::string_view(argv[1])=="--orbit-field-dump"){
    for(int count=1;count<=3;++count)for(int size:{18,43,62})for(int fill:{0,42,100}){
      red::ThemePalette p{2,count,380,66,0,0,fill};p.circles={red::ThemeCircle{32,850,size},red::ThemeCircle{154,650,38},red::ThemeCircle{274,950,37}};
      for(const auto point:std::array<std::array<double,2>,5>{{{0,0},{.5,0},{.5,.5},{0,1},{1,1}}}){
        const auto c=red::PaletteBackgroundAt(red::Theme{32,70,red::ThemeMode::Dark,31,p},true,point[0],point[1]);
        std::printf("%d,%d,%d,%.1f,%.1f,%u,%u,%u\n",count,size,fill,point[0],point[1],unsigned(c.r),unsigned(c.g),unsigned(c.b));
      }
    }return 0;
  }
  if(argc==2&&std::string_view(argv[1])=="--orbit-dump") {
    for(int dark=0;dark<2;++dark)for(int count=1;count<=3;++count)for(int size:{18,43,62})for(int fill:{0,65,100}){
      red::ThemePalette p{2,count,380,66,0,0,fill};p.circles={red::ThemeCircle{32,850,size},red::ThemeCircle{154,650,38},red::ThemeCircle{274,950,37}};
      const auto stops=red::PaletteBackgroundStops(red::Theme{32,70,red::ThemeMode::Dark,31,p},dark!=0);
      std::printf("%d,%d,%d,%d",dark,count,size,fill);for(const auto c:stops)std::printf(",%u,%u,%u",unsigned(c.r),unsigned(c.g),unsigned(c.b));std::printf("\n");
    }return 0;
  }
  if(argc==2&&std::string_view(argv[1])=="--palette-dump") {
    for(int dark=0;dark<2;++dark)for(int count=1;count<=3;++count)for(int harmony=0;harmony<3;++harmony)for(int kind=0;kind<3;++kind)for(int radius:{20,500,950}) {
      red::Theme t{41,42,red::ThemeMode::Light,24,red::ThemePalette{1,count,radius,68,harmony,kind,73}};
      const auto stops=red::PaletteBackgroundStops(t,dark!=0);
      std::printf("%d,%d,%d,%d,%d",dark,count,harmony,kind,radius);
      for(const auto c:stops)std::printf(",%u,%u,%u",unsigned(c.r),unsigned(c.g),unsigned(c.b));
      std::printf("\n");
    }
    return 0;
  }
  const Rgb background{0xe7, 0xde, 0xed};
  Check(Near(red::Themed(background, {}, false), background), "default theme keeps authored colors");
  Check(Near(red::Themed({0x80, 0x80, 0x80}, {120, 160}, false), {0x80, 0x80, 0x80}), "neutral gray is never tinted");
  Check(Near(red::Themed({0xd9, 0x43, 0x5f}, {120, 100}, false), {0xd9, 0x43, 0x5f}), "semantic red stays red");
  const auto gray = red::Themed(background, {200, 0}, false);
  Check(std::abs(gray.r - gray.g) <= 1 && std::abs(gray.g - gray.b) <= 1, "zero intensity is gray");
  const auto green = red::Themed({0x80, 0x5d, 0x9b}, {145, 100}, false);
  Check(green.g > green.r && green.g > green.b, "hue 145 turns the accent green");
  const auto vivid = red::Themed({0x80, 0x5d, 0x9b}, {30, red::kThemeMaxIntensity}, false);
  Check(vivid.r >= vivid.g && vivid.r >= vivid.b, "out-of-gamut chroma is reduced, hue kept");
  Check(red::ValidTheme(0, 0, 0, 0) && red::ValidTheme(359, 160, 2, 100) && !red::ValidTheme(360, 100, 0, 0) &&
        !red::ValidTheme(10, 161, 0, 0) && !red::ValidTheme(10, 100, 3, 0) && !red::ValidTheme(10, 100, 0, 101), "validation bounds");
  const auto night = red::Themed(background, {}, true), text = red::Themed({0x40, 0x38, 0x4a}, {}, true);
  Check(night.r < 60 && night.g < 60 && night.b < 70, "dark mode darkens the background");
  Check(text.r > 170 && text.g > 160, "dark mode lightens text");
  Check(night.b > night.g, "dark background keeps the theme tint");
  Check(red::ResolveDark(red::ThemeMode::System, true) && !red::ResolveDark(red::ThemeMode::System, false) &&
        red::ResolveDark(red::ThemeMode::Dark, false) && !red::ResolveDark(red::ThemeMode::Light, true), "mode resolution");
  std::optional<red::ThemePalette> palette=red::ThemePalette{};
  const auto encoded=red::EncodePalette(palette);
  std::optional<red::ThemePalette> decoded;
  Check(red::DecodePalette(encoded,decoded)&&decoded==palette,"palette codec round trip");
  for(const auto* bad:{"2,3,380,66,0,0,62","1,4,380,66,0,0,62","1,3,NaN,66,0,0,62","1,3,380,66,0,0,101","1,3,380,66,0,0,62,0","1,3,380,66,0,0,62x"})
    Check(!red::DecodePalette(bad,decoded),"malformed/future palette rejected");
  Check(red::DecodePalette("",decoded)&&!decoded,"legacy palette remains absent");
  red::ThemePalette orbit{2,3,950,88,2,2,100};orbit.circles={red::ThemeCircle{359,1000,62},red::ThemeCircle{359,1000,62},red::ThemeCircle{359,1000,62}};
  const auto orbit_encoded=red::EncodePalette(orbit);
  Check(orbit_encoded.size()<=64&&red::DecodePalette(orbit_encoded,decoded)&&decoded==orbit,"independent circles fit existing storage and round trip exactly");
  Check(decoded&&decoded->rotation==0,"old v2 palettes default to zero rotation");
  orbit.rotation=359;const auto turned_encoded=red::EncodePalette(orbit);
  Check(turned_encoded.size()<=64&&red::DecodePalette(turned_encoded,decoded)&&decoded==orbit,"rotated palette fits existing storage and round trips");
  for(const auto* suffix:{",-1",",360",",1.5",",90,1",","})Check(!red::DecodePalette(orbit_encoded+suffix,decoded),"invalid rotation encoding rejected");
  for(const auto* bad:{"2,3,380,66,0,0,62,360,700,43,154,700,43,274,700,43","2,3,380,66,0,0,62,32,1001,43,154,700,43,274,700,43","2,3,380,66,0,0,62,32,700,17,154,700,43,274,700,43","2,3,380,66,0,0,62,32,700,43"})Check(!red::DecodePalette(bad,decoded),"invalid independent circles rejected");
  red::Theme multi{40,42,red::ThemeMode::Dark,24,palette};
  const auto stops=red::PaletteBackgroundStops(multi,true);
  Check(!Near(stops[0],stops[1])&&!Near(stops[1],stops[2]),"three colors produce distinct background stops");
  multi.palette->count=1;const auto flat=red::PaletteBackgroundStops(multi,true);
  Check(Near(flat[0],flat[1])&&Near(flat[1],flat[2]),"one color produces a flat background");
  multi.palette->fill=0;const auto empty=red::PaletteBackgroundStops(multi,true);
  Check(Near(empty[0],{35,32,41})&&Near(empty[1],empty[2]),"zero fill preserves the mode base");
  multi.palette->fill=100;multi.palette->kind=2;const auto mono=red::PaletteBackgroundStops(multi,true);
  Check(mono[0].r==mono[0].g&&mono[0].g==mono[0].b,"monochrome palette stays neutral at full fill");
  const auto file=std::filesystem::temp_directory_path()/"red-theme-unit-cache.txt";
  red::WriteThemeCache(file,multi);Check(red::ReadThemeCache(file)==multi,"startup cache round trips the palette");
  multi.palette=orbit;red::WriteThemeCache(file,multi);Check(red::ReadThemeCache(file)==multi,"startup cache round trips independent circles");
  {std::ofstream old(file);old<<"145 120 2 35\n";}
  const auto legacy=red::ReadThemeCache(file);Check(legacy&&!legacy->palette&&legacy->hue==145,"old startup cache stays legacy");
  std::error_code ignored;std::filesystem::remove(file,ignored);
  std::printf(failures ? "theme_test: %d failures\n" : "theme_test: ok\n", failures);
  return failures ? 1 : 0;
}
