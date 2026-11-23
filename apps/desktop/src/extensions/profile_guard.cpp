#include "extensions/profile_guard.h"
#include <fstream>
#include "include/cef_app.h"
#include "include/cef_parser.h"
#include "include/cef_preference.h"
#include "include/base/cef_logging.h"
#include "app/session.h"
#include "extensions/shell_reset.h"
#include "sync/sync_platform.h"
#include "trust_policy.h"

namespace red {
namespace {
namespace fs = std::filesystem;
constexpr char kDisableExtensions[] = "extensions.disabled";
constexpr char kSettings[] = "extensions.settings";

// Returns -1 when the preference is unavailable, so absence is never read as
// "no extensions". Records only when the outcome differs from |last|: the
// preference changes often while extensions start.
int Check(const std::shared_ptr<State>& state, CefRefPtr<CefRequestContext> shell, const char* phase, std::string& last) {
  const auto value = shell->GetPreference(kSettings);
  auto details = CefDictionaryValue::Create();
  details->SetString("phase", phase);
  if (!value || value->GetType() != VTYPE_DICTIONARY) {
    details->SetBool("available", false);
    if (last != "unavailable") {
      state->Record("shell_profile_extensions", details);
      LOG(ERROR) << "Shell extension settings unavailable (" << phase << ")";
    }
    last = "unavailable";
    return -1;
  }
  const auto settings = value->GetDictionary();
  CefDictionaryValue::KeyList keys; settings->GetKeys(keys);
  auto denied = CefListValue::Create();
  std::string signature = std::to_string(keys.size()) + ":", summary;
  for (const auto& key : keys) {
    const auto entry = settings->GetDictionary(key);
    if (!entry) continue;
    const int location = entry->GetType("location") == VTYPE_INT ? entry->GetInt("location") : 0;
    const auto reasons = entry->GetType("disable_reasons") == VTYPE_LIST ? entry->GetList("disable_reasons") : nullptr;
    const bool enabled = !(reasons && reasons->GetSize()) && !(entry->GetType("state") == VTYPE_INT && entry->GetInt("state") == 0);
    if (location != 5 && location != 10) summary += " " + key.ToString().substr(0, 6) + ":" + std::to_string(location) + ":" + (enabled ? "on" : "off");
    if (!AllowedInTrustedProfile(key.ToString(), location, enabled)) { denied->SetString(denied->GetSize(), key); signature += key.ToString() + ","; }
  }
  // SetList transfers ownership and invalidates |denied|: count first.
  const int count = static_cast<int>(denied->GetSize());
  details->SetBool("available", true);
  details->SetInt("installed", static_cast<int>(keys.size()));
  details->SetList("denied", denied);
  if (signature != last) {
    state->Record("shell_profile_extensions", details);
    // Also in normal mode: short IDs, locations and state; no paths or manifests.
    LOG(WARNING) << "Shell profile extension check (" << phase << "): " << count << " denied of " << keys.size() << ";" << summary;
  }
  last = signature;
  return count;
}

void Fail(const std::shared_ptr<State>& state) {
  if (state->shell_profile_failed) return;
  state->shell_profile_failed = true;
  state->exit_code = 18;
  state->Record("shell_profile_violation");
  const bool restart = !state->probe && ScheduleShellReset(state->profile);
  LOG(ERROR) << "Shell profile verification failed; "
             << (restart ? "reserved one recovery restart" : "recovery restart unavailable");
  // Stop the background host from holding this instance open or starting the tray.
  if (state->session) state->session->quitting = true;
  bool closing = false;
  if (state->session) {
    for (const auto& window : state->session->Windows()) {
      if (!window->window) continue;
      closing = true;
      window->window->Close();
    }
  } else if (state->window) {
    closing = true;
    state->window->Close();
  }
  if (!closing) CefQuitMessageLoop();
}

class Observer final : public CefPreferenceObserver {
 public:
  Observer(std::weak_ptr<State> state, CefRefPtr<CefRequestContext> shell) : state_(std::move(state)), shell_(shell) {}
  void OnPreferenceChanged(const CefString&) override {
    if (auto state = state_.lock(); state && !Healthy(state, "changed")) Fail(state);
  }
  bool Healthy(const std::shared_ptr<State>& state, const char* phase) {
    const int count = Check(state, shell_, phase, last_);
    state->shell_extensions = count;
    const auto disabled = shell_->GetPreference(kDisableExtensions);
    if (disabled && disabled->GetType() == VTYPE_BOOL && disabled->GetBool()) return count == 0;
    LOG(ERROR) << "Shell extension loading was not disabled (" << phase << ")";
    return false;
  }
 private:
  std::weak_ptr<State> state_;
  CefRefPtr<CefRequestContext> shell_;
  std::string last_;
  IMPLEMENT_REFCOUNTING(Observer);
};
}  // namespace

bool ShellProfileHasProtection(const fs::path& profile) {
  const auto file = profile / L"Shell" / L"Preferences";
  std::error_code error;
  const auto size = fs::file_size(file, error);
  if (error || size > 4 * 1024 * 1024) return false;
  std::ifstream input(file, std::ios::binary);
  std::string json(static_cast<size_t>(size), '\0');
  if (!input.read(json.data(), static_cast<std::streamsize>(size))) return false;
  const auto value = CefParseJSON(json, JSON_PARSER_RFC);
  if (!value || value->GetType() != VTYPE_DICTIONARY) return false;
  const auto extensions = value->GetDictionary()->GetDictionary("extensions");
  return extensions && extensions->GetType("disabled") == VTYPE_BOOL && extensions->GetBool("disabled");
}

bool PrepareShellProfile(const fs::path& profile) {
  if (!ShellResetPending(profile) && ShellProfileHasProtection(profile)) return true;
  const auto quarantine = QuarantineShellProfile(profile);
  if (quarantine == ShellQuarantine::Failed) return false;
  if (quarantine == ShellQuarantine::Moved)
    LOG(WARNING) << "Archived Shell before starting the protected interface";
  const auto directory = profile / L"Shell";
  std::error_code error;
  fs::create_directories(directory, error);
  // Chromium reads this at profile creation, before extension loading begins.
  // Setting it via CefRequestContext after initialization would be too late.
  return !error && sync::platform::AtomicWrite(directory / L"Preferences", R"({"extensions":{"disabled":true}})");
}

bool GuardShellProfile(const std::shared_ptr<State>& state, CefRefPtr<CefRequestContext> shell) {
  CefRefPtr<Observer> observer = new Observer(state, shell);
  std::error_code error;
  if (shell->IsSharingWith(CefRequestContext::GetGlobalContext()) ||
      !fs::equivalent(fs::path(shell->GetCachePath().ToWString()), state->profile / L"Shell", error)) {
    LOG(ERROR) << "CEF did not open the dedicated Shell profile";
  } else if (observer->Healthy(state, "initialized")) {
    state->shell_extension_watch = shell->AddPreferenceObserver(kSettings, observer);
    if (state->shell_extension_watch) return true;
    LOG(ERROR) << "Could not observe Shell extension settings";
  }
  Fail(state);
  return false;
}
}  // namespace red
