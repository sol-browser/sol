#include "windows/window_placement.h"
#include <fstream>
#include <sstream>
#include "include/cef_parser.h"

namespace red {
bool PlausibleWindowBounds(const CefRect& b) {
  return b.width >= 700 && b.height >= 420 && b.width <= 16384 && b.height <= 16384 &&
      b.x > -32768 && b.y > -32768 && b.x < 32768 && b.y < 32768;
}
std::optional<WindowPlacement> LoadWindowPlacement(const std::filesystem::path& file) {
  std::ifstream input(file, std::ios::binary);
  if (!input) return std::nullopt;
  std::stringstream text; text << input.rdbuf();
  if (text.str().size() > 4096) return std::nullopt;
  const auto value = CefParseJSON(text.str(), JSON_PARSER_RFC);
  if (!value || value->GetType() != VTYPE_DICTIONARY) return std::nullopt;
  const auto d = value->GetDictionary();
  for (const auto* key : {"x", "y", "width", "height"}) if (d->GetType(key) != VTYPE_INT) return std::nullopt;
  if (d->GetType("maximized") != VTYPE_BOOL) return std::nullopt;
  WindowPlacement placement{CefRect(d->GetInt("x"), d->GetInt("y"), d->GetInt("width"), d->GetInt("height")), d->GetBool("maximized")};
  if (!PlausibleWindowBounds(placement.bounds)) return std::nullopt;
  return placement;
}
void SaveWindowPlacement(const std::filesystem::path& file, const WindowPlacement& placement) {
  if (!PlausibleWindowBounds(placement.bounds)) return;
  auto d = CefDictionaryValue::Create();
  d->SetInt("x", placement.bounds.x); d->SetInt("y", placement.bounds.y);
  d->SetInt("width", placement.bounds.width); d->SetInt("height", placement.bounds.height);
  d->SetBool("maximized", placement.maximized);
  auto value = CefValue::Create(); value->SetDictionary(d);
  // Write a sibling file, then replace: an interrupted write keeps the old one.
  std::error_code ignored;
  std::filesystem::create_directories(file.parent_path(), ignored);
  const auto temporary = std::filesystem::path(file).concat(L".tmp");
  { std::ofstream output(temporary, std::ios::binary | std::ios::trunc); output << CefWriteJSON(value, JSON_WRITER_DEFAULT).ToString(); }
  std::filesystem::rename(temporary, file, ignored);
}
}  // namespace red
