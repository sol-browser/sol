#pragma once
#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include "include/cef_browser.h"
#include "integrations/whatsapp_title.h"

namespace red {
struct State;
struct BrowsingProfile;

// WhatsApp's unread count for the sidebar badge without keeping WhatsApp Web loaded (350-390 MB in
// the account-test memory report, docs/memory.md). WhatsApp has no lightweight source, so every
// 5 minutes Sol loads https://web.whatsapp.com/ in a hidden, muted, off-screen browser of the
// profile, reads the unread count from its title ("(3) WhatsApp") and closes it (the user's choice,
// 2026-11-10: periodic checks, plus the real tab stays loaded for 30 minutes after use).
//
// WhatsApp Web allows one active copy per profile: a hidden copy would push an open WhatsApp tab
// into "Use here". Checks therefore run only while no WhatsApp tab of the profile is loaded, and a
// check in progress closes as soon as one loads.

class WhatsAppUnread : public std::enable_shared_from_this<WhatsAppUnread> {
 public:
  WhatsAppUnread(std::weak_ptr<State> state, std::function<void()> changed) : state_(std::move(state)), changed_(std::move(changed)) {}
  // |wanted|: the profile has a pinned WhatsApp home tab and the integration is on.
  // |tab_loaded|: a WhatsApp tab of this profile is loaded (its title decides then).
  void Update(std::shared_ptr<BrowsingProfile> profile, bool wanted, bool tab_loaded);
  std::optional<int> count() const { return wanted_ ? count_ : std::nullopt; }
  CefRefPtr<CefBrowser> worker() const { return browser_; }
  void Stop();
  // From the hidden browser's client.
  void OnTitle(int browser, const std::string& title);
  void OnClosed(int browser);
 private:
  using Clock = std::chrono::steady_clock;
  void Schedule(int delay_ms);
  void Start(int generation);
  void Tick(int generation);
  void Finish(std::optional<int> result);
  void Set(std::optional<int> value);
  std::weak_ptr<State> state_;
  std::function<void()> changed_;
  std::shared_ptr<BrowsingProfile> profile_;
  CefRefPtr<CefBrowser> browser_;
  bool closing_ = false;
  std::optional<int> count_, seen_;  // seen_: the latest count read during this check.
  Clock::time_point deadline_, settle_;
  bool wanted_ = false, tab_loaded_ = false, stopped_ = false;
  int generation_ = 0;
};
}  // namespace red
