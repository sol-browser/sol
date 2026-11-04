#include "windows/link_status_text.h"
#include <iostream>
int main() {
  const auto check = [](bool value) { if (!value) { std::cerr << "Link status text invariant failed\n"; return false; } return true; };
  if (!check(red::LinkStatusText(L"http://localhost:63079/target?a=@b#frag") == L"http://localhost:63079/target?a=@b#frag") ||
      !check(red::LinkStatusText(L"https://user:secret@example.com/path?q=1") == L"https://example.com/path?q=1") ||
      !check(red::LinkStatusText(L"https://user:pass@[::1]:8443/a") == L"https://[::1]:8443/a") ||
      !check(red::LinkStatusText(L"https://example.com/\x202e" L"safe\n") == L"https://example.com/safe") ||
      !check(red::LinkStatusText(L"mailto:a@example.com") == L"mailto:a@example.com") ||
      !check(red::LinkStatusText(L"").empty()) ||
      !check(red::LinkStatusText(L"https://example.com/" + std::wstring(6000,L'x')).size() == 4097)) return 1;
  return 0;
}
