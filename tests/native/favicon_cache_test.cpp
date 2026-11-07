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
  cache.StartDocument(1, "example.com");
  cache.StartDocument(2, "example.com");
  auto result = cache.Complete(1, *cache.Begin(1, "a"), "example.com", "png-a");
  check(result.changed && result.seeded, "first document icon seeds the site fallback");
  const auto fallback = cache.Url("example.com");
  check(fallback == "sol://ui/favicon/example.com/v1", "URL names the version");
  check(!cache.PageUrl(2), "a site fallback learned later does not repaint another document");
  result = cache.Complete(2, *cache.Begin(2, "a"), "example.com", "png-a");
  check(result.changed && !result.seeded && cache.PageUrl(2) == fallback, "identical images share one URL");
  result = cache.Complete(1, *cache.Begin(1, "b"), "example.com", "png-b");
  check(result.changed && !result.seeded && cache.PageUrl(1) == "sol://ui/favicon/example.com/v2", "a changed icon gets a new version");
  check(cache.PageUrl(2) == fallback && cache.Url("example.com") == fallback, "another document and the site default stay independent");
  check(cache.PngForPath("/favicon/example.com/v1") == "png-a" && cache.PngForPath("/favicon/example.com/v2") == "png-b",
        "the resource serves the version named by the URL");
  check(!cache.PngForPath("/favicon/example.com/v3") && !cache.PngForPath("/favicon/example.com/v01") &&
        !cache.PngForPath("/favicon/example.com/v1/extra"), "unknown and malformed versions do not fall back");
  check(!cache.Seed("example.com", "png-c"), "a later cache restore does not replace the site fallback");
  const auto stale = cache.Begin(1, "pending");
  cache.StartDocument(1, "example.com");
  check(cache.PageUrl(1) == fallback && !cache.Complete(1, *stale, "example.com", "wrong").changed,
        "a new document restores its own fallback and rejects the previous download");
  check(!cache.PngForPath("/favicon/example.com/v2"), "an unreferenced version never serves the latest bytes");
  cache.ForgetPage(2);
  check(!cache.PageUrl(2) && cache.Url("example.com") == fallback, "closing a tab releases its selection but keeps the fallback");
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
  red::FaviconCache second_profile;
  second_profile.Seed("example.com","different-profile-icon");
  const auto other_icon = second_profile.Url("example.com");
  check(other_icon && other_icon != fallback, "same-host icons in different profiles have distinct resource URLs");
  check(other_icon && !cache.PngForPath(other_icon->substr(std::string("sol://ui").size())),
        "another profile's icon URL never resolves to this profile's bytes");
  return failures ? 1 : 0;
}
