#pragma once
#include "import/zen_reader.h"
#include <functional>

namespace red {
struct CookieWriteResult { int written = 0, kept = 0, failed = 0; bool flushed = false; };
// Adds cookies to the global web context through the public CefCookieManager:
// existing cookie identities (domain, path, name) win, every SetCookie result
// is counted, and FlushStore confirms persistence. Values never leave native code.
void WriteCookies(std::vector<ZenCookie> cookies, std::function<void(CookieWriteResult)> done);
}  // namespace red
