#pragma once
#include <algorithm>
#include <array>
#include <cwctype>
#include <filesystem>
#include <functional>
#include <string>

namespace red {
// A website controls the suggested name: treat it as untrusted. The result is
// a single Windows file name component (never a path), never a reserved
// device name, never empty, and short enough for typical download folders.
inline std::wstring SanitizeDownloadName(std::wstring name) {
  // Only the last component counts: "..\..\x.txt" and "a/b.txt" become "x.txt" / "b.txt".
  if (const auto slash = name.find_last_of(L"/\\"); slash != std::wstring::npos) name.erase(0, slash + 1);
  std::wstring clean;
  for (const wchar_t c : name) {
    if (c < 32 || c == 127 || std::wstring_view(L"<>:\"|?*").find(c) != std::wstring_view::npos) clean += L'_';
    else clean += c;
  }
  // Windows silently drops trailing dots and spaces; leading ones hide files.
  while (!clean.empty() && (clean.back() == L'.' || clean.back() == L' ')) clean.pop_back();
  while (!clean.empty() && (clean.front() == L'.' || clean.front() == L' ')) clean.erase(0, 1);
  if (clean.empty()) clean = L"download";
  // Reserved device names are reserved with any extension ("CON.txt").
  std::wstring stem = clean.substr(0, clean.find(L'.'));
  std::transform(stem.begin(), stem.end(), stem.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towupper(c)); });
  static constexpr std::array<std::wstring_view, 22> kReserved = {L"CON", L"PRN", L"AUX", L"NUL",
      L"COM1", L"COM2", L"COM3", L"COM4", L"COM5", L"COM6", L"COM7", L"COM8", L"COM9",
      L"LPT1", L"LPT2", L"LPT3", L"LPT4", L"LPT5", L"LPT6", L"LPT7", L"LPT8", L"LPT9"};
  if (std::find(kReserved.begin(), kReserved.end(), stem) != kReserved.end()) clean = L"_" + clean;
  // Keep the extension when shortening so the file still opens correctly.
  constexpr size_t kMax = 180;
  if (clean.size() > kMax) {
    const auto dot = clean.find_last_of(L'.');
    const std::wstring extension = dot != std::wstring::npos && clean.size() - dot <= 16 ? clean.substr(dot) : L"";
    clean = clean.substr(0, kMax - extension.size()) + extension;
  }
  return clean;
}

// Never overwrite: "report.pdf", "report (1).pdf", "report (2).pdf", ...
inline std::filesystem::path UniqueDownloadPath(const std::filesystem::path& directory, const std::wstring& name,
                                                const std::function<bool(const std::filesystem::path&)>& exists) {
  const std::filesystem::path base(name);
  const std::wstring stem = base.stem().wstring(), extension = base.extension().wstring();
  auto candidate = directory / name;
  for (int i = 1; exists(candidate) && i < 10000; ++i)
    candidate = directory / (stem + L" (" + std::to_wstring(i) + L")" + extension);
  return candidate;
}
}  // namespace red
