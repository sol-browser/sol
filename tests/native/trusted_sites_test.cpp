#include "app/trusted_sites.h"
#include <cstdio>
#include <cstdlib>

namespace {
int failures = 0;
void check(bool ok, const char* name) { if (!ok) { std::fprintf(stderr, "FAIL: %s\n", name); ++failures; } }
}  // namespace

int main() {
  check(red::ValidTrustedSite("example.com") && red::ValidTrustedSite("a-b.co.uk") && red::ValidTrustedSite("localhost"), "plain hostnames");
  check(!red::ValidTrustedSite("") && !red::ValidTrustedSite("Example.com") && !red::ValidTrustedSite("a..b") &&
        !red::ValidTrustedSite(".a.com") && !red::ValidTrustedSite("a.com/") && !red::ValidTrustedSite("*.a.com") &&
        !red::ValidTrustedSite("a.com\nb.com"), "no patterns, paths, upper case or line breaks");
  check(red::WithTrustedSite("a.com\nb.com\n", "c.com", true) == "a.com\nb.com\nc.com\n", "adds a site");
  check(red::WithTrustedSite("a.com\nb.com\n", "b.com", true) == "a.com\nb.com\n", "no duplicate");
  check(red::WithTrustedSite("a.com\r\nb.com\r\nb.com\n", "b.com", false) == "a.com\n", "removes every copy");
  check(red::WithTrustedSite("", "a.com", false).empty(), "removing from an empty list");
  return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
