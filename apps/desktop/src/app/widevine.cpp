#include "app/widevine.h"

#include "app/state.h"
#include "include/cef_component_updater.h"
#include "include/cef_task.h"
#include "include/base/cef_callback.h"
#include "include/wrapper/cef_closure_task.h"

namespace red {
void EnsureWidevine(const std::shared_ptr<State>& state, int attempt);
namespace {
bool Installed(cef_component_state_t state) {
  return state == CEF_COMPONENT_STATE_UPDATED || state == CEF_COMPONENT_STATE_UP_TO_DATE ||
         state == CEF_COMPONENT_STATE_RUN;
}

CefRefPtr<CefDictionaryValue> Describe(CefRefPtr<CefComponent> component) {
  auto details = CefDictionaryValue::Create();
  details->SetBool("registered", component != nullptr);
  if (component) {
    details->SetString("version", component->GetVersion());
    details->SetInt("state", component->GetState());
  }
  return details;
}

class Done : public CefComponentUpdateCallback {
 public:
  explicit Done(std::weak_ptr<State> state) : state_(std::move(state)) {}
  void OnComplete(const CefString& id, cef_component_update_error_t error) override {
    auto state = state_.lock();
    if (!state) return;
    auto updater = CefComponentUpdater::GetComponentUpdater();
    auto details = Describe(updater ? updater->GetComponentByID(id) : nullptr);
    details->SetInt("error", error);
    state->Record("widevine_update_finished", details);
  }

 private:
  std::weak_ptr<State> state_;
  IMPLEMENT_REFCOUNTING(Done);
};
}  // namespace

void EnsureWidevine(const std::shared_ptr<State>& state) { EnsureWidevine(state, 0); }

void EnsureWidevine(const std::shared_ptr<State>& state, int attempt) {
  auto updater = CefComponentUpdater::GetComponentUpdater();
  auto details = CefDictionaryValue::Create();
  details->SetBool("updater", updater != nullptr);
  details->SetInt("components", updater ? static_cast<int>(updater->GetComponentCount()) : 0);
  auto component = updater ? updater->GetComponentByID(kWidevineComponentId) : nullptr;
  details->SetDictionary("widevine", Describe(component));
  details->SetInt("attempt", attempt);
  state->Record("widevine_at_start", details);
  // Chrome registers its components a little after the context is ready.
  if (!component && updater && attempt < 6) {
    CefPostDelayedTask(TID_UI, base::BindOnce([](std::weak_ptr<State> weak, int next) {
      if (auto state = weak.lock()) EnsureWidevine(state, next);
    }, std::weak_ptr<State>(state), attempt + 1), 5000);
    return;
  }
  if (!component || Installed(component->GetState())) return;
  updater->Update(kWidevineComponentId, CEF_COMPONENT_UPDATE_PRIORITY_FOREGROUND, new Done(state));
  state->Record("widevine_update_requested");
}
}  // namespace red
