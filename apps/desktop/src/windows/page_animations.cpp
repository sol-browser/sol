#include "windows/page_animations.h"
#include <map>
#include <string>
#include <vector>
#include "include/cef_devtools_message_observer.h"
#include "include/cef_parser.h"
#include "include/cef_registration.h"
#include "include/cef_values.h"

namespace red {
namespace {
// A page with more animations than this keeps the rest running.
constexpr size_t kMaxHeld = 512;

struct Hold {
  CefRefPtr<CefRegistration> observer;
  std::vector<std::string> paused;
};
std::map<int, Hold>& Holds() {
  static std::map<int, Hold> holds;
  return holds;
}
void SetPaused(CefRefPtr<CefBrowser> browser, const std::string& id, bool paused) {
  auto ids = CefListValue::Create(); ids->SetString(0, id);
  auto params = CefDictionaryValue::Create();
  params->SetList("animations", ids); params->SetBool("paused", paused);
  browser->GetHost()->ExecuteDevToolsMethod(0, "Animation.setPaused", params);
}
// Pauses each animation of a held page as it starts: `animationStarted` is sent
// when an animation leaves "pending" (its first frame) and, on enable, for those
// already running. Holding them one by one leaves the document timeline, and so
// requestAnimationFrame's timestamps, on the real clock.
class HoldObserver final : public CefDevToolsMessageObserver {
 public:
  void OnDevToolsEvent(CefRefPtr<CefBrowser> browser, const CefString& method, const void* params, size_t size) override {
    if (method != "Animation.animationStarted" || !params || !size || size > 65536) return;
    const auto found = Holds().find(browser->GetIdentifier());
    if (found == Holds().end() || found->second.paused.size() >= kMaxHeld) return;
    const auto parsed = CefParseJSON(params, size, JSON_PARSER_RFC);
    const auto event = parsed && parsed->GetType() == VTYPE_DICTIONARY ? parsed->GetDictionary() : nullptr;
    const auto animation = event ? event->GetDictionary("animation") : nullptr;
    const auto id = animation ? animation->GetString("id").ToString() : std::string();
    if (id.empty() || id.size() > 32) return;
    found->second.paused.push_back(id);
    SetPaused(browser, id, true);
  }
 private:
  IMPLEMENT_REFCOUNTING(HoldObserver);
};
}  // namespace

void PausePageAnimations(CefRefPtr<CefBrowser> browser, bool paused) {
  if (!browser) return;
  const int id = browser->GetIdentifier();
  if (paused) {
    auto& hold = Holds()[id];
    if (!hold.observer) hold.observer = browser->GetHost()->AddDevToolsMessageObserver(new HoldObserver());
    // A new document: the previous one's animation ids no longer exist.
    hold.paused.clear();
    browser->GetHost()->ExecuteDevToolsMethod(0, "Animation.enable", nullptr);
    return;
  }
  if (auto hold = Holds().extract(id)) {
    // One id per call: Chromium stops a list at the first id it no longer knows
    // (an animation that was removed), leaving the rest paused.
    for (const auto& animation : hold.mapped().paused) SetPaused(browser, animation, false);
  }
  browser->GetHost()->ExecuteDevToolsMethod(0, "Animation.disable", nullptr);
}
void ForgetPageAnimations(int browser_id) { Holds().erase(browser_id); }
size_t HeldPageAnimations(int browser_id) {
  const auto found = Holds().find(browser_id);
  return found == Holds().end() ? 0 : found->second.paused.size();
}
}  // namespace red
