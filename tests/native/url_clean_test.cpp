#include "navigation/url_clean.h"
#include <cstdio>
#include <string>

// Super Copy removes only the parameters that identify a click or a campaign, and nothing else.
namespace {
int failures = 0;
void Expect(const std::string& url, const std::string& want, bool changed) {
  bool did = false;
  const auto got = red::StripTrackingParameters(url, &did);
  if (got != want || did != changed) { std::printf("FAIL %s\n  got  %s (%d)\n  want %s (%d)\n", url.c_str(), got.c_str(), did, want.c_str(), changed); ++failures; }
}
}  // namespace

int main() {
  const std::string site = std::string("https") + "://example.com";
  Expect(site + "/a?utm_source=x&utm_medium=y&id=7", site + "/a?id=7", true);
  Expect(site + "/a?id=7&fbclid=abc#top", site + "/a?id=7#top", true);
  Expect(site + "/a?gclid=1", site + "/a", true);
  Expect(site + "/a?gclid=1#frag", site + "/a#frag", true);
  Expect(site + "/a?q=hello+world&UTM_Campaign=Z&page=2", site + "/a?q=hello+world&page=2", true);
  Expect(site + "/watch?v=abc&t=30&si=keep", site + "/watch?v=abc&t=30&si=keep", false);  // Generic names stay.
  Expect(site + "/a?ref=home&source=nav&id=1", site + "/a?ref=home&source=nav&id=1", false);
  Expect(site + "/a?flag&utm_source=x&other=", site + "/a?flag&other=", true);  // Valueless and empty values survive.
  Expect(site + "/a?hsa_acc=1&hsa_net=2&mc_eid=3&_ga=4", site + "/a", true);
  Expect(site + "/a", site + "/a", false);
  Expect(site + "/a#x?utm_source=1", site + "/a#x?utm_source=1", false);  // A "?" inside the fragment is not a query.
  Expect(site + "/a?", site + "/a?", false);
  Expect("about:blank?utm_source=1", "about:blank?utm_source=1", false);  // Only web addresses.
  Expect("file:///C:/a.html?utm_source=1", "file:///C:/a.html?utm_source=1", false);
  Expect("", "", false);
  Expect(site + "/a?utm_source=x&&id=1&", site + "/a?id=1", true);
  if (failures) return 1;
  std::puts("url_clean ok");
  return 0;
}
