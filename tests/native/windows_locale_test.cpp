#include "app/windows_locale.h"

#include <cstdio>
#include <initializer_list>

int main() {
  int failures = 0;
  auto check = [&](std::initializer_list<std::wstring_view> preferred_languages,
                   std::wstring_view regional_format, std::string_view chromium,
                   std::string_view accept_languages) {
    const auto locale = red::ResolveBrowserLocale(preferred_languages, regional_format);
    if (locale.chromium != chromium || locale.accept_languages != accept_languages) {
      std::fprintf(stderr, "Expected %.*s / %.*s, got %s / %s\n",
                   static_cast<int>(chromium.size()), chromium.data(),
                   static_cast<int>(accept_languages.size()), accept_languages.data(),
                   locale.chromium.c_str(), locale.accept_languages.c_str());
      ++failures;
    }
  };

  check({L"uk-UA", L"en-US"}, L"uk-UA", "uk-UA", "uk-UA,en-US");
  check({L"en", L"en-CA", L"fr-CA"}, L"en-CA", "en", "en-CA,en,fr-CA");
  check({L"en-CA", L"fr-CA"}, L"en-US", "en-CA", "en-CA,fr-CA");
  check({L"uk-UA", L"en-US"}, L"en-GB", "uk-UA", "uk-UA,en-US");
  check({L"zh-TW", L"en-US"}, L"zh-CN", "zh-TW", "zh-TW,en-US");
  check({L"de", L"de", L"en-US"}, L"de-DE_phoneb", "de", "de-DE,de,en-US");
  check({L"", L"invalid,tag"}, L"es-MX", "es-MX", "es-MX");
  check({}, L"", "en-US", "en-US");
  return failures ? 1 : 0;
}
