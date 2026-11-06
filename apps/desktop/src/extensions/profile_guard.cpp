#include "extensions/profile_guard.h"
#include <windows.h>
#include "include/cef_app.h"
#include "include/cef_preference.h"
#include "include/base/cef_callback.h"
#include "include/base/cef_logging.h"
#include "include/wrapper/cef_closure_task.h"
#include "app/session.h"
#include "extensions/shell_reset.h"
#include "trust_policy.h"

namespace red {
namespace {
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
    if (last != "unavailable") state->Record("shell_profile_extensions", details);
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
    if (!AllowedInTrustedProfile(location, enabled)) { denied->SetString(denied->GetSize(), key); signature += key.ToString() + ","; }
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
  // MessageBox pumps messages, so preference changes can re-enter here.
  if (state->shell_profile_failed) return;
  state->shell_profile_failed = true;
  state->Record("shell_profile_violation");
  LOG(ERROR) << "Shell profile violation confirmed; closing";
  const HWND owner = state->window ? state->window->GetWindowHandle() : nullptr;
  // Recover by moving Shell aside after shutdown and opening again.
  // A repeated violation closes Sol instead of looping.
  const bool reset = !state->probe && !state->profile.empty() && ShellResetAllowed(state->profile, std::filesystem::file_time_type::clock::now());
  state->Record(reset ? "shell_profile_reset_requested" : "shell_profile_reset_refused");
  if (reset) {
    MessageBoxW(owner, L"An extension was installed into Sol's interface profile, where extensions are not allowed.\n\n"
                L"Sol will restart and remove it from there. Your tabs, history and extensions are kept.",
                L"Sol", MB_OK | MB_ICONWARNING);
    ScheduleShellReset();
  } else {
    MessageBoxW(owner, L"An extension keeps getting into Sol's interface profile, where extensions are not allowed. "
                L"Sol will close to keep the browser interface safe.", L"Sol", MB_OK | MB_ICONERROR);
  }
  state->exit_code = 18;
  // Every window: the trusted profile is shared by all of them.
  if (state->session) {
    for (const auto& window : state->session->Windows()) if (window->window) window->window->Close();
  } else if (state->window) state->window->Close();
  else CefQuitMessageLoop();
}

class Observer final : public CefPreferenceObserver {
 public:
  Observer(std::weak_ptr<State> state, CefRefPtr<CefRequestContext> shell) : state_(std::move(state)), shell_(shell) {}
  void OnPreferenceChanged(const CefString&) override {
    if (auto state = state_.lock()) Evaluate(state, "changed");
  }
  int Evaluate(const std::shared_ptr<State>& state, const char* phase) {
    const int count = Check(state, shell_, phase, last_);
    state->shell_extensions = count;
    // Probe runs load test extensions with the global --load-extension switch,
    // which Chromium applies to every profile: recorded in evidence and the
    // snapshot, never enforced. Elsewhere a violation must persist: Chromium
    // briefly lists a default external extension as enabled before applying
    // its default disable reason.
    if (count > 0 && !state->probe && !confirming_ && !state->shell_profile_failed) {
      confirming_ = true;
      CefPostDelayedTask(TID_UI, base::BindOnce([](CefRefPtr<Observer> self) {
        self->confirming_ = false;
        auto state = self->state_.lock();
        if (state && Check(state, self->shell_, "confirm", self->last_) > 0) Fail(state);
      }, CefRefPtr<Observer>(this)), 3000);
    }
    return count;
  }
 private:
  std::weak_ptr<State> state_;
  CefRefPtr<CefRequestContext> shell_;
  std::string last_;
  bool confirming_ = false;
  IMPLEMENT_REFCOUNTING(Observer);
};
}  // namespace

bool GuardShellProfile(const std::shared_ptr<State>& state, CefRefPtr<CefRequestContext> shell) {
  CefRefPtr<Observer> observer = new Observer(state, shell);
  const int count = observer->Evaluate(state, "initialized");
  state->shell_extension_watch = shell->AddPreferenceObserver(kSettings, observer);
  if (count < 0 && !state->probe) {
    // Fail closed: without the preference the invariant cannot be checked.
    MessageBoxW(nullptr, L"Sol cannot verify its protected interface profile and will not start.", L"Sol", MB_OK | MB_ICONERROR);
    state->exit_code = 18;
    return false;
  }
  return true;
}
}  // namespace red
