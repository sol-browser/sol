#include "windows/default_browser_rules.h"

#include <cstdio>

// Sol's Windows registration rules (docs/settings.md): which Chromium
// registrations Sol may remove, and which file types it offers.
int main() {
  int failures = 0;
  auto check = [&](bool condition, const char* description) {
    if (!condition) { std::fprintf(stderr, "FAIL: %s\n", description); ++failures; }
  };
  // Chromium's own shell integration names: "Chromium" and "Chromium.<suffix>".
  check(red::ChromiumRegistrationSuffix(L"Chromium") == std::wstring(), "plain Chromium");
  check(red::ChromiumRegistrationSuffix(L"Chromium.RLEAMMB5RN5VLIGS5SPARXCWK4") == std::wstring(L".RLEAMMB5RN5VLIGS5SPARXCWK4"),
        "per-user suffix");
  for (const wchar_t* name : {L"", L"Sol", L"Google Chrome", L"Chromium.", L"ChromiumX", L"Chromium.A B", L"Chromium..A",
                              L"Chromium.A\\B", L"chromium.ABC", L"Microsoft Edge", L"ChromiumHTM.ABC"})
    check(!red::ChromiumRegistrationSuffix(name), "not a Chromium registration name");

  // Only registrations that start the installed Sol are Sol's to remove.
  const std::filesystem::path sol = L"C:\\Users\\a\\AppData\\Local\\SolBrowser\\current\\sol.exe";
  for (const wchar_t* value : {L"\"C:\\Users\\a\\AppData\\Local\\SolBrowser\\current\\sol.exe\" --single-argument %1",
                               L"C:\\Users\\a\\AppData\\Local\\SolBrowser\\current\\sol.exe,0",
                               L"c:\\users\\a\\appdata\\local\\solbrowser\\current\\SOL.EXE",
                               L"C:\\Users\\a\\AppData\\Local\\SolBrowser\\current\\sol.exe -- \"%1\""})
    check(red::StartsExecutable(value, sol), "starts Sol");
  for (const wchar_t* value : {L"", L"\"C:\\Program Files\\Chromium\\chrome.exe\" -- \"%1\"",
                               L"C:\\Users\\a\\AppData\\Local\\SolBrowser\\current\\sol.exe.bak",
                               L"\"C:\\Users\\a\\AppData\\Local\\SolBrowser\\current\\sol.exe2\" %1",
                               L"D:\\C:\\Users\\a\\AppData\\Local\\SolBrowser\\current\\sol.exe"})
    check(!red::StartsExecutable(value, sol), "does not start Sol");
  check(!red::StartsExecutable(L"anything", std::filesystem::path{}), "no installed Sol");

  // The offered types match Chromium's, PDF apart.
  int types = 0;
  for (const auto* type : red::kSolDocumentTypes) { ++types; check(type[0] == L'.', "extension"); }
  check(types == 8 && std::wstring(red::kSolPdfType) == L".pdf", "offered types");

  if (failures) return 1;
  std::puts("default_browser_rules: all checks passed");
  return 0;
}
