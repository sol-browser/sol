#include "web_url.h"
#include <cstdio>
#include <string>

// The scheme check every validator used to spell out. The shared helper must agree with it
// on every address, so replacing the copies cannot change what Sol accepts.
namespace {
std::size_t Reference(const std::string& url) {
  if (url.starts_with(std::string("https") + "://")) return 8;
  if (url.starts_with(std::string("http") + "://")) return 7;
  return 0;
}
}  // namespace

int main() {
  int failures = 0;
  auto check = [&](bool condition, const char* description) {
    if (!condition) { std::fprintf(stderr, "%s\n", description); ++failures; }
  };
  const std::string http = std::string("http") + "://", https = std::string("https") + "://";
  check(red::WebSchemeLength(https + "example.com/") == 8, "https prefix is 8");
  check(red::WebSchemeLength(http + "example.com/") == 7, "http prefix is 7");
  check(red::WebSchemeLength(https) == 8 && red::WebSchemeLength(http) == 7, "a bare scheme still counts, as before");
  check(red::HasWebScheme(https + "a") && red::HasWebScheme(http + "a"), "both web schemes");
  // Everything that is not exactly http:// or https:// stays out.
  for (const char* other : {"", "http", "http:", "http:/", "http:/x", "https:", "https:/x", "ftp://x", "file:///c:/x", "chrome://newtab",
                            "sol://ui/index.html", "about:blank", "javascript:alert(1)", "data:text/html,x", "chrome-extension://abc/x.html",
                            "HTTP://x", "Https://x", "httpx://x", "xhttp://x", "ahttps://x", " http://x", "ws://x", "wss://x",
                            "https//x", "http//x", "http:\\\\x", "://x", "x://http://y"})
    check(red::WebSchemeLength(other) == Reference(other), other);
  // The old test on every address over a small alphabet: all strings up to eight characters.
  const char alphabet[] = {'h', 't', 'p', 's', ':', '/'};
  std::string text;
  const auto walk = [&](auto&& self, int remaining) -> void {
    if (red::WebSchemeLength(text) != Reference(text)) { check(false, text.c_str()); return; }
    if (!remaining) return;
    for (const char c : alphabet) { text.push_back(c); self(self, remaining - 1); text.pop_back(); }
  };
  walk(walk, 8);
  constexpr bool compile_time = red::HasWebScheme("https://example.com") && !red::HasWebScheme("ftp://example.com");
  check(compile_time, "usable in constant expressions");
  return failures ? 1 : 0;
}
