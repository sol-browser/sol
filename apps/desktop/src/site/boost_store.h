#pragma once
#include <map>
#include <string>
#include <vector>

namespace red {
// Boosts (docs/boosts.md): per-site user CSS. Pure data and script building, no
// CEF; persistence and injection are in windows/boosts.cpp.
// The visual editor's settings (all zero = untouched). BoostCss turns them into CSS.
struct BoostLook {
  int hue = 0;         // 0..359, page tint colour (the pad's big dot)
  int tint = 0;        // 0..100, tint strength
  int hue2 = 0;        // 0..359, accent colour for links, selection and form controls (the small dot)
  int tint2 = 0;       // 0..100, accent strength; 0 leaves the page's own accents
  int brightness = 0;  // -50..50
  int contrast = 0;    // -50..50
  int saturation = 0;  // -50..50
  int dark = 0;        // 1: automatic dark mode (inverted page, media kept as is)
  int font = 0;        // 0 = the page's own, else an index of kFonts
  int size = 0;        // -30..50, percent change of the page size
  int caps = 0;        // 0 as is, 1 UPPER, 2 lower, 3 Capitalized
  bool Neutral() const { return !tint && !tint2 && !brightness && !contrast && !saturation && !dark && !font && !size && !caps; }
  bool Valid() const;
};
struct Boost {
  bool enabled = true;
  std::string css;
  BoostLook look;
  std::vector<std::string> zaps;  // Hidden elements (Zap), as CSS selectors.
  std::string focus;              // The one part of the page left visible (Focus); empty for none.
  bool Empty() const { return css.empty() && look.Neutral() && zaps.empty() && focus.empty(); }
};
constexpr int kBoostFontCount = 15;  // Keep in step with FONTS in apps/browser-ui/src/boost-look.ts.
constexpr size_t kMaxBoostCss = 64 * 1024;
constexpr size_t kMaxBoosts = 200;
constexpr size_t kMaxBoostZaps = 100;
constexpr size_t kMaxBoostSelector = 400;
// A selector made by the element picker: bounded and unable to leave its rule (no braces, semicolons,
// quotes, comments, at-rules or line breaks).
bool ValidBoostSelector(const std::string& selector);
// CSS of the visual settings, Zap and Focus, followed by the user's own CSS.
std::string BoostCss(const Boost& boost);

// "https://www.Example.com:8080/" -> "example.com"; empty for anything that is
// not a plain http(s) DNS name or localhost (IP literals and odd hosts get no Boost).
std::string BoostKeyFromOrigin(const std::string& origin);
// Quoted JavaScript string literal for |text| (safe inside a script source).
std::string JsString(const std::string& text);
// Isolated-world element picker for Zap ("zap") and Focus ("focus"); an empty mode stops a running
// picker. Picks are reported through the world's redBoostPick binding as JSON
// {"mode","selector"} or {"cancel":true} (Escape).
std::string BoostPicker(const std::string& mode);

class BoostStore {
 public:
  const Boost* Find(const std::string& key) const;
  // 0, or an HTTP-like code with |error| set. A Boost with nothing in it is removed.
  int Set(const std::string& key, Boost boost, std::string& error);
  const std::map<std::string, Boost>& All() const { return boosts_; }
  void Replace(std::map<std::string, Boost> boosts) { boosts_ = std::move(boosts); }
  // Isolated-world script: applies the CSS of every enabled Boost whose key is the
  // frame's host or a parent domain of it, and removes what it applied before.
  std::string Loader() const;
 private:
  std::map<std::string, Boost> boosts_;
};
}  // namespace red
