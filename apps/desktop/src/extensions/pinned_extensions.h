#pragma once
#include <filesystem>
#include <string>
#include <vector>
#include "include/cef_request_context.h"
#include "include/cef_values.h"
namespace red {
class ExtensionCatalog;
// Extensions pinned to the top bar (Arc/Chrome toolbar pins), in pin order. Stored as
// BrowserData/pinned_extensions.json; an extension that is no longer installed simply drops out
// of the shown list (its pin is kept, so reinstalling restores it). UI thread only.
class PinnedExtensions {
 public:
  static constexpr size_t kMax = 12;
  void Load(const std::filesystem::path& file);
  bool Contains(const std::string& id) const;
  // Pins or unpins; false when nothing changed (unknown id, full, or already so) or the file failed.
  bool Set(const std::string& id, bool pinned);
  // The pinned, installed extensions as [{id, name, icon}] for the top bar; recomputed from
  // |catalog| only after a change (Set, MarkStale on an install/removal).
  CefRefPtr<CefListValue> Items(ExtensionCatalog& catalog, CefRefPtr<CefRequestContext> context);
  void MarkStale() { stale_ = true; }
 private:
  bool Save() const;
  std::filesystem::path file_;
  std::vector<std::string> ids_;
  CefRefPtr<CefListValue> items_;
  bool stale_ = true;
};
// A Chrome extension id: 32 letters a-p.
bool ValidExtensionId(const std::string& id);
}  // namespace red
