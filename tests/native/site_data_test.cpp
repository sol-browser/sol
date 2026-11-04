#include "site/cookie_domain.h"
#include <cstdio>

int main() {
  int failures = 0;
  auto check = [&](bool condition, const char* description) {
    if (!condition) { std::fprintf(stderr, "%s\n", description); ++failures; }
  };
  check(red::CookieDomainMatches("www.youtube.com", "www.youtube.com"), "host cookie");
  check(red::CookieDomainMatches(".youtube.com", "www.youtube.com"), "parent domain cookie");
  check(!red::CookieDomainMatches("youtube.com", "m.youtube.com"), "host-only cookie stays on its host");
  check(!red::CookieDomainMatches(".youtube.com", "notyoutube.com"), "suffix without label boundary");
  check(!red::CookieDomainMatches("www.youtube.com", "youtube.com"), "child cookie not sent to parent");
  check(!red::CookieDomainMatches(".google.com", "www.youtube.com"), "other site");
  check(!red::CookieDomainMatches(".", "example.com") && !red::CookieDomainMatches("", "example.com"), "empty domain");
  return failures;
}
