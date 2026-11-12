#pragma once
#include <list>
#include <map>
#include <memory>
#include <string>
#include "include/cef_browser.h"

namespace red {
// Small pictures of tabs for the Ctrl+Tab switcher. A picture is taken of what a page shows (Chrome's own
// Page.captureScreenshot, scaled down to about 440 px wide, JPEG) and kept in memory only, for the last
// few tabs; a tab that was never shown in this run has none and the switcher shows its icon instead.
// UI thread only.
class TabThumbnails {
 public:
  TabThumbnails();
  // Takes a picture of |page| now for tab |id|; the cache holds the result a moment later.
  void Capture(CefRefPtr<CefBrowser> page, const std::string& id);
  // JPEG bytes, or empty.
  std::string Get(const std::string& id) const;
  void Forget(const std::string& id);
  struct Store;
 private:
  std::shared_ptr<Store> store_;
};
}  // namespace red
