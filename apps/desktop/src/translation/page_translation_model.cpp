#include "translation/page_translation_model.h"

#include <windows.h>

#include <algorithm>
#include <array>

namespace red {

bool ValidLanguageCode(std::string_view code) {
  return code == "zh_hant" || ((code.size() == 2 || code.size() == 3) &&
         std::ranges::all_of(code, [](char c) { return c >= 'a' && c <= 'z'; }));
}

std::string CatalogLanguage(std::string_view tag) {
  std::string lower(tag);
  std::ranges::transform(lower, lower.begin(), [](char c) {
    return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c == '_' ? '-' : c;
  });
  const std::string primary = lower.substr(0, lower.find('-'));
  if (primary == "zh")
    for (const char* traditional : {"-hant", "-tw", "-hk", "-mo"})
      if (lower.find(traditional) != std::string::npos) return "zh_hant";
  return ValidLanguageCode(primary) ? primary : "";
}

std::string TranslationTargetFromLocale(std::string_view chromium_locale) {
  const auto code = CatalogLanguage(chromium_locale);
  return code.empty() ? "en" : code;
}

std::wstring LanguageDisplayName(std::string_view code) {
  std::wstring upper(code.begin(), code.end());
  std::ranges::transform(upper, upper.begin(), [](wchar_t c) { return c >= L'a' && c <= L'z' ? static_cast<wchar_t>(c - L'a' + L'A') : c; });
  if (code == "zh_hant") return L"Traditional Chinese";
  if (!ValidLanguageCode(code)) return upper;
  const std::wstring tag(code.begin(), code.end());
  std::array<wchar_t, 128> name{};
  if (GetLocaleInfoEx(tag.c_str(), LOCALE_SENGLISHLANGUAGENAME, name.data(), static_cast<int>(name.size())) > 1) {
    std::wstring result(name.data());
    // A tag Windows only guesses at comes back as "Unknown Language (xx)".
    if (!result.empty() && result.find(L'(') == std::wstring::npos) return result;
  }
  return upper;
}

}  // namespace red
