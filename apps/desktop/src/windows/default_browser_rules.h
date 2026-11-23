#pragma once
// Pure rules of Sol's Windows registration (default_browser.cpp), without CEF,
// for tests/native/default_browser_rules_test.cpp.
#include <cwchar>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
namespace red {
// File types Sol offers to open, as Chromium browsers do. Sol opens any local
// file it is launched with (navigation/external_launch.cpp), and Chromium
// shows these itself. PDF gets its own ProgID so Windows lists it as a PDF.
inline constexpr const wchar_t* kSolDocumentTypes[] = {L".htm", L".html", L".shtml", L".xht", L".xhtml", L".mhtml", L".svg", L".webp"};
inline constexpr const wchar_t* kSolPdfType = L".pdf";
// A key name Chromium's own shell integration writes ("Chromium" or
// "Chromium.<per-user suffix>"): the suffix part, or nothing for other names.
inline std::optional<std::wstring> ChromiumRegistrationSuffix(std::wstring_view name) {
  constexpr std::wstring_view prefix = L"Chromium";
  if (!name.starts_with(prefix)) return std::nullopt;
  const auto suffix = name.substr(prefix.size());
  if (suffix.empty()) return std::wstring{};
  if (suffix.size() < 2 || suffix.size() > 65 || suffix[0] != L'.') return std::nullopt;
  for (const wchar_t c : suffix.substr(1))
    if (!((c >= L'A' && c <= L'Z') || (c >= L'a' && c <= L'z') || (c >= L'0' && c <= L'9'))) return std::nullopt;
  return std::wstring(suffix);
}
// A registry command or icon value that starts |executable| ("\"path\" args",
// "path,0"), compared without case. Only those registrations are Sol's to remove.
inline bool StartsExecutable(std::wstring_view value, const std::filesystem::path& executable) {
  const auto path = executable.wstring();
  if (path.empty()) return false;
  if (!value.empty() && value.front() == L'"') value.remove_prefix(1);
  if (value.size() < path.size() || _wcsnicmp(value.data(), path.c_str(), path.size()) != 0) return false;
  const auto rest = value.substr(path.size());
  return rest.empty() || rest.front() == L'"' || rest.front() == L',' || rest.front() == L' ';
}
}  // namespace red
