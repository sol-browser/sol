#include "site/boost_store.h"
#include <cstdio>

int main() {
  int failures = 0;
  auto check = [&](bool condition, const char* description) {
    if (!condition) { std::fprintf(stderr, "%s\n", description); ++failures; }
  };
  using red::BoostKeyFromOrigin;
  check(BoostKeyFromOrigin("https://www.Example.com/") == "example.com", "www and case are dropped");
  check(BoostKeyFromOrigin("http://mail.example.com:8080/") == "mail.example.com", "port is dropped");
  check(BoostKeyFromOrigin("http://localhost:3000/") == "localhost", "localhost is allowed");
  check(BoostKeyFromOrigin("http://127.0.0.1:3000/").empty(), "IP literals get no Boost");
  check(BoostKeyFromOrigin("chrome://settings/").empty(), "internal pages get no Boost");
  check(BoostKeyFromOrigin("https://exa mple.com/").empty(), "odd hosts get no Boost");
  check(BoostKeyFromOrigin("https://[::1]/").empty(), "IPv6 literals get no Boost");

  check(red::JsString("a\"b\\c\n") == "\"a\\\"b\\\\c\\n\"", "quotes, backslashes and newlines are escaped");
  check(red::JsString("</style>") == "\"\\u003c/style\\u003e\"", "angle brackets never reach the script as markup");
  check(red::JsString("a\xE2\x80\xA8z") == "\"a\\u2028z\"", "line separator is escaped");

  red::BoostStore store; std::string error;
  check(store.Loader().find("const R=[]") != std::string::npos, "no Boosts, empty rule list");
  check(store.Set("example.com", {true, "h1{color:red}"}, error) == 0, "set");
  check(store.Find("example.com") && store.Find("example.com")->css == "h1{color:red}", "found");
  check(store.Loader().find("[\"example.com\",\"h1{color:red}\"]") != std::string::npos, "enabled Boost is in the loader");
  check(store.Set("example.com", {false, "h1{color:red}"}, error) == 0, "disable keeps it");
  check(store.Find("example.com") && store.Loader().find("example.com") == std::string::npos, "disabled Boost is not applied");
  check(store.Set("example.com", {true, ""}, error) == 0 && !store.Find("example.com"), "empty CSS removes the Boost");
  check(store.Set("127.0.0.1", {true, "a{}"}, error) == 400, "invalid key rejected");
  check(store.Set("big.com", {true, std::string(red::kMaxBoostCss + 1, 'a')}, error) == 413, "size limit");
  red::Boost look; look.look.tint = 40; look.look.hue = 200; look.look.brightness = -10; look.look.font = 7; look.look.size = 10; look.look.caps = 1;
  const auto css = red::BoostCss(look);
  check(css.find("brightness(90%)") != std::string::npos && css.find("background:hsl(200 75% 50%);mix-blend-mode:color;opacity:22%") != std::string::npos, "brightness filter and tint layer");
  { red::Boost accent; accent.look.hue2 = 320; accent.look.tint2 = 60; const auto a = red::BoostCss(accent);
    check(a.find("a,a *{color:hsl(320 75% 62%)") != std::string::npos && a.find("accent-color") != std::string::npos && a.find("html::after") == std::string::npos, "accent shade alone"); }
  check(css.find("Georgia") != std::string::npos && css.find("zoom:110%") != std::string::npos && css.find("uppercase") != std::string::npos, "font, size and case");
  check(red::BoostCss(red::Boost{}).empty(), "neutral settings produce no CSS");
  check(store.Set("look.com", look, error) == 0 && store.Loader().find("look.com") != std::string::npos, "visual-only Boost is stored and applied");
  look.look.font = red::kBoostFontCount;
  check(store.Set("bad.com", look, error) == 400 && !store.Find("bad.com"), "out-of-range settings rejected");
  red::Boost cleared; check(store.Set("look.com", cleared, error) == 0 && !store.Find("look.com"), "neutral and no CSS removes the Boost");
  check(red::ValidBoostSelector("#main > div.card:nth-of-type(2)"), "picker selector accepted");
  for (const char* bad : {"a{}", "a;b", "a\nb", "a /* x */", "", "[x=\"y\"]", "@import", "a}body{"})
    check(!red::ValidBoostSelector(bad), "selector that could leave its rule is rejected");
  red::Boost zapped; zapped.zaps = {"#ad", ".banner"}; zapped.focus = "#article";
  const auto zcss = red::BoostCss(zapped);
  check(zcss.find("#ad{display:none !important}") != std::string::npos && zcss.find(".banner{display:none") != std::string::npos, "zaps hide elements");
  check(zcss.find(":not(#article):not(#article *):not(:has(#article))") != std::string::npos, "focus hides everything else");
  check(store.Set("zap.com", zapped, error) == 0 && store.Find("zap.com"), "zap-only Boost is kept");
  zapped.zaps = {"a{"}; check(store.Set("zap.com", zapped, error) == 400, "bad zap rejected");
  red::Boost dark; dark.look.dark = 1;
  check(red::BoostCss(dark).find("invert(1) hue-rotate(180deg)") != std::string::npos && red::BoostCss(dark).find("img,video") != std::string::npos, "dark mode inverts and keeps media");
  check(red::BoostPicker("zap").find("redBoostPick") != std::string::npos && red::BoostPicker("").find("if(!M)return") != std::string::npos, "picker script");
  return failures ? 1 : 0;
}
