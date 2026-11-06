#include "app/windows_locale.h"

#include <windows.h>

#include <algorithm>
#include <utility>
#include <vector>

namespace red {
namespace {

std::string LanguageTag(std::wstring_view locale) {
  // Windows can append an alternate sort order, such as de-DE_phoneb.
  const auto tag = locale.substr(0, locale.find(L'_'));
  if (!std::ranges::all_of(tag, [](wchar_t c) {
        return (c >= L'A' && c <= L'Z') || (c >= L'a' && c <= L'z') ||
               (c >= L'0' && c <= L'9') || c == L'-';
      })) return {};
  std::string ascii;
  ascii.reserve(tag.size());
  for (const wchar_t c : tag) ascii.push_back(static_cast<char>(c));
  return ascii;
}

std::string_view PrimaryLanguage(std::string_view tag) {
  return tag.substr(0, tag.find('-'));
}

}  // namespace

BrowserLocale ResolveBrowserLocale(std::span<const std::wstring_view> preferred_languages,
                                   std::wstring_view regional_format) {
  std::vector<std::string> languages;
  for (const auto preference : preferred_languages) {
    auto tag = LanguageTag(preference);
    if (!tag.empty() && std::ranges::find(languages, tag) == languages.end())
      languages.push_back(std::move(tag));
  }

  const auto regional_tag = LanguageTag(regional_format);
  if (languages.empty()) languages.push_back(regional_tag.empty() ? "en-US" : regional_tag);
  BrowserLocale locale{languages.front(), {}};
  // Use the format's region to refine a neutral language. A full preferred tag
  // may specify a different region or script deliberately.
  if (locale.chromium.find('-') == std::string::npos &&
      regional_tag.find('-') != std::string::npos &&
      PrimaryLanguage(regional_tag) == PrimaryLanguage(locale.chromium)) {
    std::erase(languages, regional_tag);
    languages.insert(languages.begin(), regional_tag);
  }
  for (const auto& tag : languages) {
    if (!locale.accept_languages.empty()) locale.accept_languages += ',';
    locale.accept_languages += tag;
  }
  return locale;
}

BrowserLocale ReadWindowsBrowserLocale() {
  wchar_t regional_format[LOCALE_NAME_MAX_LENGTH]{};
  GetUserDefaultLocaleName(regional_format, LOCALE_NAME_MAX_LENGTH);

  ULONG count = 0;
  ULONG size = 0;
  std::vector<wchar_t> buffer;
  std::vector<std::wstring_view> preferred_languages;
  if (GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, &count, nullptr, &size) && size) {
    buffer.resize(size);
    if (GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, &count, buffer.data(), &size)) {
      const auto* tag = buffer.data();
      for (ULONG index = 0; index < count; ++index) {
        preferred_languages.emplace_back(tag);
        tag += preferred_languages.back().size() + 1;
      }
    }
  }
  return ResolveBrowserLocale(preferred_languages, regional_format);
}

}  // namespace red
