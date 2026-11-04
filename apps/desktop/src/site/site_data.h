#pragma once
#include <string>
#include "app/state.h"
#include "site/cookie_domain.h"
namespace red {
// Site popover "…" actions for the visible page; both reload the page after.
// Cookies: every cookie that would be sent to the page's host (including parent
// domain cookies, which other subdomains share, as in Chrome's per-site clear).
// Cache: the origin's Cache Storage, service workers and shader cache, then a
// reload that bypasses the HTTP cache. Chromium's HTTP cache has no public
// per-site removal, so it is not purged wholesale.
void ClearSiteCookies(const std::shared_ptr<State>& state);
void ClearSiteCache(const std::shared_ptr<State>& state);
}  // namespace red
