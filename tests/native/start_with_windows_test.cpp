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
  const red::KeyBinding binding{VK_F20, true, true, false};
  const auto argument = red::LittleSolHotkeyArgument(binding);
  check(!argument.empty() && red::ParseLittleSolHotkey(std::wstring_view(argument).substr(1 + red::kLittleSolHotkeySwitch.size())) == binding,
        "the tray helper reads the same custom chord that the browser writes");
  check(red::StartWithWindowsCommand(tray, "google", false, L"", binding).ends_with(argument),
        "the Windows startup command carries the custom Little Sol chord");
  check(red::LittleSolHotkeyLabel(binding) == "Ctrl+Shift+F20" && !red::ParseLittleSolHotkey(L"4096") &&
        !red::ParseLittleSolHotkey(L"-1") && !red::ValidLittleSolHotkey({VK_F12, true, true, false}),
        "bad switch values and the reserved F12 are rejected");
  if (failures) return EXIT_FAILURE;
  std::puts("start with windows: all checks passed");
  return EXIT_SUCCESS;
}
