#include "history/favicon_cache.h"
#include "history/favicon_request.h"
#include <cstdio>

int main() {
  int failures = 0;
  auto check = [&](bool condition, const char* description) {
    if (!condition) { std::fprintf(stderr, "%s\n", description); ++failures; }
  };
  red::FaviconCache cache;
  check(!cache.Url("example.com"), "no icon, no URL");
  check(cache.Put("example.com", "png-a") == 1, "first icon is version 1");
  check(cache.Url("example.com") == "sol://ui/favicon/example.com/1", "URL names the version");
  // A page re-downloading the same icon on a cold load keeps the URL, so the
  // sidebar's image and tile tint do not reload.
  check(cache.Put("example.com", "png-a") == 1, "same icon keeps its version");
  check(cache.Put("example.com", "png-b") == 2, "a changed icon gets a new version");
  check(cache.Put("other.org", "png-a") == 1, "versions are per host");
  red::FaviconRequest request;
  auto first=request.Begin("https://example.com/icon.svg");
  check(first.has_value(),"first candidate starts a request");
  check(!request.Begin("https://example.com/icon.svg"),"duplicate while downloading is coalesced");
  request.NewDocument();
  auto next=request.Begin("https://example.com/icon.svg");
  check(next.has_value() && !request.Complete(*first),"reload allows same URL and rejects old document callback");
  check(request.Complete(*next),"current callback accepted");
  check(!request.Complete(*next),"callback accepted only once");
  check(!request.Begin("https://example.com/icon.svg"),"repeated resize cannot refetch completed icon");
  auto changed=request.Begin("https://example.com/badge.svg");
  check(changed.has_value(),"site changing icon still downloads");
  auto reverted=request.Begin("https://example.com/icon.svg");
  check(reverted.has_value() && !request.Complete(*changed),"latest icon selection wins a callback race");
  check(!request.Begin(""),"empty URL rejected");
  return failures ? 1 : 0;
}
