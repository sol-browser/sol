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

}  // namespace red
