#include "extensions/install_notice.h"
#include <windows.h>
#include <set>
#include <vector>
#include "include/cef_preference.h"
#include "trust_policy.h"
#include "workspace/controller.h"
#include "app/session.h"
#include "app/browsing_profiles.h"

namespace red {
namespace {
constexpr char kSettings[] = "extensions.settings";

class Watcher final : public CefPreferenceObserver {
 public:
  Watcher(std::weak_ptr<State> state, CefRefPtr<CefRequestContext> content) : state_(std::move(state)), content_(content) {
    if (const auto owner=state_.lock()) session_=owner->session;
  }
  std::vector<std::string> InstalledIds() const {
    std::vector<std::string> result;
    const auto value = content_->GetPreference(kSettings);
    if (!value || value->GetType() != VTYPE_DICTIONARY) return result;
    const auto settings = value->GetDictionary();
    CefDictionaryValue::KeyList keys; settings->GetKeys(keys);
    for (const auto& key : keys) {
      const auto entry = settings->GetDictionary(key);
      if (!entry) continue;
      const int location = entry->GetType("location") == VTYPE_INT ? entry->GetInt("location") : 0;
      const auto reasons = entry->GetType("disable_reasons") == VTYPE_LIST ? entry->GetList("disable_reasons") : nullptr;
      if (!IsTrackedExtensionInstallLocation(location) || (reasons && reasons->GetSize())) continue;
      result.push_back(key.ToString());
    }
    return result;
  }
  void Baseline() { for (const auto& id : InstalledIds()) seen_.insert(id); }
  void OnPreferenceChanged(const CefString&) override {
    auto state = state_.lock();
    if (!state) if (const auto session=session_.lock()) for (const auto& window : session->Windows())
      if (!window->private_window && window->workspace && window->window) { state=window; break; }
    if (!state || !state->workspace) return;
    // Installs, removals and enable/disable change what the top bar's pins can show.
    if (state->session && state->session->profiles) {
      if (const auto profile = state->session->profiles->ForContext(content_)) profile->pins->MarkStale();
      for (const auto& window : state->session->Windows()) if (window->workspace) window->workspace->Refresh();
    }
    for (const auto& id : InstalledIds()) {
      if (!seen_.insert(id).second) continue;
      auto details = CefDictionaryValue::Create(); details->SetString("id", id);
      state->Record("extension_install_detected", details);
      if (state->session) for (const auto& window : state->session->Windows()) {
        if (window->private_window || !window->workspace || !content_->IsSharingWith(window->WebContext())) continue;
        window->last_extension_install_ms = GetTickCount64();
      }
    }
  }
 private:
  std::weak_ptr<State> state_;
  std::weak_ptr<AppSession> session_;
  CefRefPtr<CefRequestContext> content_;
  std::set<std::string> seen_;
  IMPLEMENT_REFCOUNTING(Watcher);
};
}  // namespace

CefRefPtr<CefRegistration> WatchExtensionInstalls(const std::shared_ptr<State>& state, CefRefPtr<CefRequestContext> content) {
  CefRefPtr<Watcher> watcher = new Watcher(state, content);
  watcher->Baseline();
  return content->AddPreferenceObserver(kSettings, watcher);
}
}  // namespace red
