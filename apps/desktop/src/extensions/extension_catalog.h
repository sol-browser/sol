#pragma once
#include <filesystem>
#include <map>
#include <string>
#include <vector>
#include "include/cef_request_context.h"
namespace red {
// One user-installed, enabled extension as the site popover shows it.
struct InstalledExtension {
  std::string id, name;
  std::string popup;    // Package path of the toolbar popup; empty when none.
  std::string options;  // Package path of the options page; empty when none.
  std::string icon;     // data:image/png;base64,… or empty.
  bool action = false;  // Declares a toolbar action (with or without a popup).
};
// Lists the extensions of the web profile from Chrome's own record of them
// (the "extensions.settings" preference) and each package's manifest. Chrome
// Runtime has no public extension list in CEF, so this reads what Chrome
// wrote; it never loads, enables or changes extensions. UI thread only.
class ExtensionCatalog {
 public:
  // |profile|: the web profile directory (contains Extensions/).
  explicit ExtensionCatalog(std::filesystem::path profile) : profile_(std::move(profile)) {}
  std::vector<InstalledExtension> List(CefRefPtr<CefRequestContext> context);
  // The entry with |id| from the latest List(), if any.
  const InstalledExtension* Find(const std::string& id) const;
 private:
  std::filesystem::path profile_;
  // Parsed packages by extension directory; a package directory is versioned,
  // so an update gets a new entry.
  std::map<std::filesystem::path, InstalledExtension> packages_;
  std::vector<InstalledExtension> latest_;
};
}  // namespace red
