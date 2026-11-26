#include "workspace/link_redirect.h"
#include <cstdio>
#include <string>

// Link Peek reads where a same-site out-link redirector leads (youtube.com/redirect?q=...).
int main() {
  int failures = 0;
  auto check = [&](bool condition, const char* description) {
    if (!condition) { std::fprintf(stderr, "%s\n", description); ++failures; }
  };
  const std::string https = std::string("https") + "://", http = std::string("http") + "://";
  using red::LinkRedirectTarget;
  check(LinkRedirectTarget(https + "www.youtube.com/redirect?event=video_description&redir_token=QUFF&q=" +
                           "https%3A%2F%2Flinktr.ee%2Frickastleynever&v=dQw4w9WgXcQ") == https + "linktr.ee/rickastleynever",
        "YouTube description link: q is the destination");
  check(LinkRedirectTarget(https + "www.google.com/url?sa=t&url=https%3a%2f%2fexample.org%2fa%3fb%3d1&usg=x") ==
            https + "example.org/a?b=1", "Google result redirector: url, lower-case escapes");
  check(LinkRedirectTarget(https + "vk.com/away.php?to=" + http + "example.org/x&cc_key=") == http + "example.org/x",
        "unescaped destination");
  check(LinkRedirectTarget(https + "a.test/out?Q=https%3A%2F%2Fb.test") == https + "b.test", "parameter names ignore case");
  check(LinkRedirectTarget(https + "a.test/r?event=x&q=hello&url=https%3A%2F%2Fb.test") == https + "b.test",
        "first parameter that holds a web address");
  check(LinkRedirectTarget(https + "a.test/search?q=cats").empty(), "a search term is not a destination");
  check(LinkRedirectTarget(https + "a.test/login?next=https%3A%2F%2Fb.test&continue=https%3A%2F%2Fb.test&return_to=" +
                           "https%3A%2F%2Fb.test&redirect_uri=https%3A%2F%2Fb.test").empty(),
        "login return addresses are not out-links");
  check(LinkRedirectTarget(https + "a.test/r?q=javascript%3Aalert(1)").empty() &&
            LinkRedirectTarget(https + "a.test/r?q=file%3A%2F%2F%2FC%3A%2Fx").empty(),
        "only web destinations");
  check(LinkRedirectTarget(https + "a.test/r#?q=https%3A%2F%2Fb.test").empty(), "a fragment is not a query");
  check(LinkRedirectTarget(https + "a.test/r?x=1#q=https%3A%2F%2Fb.test").empty(), "the query ends at the fragment");
  check(LinkRedirectTarget(https + "a.test/r?q=https%3A%2F%2Fb.test%").empty() == false, "a trailing % is kept, not read past");
  check(LinkRedirectTarget("ftp://a.test/r?q=https%3A%2F%2Fb.test").empty(), "only web links are read");
  check(LinkRedirectTarget(https + "a.test/r?q=" + std::string(5000, 'a')).empty(), "bounded length");
  return failures;
}
