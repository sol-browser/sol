#include "app/browsing_profiles.h"
#include "app/session.h"
#include "app/windows_locale.h"
#include "storage/tree_store.h"
#include "history/history_service.h"
#include "history/favicon_cache.h"
#include "extensions/extension_catalog.h"
#include "downloads/download_manager.h"
#include "windows/extension_popup.h"
#include "windows/extensions_window.h"
#include "workspace/controller.h"
#include "include/cef_request_context_handler.h"

namespace red {
namespace {
class ProfileReady final : public CefRequestContextHandler {
 public:
  explicit ProfileReady(std::weak_ptr<BrowsingProfile> profile) : profile_(std::move(profile)) {}
  void OnRequestContextInitialized(CefRefPtr<CefRequestContext> context) override {
    // Every Chrome profile has its own preferences. Suppress Chrome's toolbar
    // bubble here as well: Sol supplies the downloads popover for these pages.
    auto off=CefValue::Create(); off->SetBool(false); CefString error;
    if (context->CanSetPreference("download_bubble.partial_view_enabled"))
      context->SetPreference("download_bubble.partial_view_enabled",off,error);
    if(auto profile=profile_.lock()) {
      profile->initialized=true;
      auto actions=std::move(profile->on_initialized);
      for(auto& action:actions) action();
    }
  }
 private:
  std::weak_ptr<BrowsingProfile> profile_;
  IMPLEMENT_REFCOUNTING(ProfileReady);
};
}
void BrowsingProfiles::AdoptDefault(const std::shared_ptr<State>& state) {
  std::lock_guard lock(mutex_);
  auto profile = std::make_shared<BrowsingProfile>();
  profile->initialized = true;
  profile->id = "default"; profile->path = state->profile / "Default";
  profile->context = CefRequestContext::GetGlobalContext();
  profile->history = state->history; profile->favicons = state->favicons;
  profile->extensions = std::make_shared<ExtensionCatalog>(profile->path);
  profile->downloads = state->downloads;
  profile->downloads->SetHistory(profile->history);
  profile->pins = state->session->pinned_extensions;
  profile->install_watch = state->extension_install_watch;
  profiles_.emplace(profile->id, profile);
}
std::shared_ptr<BrowsingProfile> BrowsingProfiles::Get(const std::shared_ptr<State>& state, const std::string& id) {
  std::lock_guard lock(mutex_);
  if (const auto found = profiles_.find(id); found != profiles_.end()) return found->second;
  if (!ValidBrowsingProfileId(id) || id == "default") return nullptr;
  auto profile = std::make_shared<BrowsingProfile>();
  profile->id = id;
  // Chrome Runtime requires a direct child of root_cache_path.
  profile->path = state->profile / ("Profile-" + id);
  CefRequestContextSettings settings;
  CefString(&settings.cache_path) = profile->path.wstring();
  settings.persist_session_cookies = true;
  CefString(&settings.accept_language_list) = ReadWindowsBrowserLocale().accept_languages;
  profile->context = CefRequestContext::CreateContext(settings, new ProfileReady(profile));
  if (!profile->context) return nullptr;
  const auto data = state->profile / "BrowserData" / ("Profile-" + id);
  profile->history = std::make_shared<HistoryService>(state);
  profile->history->Start(data / "history.sqlite3", [](std::vector<DownloadRecord>) {});
  profile->favicons = std::make_shared<FaviconCache>();
  profile->history->LoadFavicons([icons = profile->favicons, session=std::weak_ptr<AppSession>(state->session)](std::vector<std::pair<std::string,std::string>> saved) {
    for (const auto& [host,png] : saved) icons->Seed(host,png);
    if (const auto owner=session.lock()) for (const auto& window : owner->Windows())
      if (window->workspace && window->favicons==icons) window->workspace->Refresh();
  });
  profile->extensions = std::make_shared<ExtensionCatalog>(profile->path);
  profile->downloads = std::make_shared<DownloadManager>(state, state->downloads_directory.empty() ? DownloadManager::DefaultDirectory() : state->downloads_directory);
  profile->downloads->SetHistory(profile->history);
  profile->pins = std::make_shared<PinnedExtensions>(); profile->pins->Load(data / "pinned_extensions.json");
  profiles_.emplace(id, profile);
  return profile;
}
std::shared_ptr<BrowsingProfile> BrowsingProfiles::ForContext(CefRefPtr<CefRequestContext> context) const {
  std::lock_guard lock(mutex_);
  for (const auto& [id,profile] : profiles_) if (context && profile->context->IsSharingWith(context)) return profile;
  return nullptr;
}
void BrowsingProfiles::Stop() {
  std::lock_guard lock(mutex_);
  for (const auto& [id,profile] : profiles_) {
    profile->install_watch = nullptr;
    profile->history->Stop();
  }
  profiles_.clear();
}
std::optional<std::string> BrowsingProfiles::FaviconPng(std::string_view path) const {
  std::lock_guard lock(mutex_);
  for (const auto& [id,profile] : profiles_) if (auto png = profile->favicons->PngForPath(path)) return png;
  return std::nullopt;
}
std::shared_ptr<BrowsingProfile> ProfileForBrowser(const std::shared_ptr<State>& state, CefRefPtr<CefBrowser> browser) {
  if (!state || state->private_window || !state->session || !state->session->profiles || !browser) return nullptr;
  if (const auto found = state->browser_profiles.find(browser->GetIdentifier()); found != state->browser_profiles.end()) return found->second;
  return state->session->profiles->ForContext(browser->GetHost()->GetRequestContext());
}
std::shared_ptr<PinnedExtensions> ActiveProfilePins(const std::shared_ptr<State>& state) {
  if (!state->session) return nullptr;
  if (state->session->profiles && !state->private_window) {
    const auto profile = state->session->profiles->Get(state,state->active_profile);
    if (profile) return profile->pins;
  }
  return state->session->pinned_extensions;
}
bool SelectBrowsingProfile(const std::shared_ptr<State>& state, const std::string& id) {
  if (!state || state->private_window || !state->session || !state->session->profiles) return true;
  const auto profile = state->session->profiles->Get(state,id);
  if (!profile) return false;
  if (state->active_profile == id) return true;
  if (state->extension_popup) state->extension_popup->Close();
  if (state->extensions_window) state->extensions_window->Close();
  state->active_profile = id;
  state->web_context = profile->context;
  state->history = profile->history; state->favicons = profile->favicons;
  state->extensions = profile->extensions; state->downloads = profile->downloads;
  return true;
}
}
