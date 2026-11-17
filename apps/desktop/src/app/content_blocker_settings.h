#pragma once
#include <functional>
#include <memory>
#include <string>
#include "app/trusted_sites.h"
#include "include/cef_values.h"

namespace red {
struct State;
// Sol's own ad blocker settings (Settings > Ad blocker, docs/ublock-origin.md),
// carried out by uBlock Origin through ContentBlockerBridge. Sol treats them as
// one setting: changes go to uBO in every loaded web profile, and Settings shows
// the default profile's uBO. Profiles opened later keep their own uBO state
// until the next change.
using BlockerReply = std::function<void(CefRefPtr<CefDictionaryValue> result, const std::string& error)>;

// {lists: [{key, title, group, enabled, rules, updated}], updating, rules,
//  trusted: [hostnames], userFilters, waitForLists}
void ReadContentBlocker(const std::shared_ptr<State>& state, BlockerReply done);

// |command| (validated here):
//   {method: "blocker.setList", key, enabled}
//   {method: "blocker.updateLists"}
//   {method: "blocker.setTrusted", sites: [hostname...]}
//   {method: "blocker.setUserFilters", text}
//   {method: "blocker.setWaitForLists", value}
// Replies with ReadContentBlocker's result after the default profile applied it.
void ChangeContentBlocker(const std::shared_ptr<State>& state, CefRefPtr<CefDictionaryValue> command, BlockerReply done);

// A private window runs its own uBO, which starts with uBO's defaults: give it
// the default profile's lists, sites without blocking, own rules and startup
// setting. Called when the private window opens; changes stay in that window.
void PreparePrivateBlocker(const std::shared_ptr<State>& state);

// Site panel: whether blocking is on for |url|'s site and how many requests
// uBO blocked on the page at |url| ({available, enabled, blocked}).
void ReadSiteBlocking(const std::shared_ptr<State>& state, const std::string& url, BlockerReply done);
// Turns blocking on or off for |url|'s hostname in every loaded profile.
void SetSiteBlocking(const std::shared_ptr<State>& state, const std::string& url, bool enabled, BlockerReply done);
}  // namespace red
