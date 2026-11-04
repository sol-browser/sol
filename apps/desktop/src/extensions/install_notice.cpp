#include "extensions/install_notice.h"
#include <windows.h>
#include <set>
#include "include/cef_preference.h"
#include "trust_policy.h"
#include "workspace/controller.h"
#include "app/session.h"

namespace red {
namespace {
constexpr char kSettings[] = "extensions.settings";

class Watcher final : public CefPreferenceObserver {
 public:
  Watcher(std::weak_ptr<State> state, CefRefPtr<CefRequestContext> content) : state_(std::move(state)), content_(content) {}
  // Returns IDs of enabled, user-installed extensions with their display names.
  std::map<std::string, std::string> Installed() const {
    std::map<std::string, std::string> result;
    const auto value = content_->GetPreference(kSettings);
    if (!value || value->GetType() != VTYPE_DICTIONARY) return result;
    const auto settings = value->GetDictionary();
    CefDictionaryValue::KeyList keys; settings->GetKeys(keys);
    for (const auto& key : keys) {
      const auto entry = settings->GetDictionary(key);
      if (!entry) continue;
      const int location = entry->GetType("location") == VTYPE_INT ? entry->GetInt("location") : 0;
      const auto reasons = entry->GetType("disable_reasons") == VTYPE_LIST ? entry->GetList("disable_reasons") : nullptr;
      if (!AnnouncedInstallLocation(location) || (reasons && reasons->GetSize())) continue;
      const auto manifest = entry->GetType("manifest") == VTYPE_DICTIONARY ? entry->GetDictionary("manifest") : nullptr;
      std::string name = manifest && manifest->GetType("name") == VTYPE_STRING ? manifest->GetString("name").ToString() : "";
      // Untranslated __MSG_*__ names or oversized names fall back to a generic label.
      if (name.empty() || name.starts_with("__MSG_") || name.size() > 80) name = "An extension";
      result[key.ToString()] = name;
    }
    return result;
  }
  void Baseline() { for (const auto& [id, name] : Installed()) announced_.insert(id); }
  void OnPreferenceChanged(const CefString&) override {
    auto state = state_.lock();
    if (!state || !state->workspace) return;
    // Installs, removals and enable/disable change what the top bar's pins can show.
    if (state->session && state->session->pinned_extensions) {
      state->session->pinned_extensions->MarkStale();
      for (const auto& window : state->session->Windows()) if (window->workspace) window->workspace->Refresh();
    }
    for (const auto& [id, name] : Installed()) {
      if (!announced_.insert(id).second) continue;
      state->last_extension_install_ms = GetTickCount64();
      auto details = CefDictionaryValue::Create(); details->SetString("id", id);
      state->Record("extension_install_announced", details);
      state->workspace->Notify(name + " was added. Open it from the site controls, or pin it to the top bar from the browser menu.");
    }
  }
 private:
  std::weak_ptr<State> state_;
  CefRefPtr<CefRequestContext> content_;
  std::set<std::string> announced_;
  IMPLEMENT_REFCOUNTING(Watcher);
};
}  // namespace

void WatchExtensionInstalls(const std::shared_ptr<State>& state, CefRefPtr<CefRequestContext> content) {
  CefRefPtr<Watcher> watcher = new Watcher(state, content);
  watcher->Baseline();
  state->extension_install_watch = content->AddPreferenceObserver(kSettings, watcher);
}
}  // namespace red
