#pragma once

#include <span>
#include <string>
#include <string_view>

namespace red {

struct BrowserLocale {
  std::string chromium;
  std::string accept_languages;
};

BrowserLocale ResolveBrowserLocale(std::span<const std::wstring_view> preferred_languages,
                                   std::wstring_view regional_format);
BrowserLocale ReadWindowsBrowserLocale();
bool ValidInterfaceLanguage(std::string_view value);
std::string ResolveInterfaceLanguage(std::string_view preference, std::string_view windows_locale);

}  // namespace red
