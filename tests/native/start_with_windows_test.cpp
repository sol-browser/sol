// The Run value Sol writes for Start with Windows (windows/start_with_windows.h).
#include "windows/start_with_windows.h"
#include <cstdio>
#include <cstdlib>

namespace {
int failures = 0;
void check(bool ok, const char* what) {
  if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what); ++failures; }
}
}  // namespace

int main() {
  const std::filesystem::path tray = L"C:\\Users\\a b\\AppData\\Local\\SolBrowser\\current\\sol-tray.exe";
  check(red::StartWithWindowsCommand(tray, "google", true, L"") ==
            L"\"C:\\Users\\a b\\AppData\\Local\\SolBrowser\\current\\sol-tray.exe\" --search-engine=google --search-suggestions=1",
        "quoted path, engine and completions");
  check(red::StartWithWindowsCommand(tray, "duckduckgo", false, L"") ==
            L"\"C:\\Users\\a b\\AppData\\Local\\SolBrowser\\current\\sol-tray.exe\" --search-engine=duckduckgo",
        "completions off are left out");
  check(red::StartWithWindowsCommand(tray, "google", false, L"D:\\Sol Profile").ends_with(L" \"--profile-dir=D:\\Sol Profile\""),
        "a non-default profile is passed on, quoted");
  check(red::StartWithWindowsCommand(tray, "", true, L"").empty() && red::StartWithWindowsCommand(tray, "Goo gle", true, L"").empty() &&
            red::StartWithWindowsCommand(tray, "google\" --x", true, L"").empty(),
        "an engine that is not a plain id writes nothing");
  check(red::StartWithWindowsCommand(tray, "google", true, L"D:\\a\" --x").empty(), "a profile with a quote writes nothing");
  if (failures) return EXIT_FAILURE;
  std::puts("start with windows: all checks passed");
  return EXIT_SUCCESS;
}
