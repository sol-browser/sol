#include "downloads/download_names.h"
#include <cstdio>
#include <set>

int main() {
  int failures = 0;
  auto check = [&](bool condition, const char* description) {
    if (!condition) { std::fprintf(stderr, "FAIL: %s\n", description); ++failures; }
  };
  using red::SanitizeDownloadName;
  check(SanitizeDownloadName(L"report.pdf") == L"report.pdf", "ordinary name kept");
  check(SanitizeDownloadName(L"Отчёт 2026.xlsx") == L"Отчёт 2026.xlsx", "Unicode name kept");
  check(SanitizeDownloadName(L"..\\..\\Windows\\evil.dll") == L"evil.dll", "backslash traversal reduced to one component");
  check(SanitizeDownloadName(L"../../etc/passwd") == L"passwd", "slash traversal reduced to one component");
  check(SanitizeDownloadName(L"a:b*c?d\"e<f>g|h.txt") == L"a_b_c_d_e_f_g_h.txt", "reserved characters replaced");
  check(SanitizeDownloadName(L"x\ny\x01.txt") == L"x_y_.txt", "control characters replaced");
  check(SanitizeDownloadName(L"CON") == L"_CON" && SanitizeDownloadName(L"nul.txt") == L"_nul.txt" &&
        SanitizeDownloadName(L"Com1.tar.gz") == L"_Com1.tar.gz", "reserved device names neutralised");
  check(SanitizeDownloadName(L"CONSOLE.txt") == L"CONSOLE.txt", "names merely starting with a device name kept");
  check(SanitizeDownloadName(L"setup.exe. . ") == L"setup.exe", "trailing dots and spaces removed");
  check(SanitizeDownloadName(L".hidden") == L"hidden", "leading dot removed");
  check(SanitizeDownloadName(L"") == L"download" && SanitizeDownloadName(L"...") == L"download" &&
        SanitizeDownloadName(L"dir/") == L"download", "empty result gets a default name");
  const auto longName = SanitizeDownloadName(std::wstring(400, L'a') + L".pdf");
  check(longName.size() == 180 && longName.ends_with(L".pdf"), "long names shortened, extension kept");

  std::set<std::filesystem::path> present = {L"C:\\d\\report.pdf", L"C:\\d\\report (1).pdf"};
  const auto exists = [&](const std::filesystem::path& p) { return present.contains(p); };
  check(red::UniqueDownloadPath(L"C:\\d", L"report.pdf", exists) == L"C:\\d\\report (2).pdf", "existing files never overwritten");
  check(red::UniqueDownloadPath(L"C:\\d", L"new.zip", exists) == L"C:\\d\\new.zip", "free name used as is");
  check(red::UniqueDownloadPath(L"C:\\d", L"README", [](const auto& p) { return p == L"C:\\d\\README"; }) == L"C:\\d\\README (1)",
        "names without extension get a counter");
  std::printf("Download name tests: %s\n", failures ? "FAIL" : "PASS");
  return failures ? 1 : 0;
}
