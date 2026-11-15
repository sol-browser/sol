#pragma once

#include <string>
#include <string_view>

namespace red {

// A language as the model catalog names it: two or three ASCII lowercase letters, or "zh_hant".
bool ValidLanguageCode(std::string_view code);
// The catalog's code for a BCP 47 tag: "zh_hant" for zh-TW / zh-HK / zh-MO / zh-Hant, otherwise the
// primary subtag in lower case ("pt-BR" -> "pt"); empty when it is not a valid code.
std::string CatalogLanguage(std::string_view tag);

// The language pages are translated into: the primary subtag of the Chromium UI locale
// ("uk" for "uk", "pt" for "pt-BR", "zh_hant" for "zh-TW"); English when the locale is empty or malformed.
std::string TranslationTargetFromLocale(std::string_view chromium_locale);

// "German" for "de" (Windows' English language name, as Sol's UI is English); the code in
// upper case when Windows does not know it.
std::wstring LanguageDisplayName(std::string_view code);

}  // namespace red
