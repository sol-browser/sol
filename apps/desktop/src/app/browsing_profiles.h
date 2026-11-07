#pragma once
#include "app/state.h"
#include "extensions/pinned_extensions.h"
#include <map>
#include <mutex>

namespace red {
struct BrowsingProfile {
  // Views browser creation requires a fully initialized request context.
  bool initialized = false;
  std::vector<std::function<void()>> on_initialized;
  void WhenReady(std::function<void()> action) {
    if(initialized) action(); else on_initialized.push_back(std::move(action));
  }
  std::string id;
  std::filesystem::path path;
  CefRefPtr<CefRequestContext> context;
  std::shared_ptr<HistoryService> history;
  std::shared_ptr<FaviconCache> favicons;
  std::shared_ptr<ExtensionCatalog> extensions;
  std::shared_ptr<DownloadManager> downloads;
  std::shared_ptr<PinnedExtensions> pins;
  CefRefPtr<CefRegistration> install_watch;
};
// Owned by AppSession. All selection and lifetime operations run on TID_UI.
class BrowsingProfiles {
 public:
  void AdoptDefault(const std::shared_ptr<State>& state);
  std::shared_ptr<BrowsingProfile> Get(const std::shared_ptr<State>& state, const std::string& id);
  std::shared_ptr<BrowsingProfile> ForContext(CefRefPtr<CefRequestContext> context) const;
  void Stop();
  std::optional<std::string> FaviconPng(std::string_view path) const;
 private:
  std::map<std::string,std::shared_ptr<BrowsingProfile>> profiles_;
  mutable std::mutex mutex_;
};
std::shared_ptr<BrowsingProfile> ProfileForBrowser(const std::shared_ptr<State>& state, CefRefPtr<CefBrowser> browser);
bool SelectBrowsingProfile(const std::shared_ptr<State>& state, const std::string& id);
std::shared_ptr<PinnedExtensions> ActiveProfilePins(const std::shared_ptr<State>& state);
}
