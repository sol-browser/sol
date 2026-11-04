#include "experiments/private_probe.h"
#include "app/session.h"
#include "history/history_service.h"
#include "include/base/cef_callback.h"
#include "include/wrapper/cef_closure_task.h"
#include "workspace/controller.h"
namespace red {
namespace {
std::string Origin(const std::string& url) {
  const auto scheme = url.find("://");
  const auto slash = scheme == std::string::npos ? std::string::npos : url.find('/', scheme + 3);
  return slash == std::string::npos ? url : url.substr(0, slash);
}
// Records go to the probing (first) window's evidence; a private window has none of its own.
std::shared_ptr<State> Prober(const std::shared_ptr<State>& state) {
  if (state->private_probe) return state;
  if (!state->session) return nullptr;
  for (const auto& window : state->session->Windows()) if (window->private_probe) return window;
  return nullptr;
}
std::shared_ptr<State> PrivateWindow(const std::shared_ptr<State>& prober) {
  for (const auto& window : prober->session->Windows()) if (window->private_window) return window;
  return nullptr;
}
}  // namespace
void OnPrivateProbeLoad(const std::shared_ptr<State>& state, const std::string& url) {
  const auto prober = Prober(state);
  if (!prober || prober->private_done || !url.starts_with("http://localhost:")) return;
  const auto origin = Origin(url);
  if (state->private_window && url.ends_with("/private")) {
    auto details = CefDictionaryValue::Create(); details->SetBool("private_context", state->web_context != nullptr);
    details->SetBool("isolated", state->web_context && !state->web_context->IsSharingWith(CefRequestContext::GetGlobalContext()));
    prober->Record("private_loaded", details);
    // Back in the normal window: its request shows whether the private cookie leaked.
    prober->workspace->OpenLink(origin + "/check", true);
    return;
  }
  if (state != prober) return;
  if (url.ends_with("/plain") && !prober->private_started) {
    prober->private_started = true;
    CefPostDelayedTask(TID_UI, base::BindOnce([](std::weak_ptr<State> weak, std::string origin) {
      const auto state = weak.lock(); if (!state || !state->session) return;
      // Opened straight on /private, as "Open link in private window" does (its first tab).
      const bool created = state->session->new_window_with_link && state->session->new_window_with_link(state, origin + "/private", true);
      auto details = CefDictionaryValue::Create(); details->SetBool("created", created);
      state->Record("private_window_opened", details);
      CefPostDelayedTask(TID_UI, base::BindOnce([](std::weak_ptr<State> weak, std::string origin) {
        const auto state = weak.lock(); if (!state) return;
        const auto window = PrivateWindow(state);
        auto details = CefDictionaryValue::Create(); details->SetBool("found", window != nullptr);
        details->SetBool("workspace", window && window->workspace);
        state->Record("private_window_ready", details);
      }, weak, origin), 2500);
    }, std::weak_ptr<State>(state), origin), 800);
    return;
  }
  if (url.ends_with("/check")) {
    // History is written off the UI thread; ask a moment later whether /private was recorded.
    CefPostDelayedTask(TID_UI, base::BindOnce([](std::weak_ptr<State> weak) {
      const auto state = weak.lock(); if (!state || !state->history) return;
      state->history->Suggest("private", 10, [weak](std::vector<Visit> visits) {
        const auto state = weak.lock(); if (!state) return;
        bool recorded = false;
        for (const auto& visit : visits) if (visit.url.ends_with("/private")) recorded = true;
        auto details = CefDictionaryValue::Create(); details->SetBool("recorded", recorded);
        state->Record("private_history", details);
        if (const auto window = PrivateWindow(state); window && window->window) window->window->Close();
        CefPostDelayedTask(TID_UI, base::BindOnce([](std::weak_ptr<State> weak) {
          const auto state = weak.lock(); if (!state) return;
          auto details = CefDictionaryValue::Create(); details->SetBool("closed", PrivateWindow(state) == nullptr);
          state->Record("private_closed", details); state->private_done = true;
        }, weak), 2500);
      });
    }, std::weak_ptr<State>(state)), 1500);
  }
}
}  // namespace red
