#include "app/content_blocker_settings.h"
#include <algorithm>
#include <cctype>
#include <set>
#include <sstream>
#include <vector>
#include "app/browsing_profiles.h"
#include "app/session.h"
#include "app/state.h"
#include "include/cef_parser.h"
#include "include/cef_request_context.h"
#include "trust_policy.h"

namespace red {
namespace {
constexpr size_t kMaxUserFilters = 1024 * 1024;
constexpr size_t kMaxTrustedSites = 2000;

// uBO is registered in this context's profile (engine patch red_17): an engine
// without it, or a start with the blocker off, has no extension page to reach,
// and a hidden page for it must not be opened at all.
bool Registered(CefRefPtr<CefRequestContext> context) {
  const auto value = context ? context->GetPreference("extensions.settings") : nullptr;
  const auto settings = value && value->GetType() == VTYPE_DICTIONARY ? value->GetDictionary() : nullptr;
  return settings && settings->HasKey(kContentBlockerExtensionId);
}
std::shared_ptr<ContentBlockerBridge> BridgeFor(const std::shared_ptr<State>& state, const std::shared_ptr<BrowsingProfile>& profile) {
  if (!profile || !profile->context || !Registered(profile->context)) return nullptr;
  if (!profile->blocker) profile->blocker = std::make_shared<ContentBlockerBridge>(state, profile->context);
  return profile->blocker;
}
bool BlockerOn(const std::shared_ptr<State>& state) {
  return state && state->content_blocker && state->session && state->session->preferences &&
         state->session->preferences->Get().content_blocker;
}
// The uBO of the window's visible pages: a private window has its own, off the
// record, which forgets its changes with the window.
std::shared_ptr<BrowsingProfile> DefaultProfile(const std::shared_ptr<State>& state);
std::shared_ptr<ContentBlockerBridge> PageBridge(const std::shared_ptr<State>& state) {
  if (state->private_window) {
    // The private profile inherits the default one's extension records.
    const auto main = DefaultProfile(state);
    if (!main || !main->context || !Registered(main->context)) return nullptr;
    if (!state->private_blocker && state->web_context) state->private_blocker = std::make_shared<ContentBlockerBridge>(state, state->web_context);
    return state->private_blocker;
  }
  const auto profile = state->session && state->session->profiles ? state->session->profiles->ForContext(state->WebContext()) : nullptr;
  return profile ? BridgeFor(state, profile) : nullptr;
}
std::shared_ptr<BrowsingProfile> DefaultProfile(const std::shared_ptr<State>& state) {
  return state->session && state->session->profiles ? state->session->profiles->Get(state, "default") : nullptr;
}
CefRefPtr<CefDictionaryValue> Message(const std::string& what) {
  auto message = CefDictionaryValue::Create(); message->SetString("what", what); return message;
}
CefRefPtr<CefDictionaryValue> Dict(CefRefPtr<CefValue> value) {
  return value && value->GetType() == VTYPE_DICTIONARY ? value->GetDictionary() : nullptr;
}
double Number(CefRefPtr<CefDictionaryValue> d, const char* key) {
  if (!d) return 0;
  if (d->GetType(key) == VTYPE_INT) return d->GetInt(key);
  if (d->GetType(key) == VTYPE_DOUBLE) return d->GetDouble(key);
  return 0;
}
// Sends |messages| in order to one bridge; stops at the first error.
void Sequence(std::shared_ptr<ContentBlockerBridge> bridge, std::vector<std::pair<std::string, CefRefPtr<CefDictionaryValue>>> messages,
              std::function<void(const std::string&)> done, size_t index = 0) {
  if (index == messages.size()) { done(""); return; }
  const auto [channel, message] = messages[index];
  bridge->Send(channel, message, [bridge, messages, done, index](CefRefPtr<CefValue>, const std::string& error) {
    if (!error.empty()) { done(error); return; }
    Sequence(bridge, messages, done, index + 1);
  });
}
// Applies |messages| to every loaded profile's uBO; |done| follows the default profile.
void ApplyEverywhere(const std::shared_ptr<State>& state, std::vector<std::pair<std::string, CefRefPtr<CefDictionaryValue>>> messages,
                     std::function<void(const std::string&)> done) {
  const auto main = DefaultProfile(state);
  if (!main) { done("The ad blocker is unavailable"); return; }
  for (const auto& profile : state->session->profiles->All())
    if (profile != main) if (const auto bridge = BridgeFor(state, profile)) Sequence(bridge, messages, [](const std::string&) {});
  Sequence(BridgeFor(state, main), messages, std::move(done));
}
// The page's hostname, for http(s) pages only (GURL-canonical, lower case).
std::string HostOf(const std::string& url) {
  CefURLParts parts;
  if (url.size() > 8192 || !CefParseURL(url, parts)) return {};
  const auto scheme = CefString(&parts.scheme).ToString();
  if (scheme != "http" && scheme != "https") return {};
  return CefString(&parts.host).ToString();
}
}  // namespace


void ReadContentBlocker(const std::shared_ptr<State>& state, BlockerReply done) {
  if (!BlockerOn(state)) { done(nullptr, "The ad blocker is off"); return; }
  const auto bridge = BridgeFor(state, DefaultProfile(state));
  if (!bridge) { done(nullptr, "The ad blocker is unavailable"); return; }
  bridge->Send("dashboard", Message("getLists"), [state, bridge, done](CefRefPtr<CefValue> lists_value, const std::string& error) {
    const auto lists = Dict(lists_value);
    if (!error.empty() || !lists) { done(nullptr, error.empty() ? "Unexpected ad blocker reply" : error); return; }
    auto result = CefDictionaryValue::Create();
    auto rows = CefListValue::Create();
    const auto available = lists->GetDictionary("available");
    const auto cache = lists->GetDictionary("cache");
    const auto user_path = lists->GetString("userFiltersPath");
    CefDictionaryValue::KeyList keys; if (available) available->GetKeys(keys);
    for (const auto& key : keys) {
      const auto entry = available->GetDictionary(key);
      if (!entry || key == user_path || entry->GetString("content") != "filters") continue;
      auto row = CefDictionaryValue::Create();
      row->SetString("key", key);
      row->SetString("title", entry->GetString("title").ToString().substr(0, 160));
      row->SetString("group", entry->GetString("group").ToString().substr(0, 40));
      row->SetBool("enabled", !entry->GetBool("off"));
      row->SetDouble("rules", Number(entry, "entryCount"));
      const auto meta = cache ? cache->GetDictionary(key) : nullptr;
      row->SetDouble("updated", Number(meta, "writeTime"));
      rows->SetDictionary(rows->GetSize(), row);
    }
    result->SetList("lists", rows);
    result->SetBool("updating", lists->GetBool("isUpdating"));
    result->SetDouble("rules", Number(lists, "netFilterCount") + Number(lists, "cosmeticFilterCount"));
    result->SetBool("waitForLists", lists->GetBool("suspendUntilListsAreLoaded"));
    bridge->Send("dashboard", Message("getWhitelist"), [state, bridge, done, result](CefRefPtr<CefValue> trusted_value, const std::string& error) {
      const auto trusted = Dict(trusted_value);
      if (!error.empty() || !trusted) { done(nullptr, error.empty() ? "Unexpected ad blocker reply" : error); return; }
      auto sites = CefListValue::Create();
      if (const auto list = trusted->GetList("whitelist"))
        for (size_t i = 0; i < list->GetSize() && sites->GetSize() < kMaxTrustedSites; ++i)
          if (list->GetType(i) == VTYPE_STRING && !list->GetString(i).empty()) sites->SetString(sites->GetSize(), list->GetString(i));
      result->SetList("trusted", sites);
      bridge->Send("dashboard", Message("readUserFilters"), [done, result](CefRefPtr<CefValue> user_value, const std::string& error) {
        const auto user = Dict(user_value);
        if (!error.empty() || !user) { done(nullptr, error.empty() ? "Unexpected ad blocker reply" : error); return; }
        result->SetString("userFilters", user->GetString("content").ToString().substr(0, kMaxUserFilters));
        done(result, "");
      });
    });
  });
}

void ChangeContentBlocker(const std::shared_ptr<State>& state, CefRefPtr<CefDictionaryValue> command, BlockerReply done) {
  if (!BlockerOn(state)) { done(nullptr, "The ad blocker is off"); return; }
  const auto method = command ? command->GetString("method").ToString() : std::string();
  std::vector<std::pair<std::string, CefRefPtr<CefDictionaryValue>>> messages;
  if (method == "blocker.setList" && command->GetSize() == 3 && command->GetType("key") == VTYPE_STRING &&
      command->GetType("enabled") == VTYPE_BOOL && !command->GetString("key").empty() && command->GetString("key").length() <= 200) {
    auto apply = Message("applyFilterListSelection");
    auto keys = CefListValue::Create(); keys->SetString(0, command->GetString("key"));
    if (command->GetBool("enabled")) { apply->SetList("toSelect", keys); apply->SetBool("merge", true); }
    else {
      // uBO's own pane sends the whole selection; ask it which lists are on.
      const auto bridge = BridgeFor(state, DefaultProfile(state));
      if (!bridge) { done(nullptr, "The ad blocker is unavailable"); return; }
      const auto key = command->GetString("key").ToString();
      bridge->Send("dashboard", Message("getLists"), [state, key, done](CefRefPtr<CefValue> value, const std::string& error) {
        const auto lists = Dict(value);
        const auto available = lists ? lists->GetDictionary("available") : nullptr;
        if (!error.empty() || !available) { done(nullptr, error.empty() ? "Unexpected ad blocker reply" : error); return; }
        auto selected = CefListValue::Create();
        CefDictionaryValue::KeyList keys; available->GetKeys(keys);
        for (const auto& k : keys) if (const auto entry = available->GetDictionary(k); entry && !entry->GetBool("off") && k != key)
          selected->SetString(selected->GetSize(), k);
        auto apply = Message("applyFilterListSelection"); apply->SetList("toSelect", selected);
        ApplyEverywhere(state, {{"dashboard", apply}, {"dashboard", Message("reloadAllFilters")}}, [state, done](const std::string& error) {
          if (!error.empty()) { done(nullptr, error); return; }
          ReadContentBlocker(state, done);
        });
      });
      return;
    }
    messages = {{"dashboard", apply}, {"dashboard", Message("reloadAllFilters")}};
  } else if (method == "blocker.updateLists" && command->GetSize() == 1) {
    messages = {{"dashboard", Message("updateNow")}};
  } else if (method == "blocker.setTrusted" && command->GetSize() == 2 && command->GetType("sites") == VTYPE_LIST) {
    const auto sites = command->GetList("sites");
    if (sites->GetSize() > kMaxTrustedSites) { done(nullptr, "Too many sites"); return; }
    std::string text;
    for (size_t i = 0; i < sites->GetSize(); ++i) {
      if (sites->GetType(i) != VTYPE_STRING || !ValidTrustedSite(sites->GetString(i))) { done(nullptr, "Invalid site"); return; }
      text += sites->GetString(i).ToString() + "\n";
    }
    auto set = Message("setWhitelist"); set->SetString("whitelist", text);
    messages = {{"dashboard", set}};
  } else if (method == "blocker.setUserFilters" && command->GetSize() == 2 && command->GetType("text") == VTYPE_STRING &&
             command->GetString("text").length() <= kMaxUserFilters) {
    auto write = Message("writeUserFilters");
    write->SetString("content", command->GetString("text")); write->SetBool("enabled", true); write->SetBool("trusted", false);
    messages = {{"dashboard", write}, {"dashboard", Message("reloadAllFilters")}};
  } else if (method == "blocker.setWaitForLists" && command->GetSize() == 2 && command->GetType("value") == VTYPE_BOOL) {
    auto setting = Message("userSettings");
    setting->SetString("name", "suspendUntilListsAreLoaded"); setting->SetBool("value", command->GetBool("value"));
    messages = {{"dashboard", setting}};
  } else { done(nullptr, "Invalid ad blocker command"); return; }
  ApplyEverywhere(state, std::move(messages), [state, done](const std::string& error) {
    if (!error.empty()) { done(nullptr, error); return; }
    ReadContentBlocker(state, done);
  });
}

void PreparePrivateBlocker(const std::shared_ptr<State>& state) {
  if (!state || !state->private_window || !BlockerOn(state)) return;
  ReadContentBlocker(state, [weak = std::weak_ptr<State>(state)](CefRefPtr<CefDictionaryValue> settings, const std::string& error) {
    const auto state = weak.lock();
    if (!state || !settings || !error.empty()) return;
    const auto bridge = PageBridge(state);
    if (!bridge) return;
    auto selected = CefListValue::Create();
    if (const auto lists = settings->GetList("lists"))
      for (size_t i = 0; i < lists->GetSize(); ++i)
        if (const auto row = lists->GetDictionary(i); row && row->GetBool("enabled")) selected->SetString(selected->GetSize(), row->GetString("key"));
    auto apply = Message("applyFilterListSelection"); apply->SetList("toSelect", selected);
    std::string trusted;
    if (const auto sites = settings->GetList("trusted"))
      for (size_t i = 0; i < sites->GetSize(); ++i) trusted += sites->GetString(i).ToString() + "\n";
    auto whitelist = Message("setWhitelist"); whitelist->SetString("whitelist", trusted);
    auto user = Message("writeUserFilters");
    user->SetString("content", settings->GetString("userFilters")); user->SetBool("enabled", true); user->SetBool("trusted", false);
    auto wait = Message("userSettings");
    wait->SetString("name", "suspendUntilListsAreLoaded"); wait->SetBool("value", settings->GetBool("waitForLists"));
    Sequence(bridge, {{"dashboard", apply}, {"dashboard", whitelist}, {"dashboard", user}, {"dashboard", wait}, {"dashboard", Message("reloadAllFilters")}},
             [weak](const std::string& error) {
               if (const auto state = weak.lock(); state && state->probe) {
                 auto data = CefDictionaryValue::Create(); data->SetString("error", error); state->Record("private_blocker_prepared", data);
               }
             });
  });
}

void ReadSiteBlocking(const std::shared_ptr<State>& state, const std::string& url, BlockerReply done) {
  auto unavailable = CefDictionaryValue::Create(); unavailable->SetBool("available", false);
  const auto host = HostOf(url);
  if (!BlockerOn(state) || host.empty()) { done(unavailable, ""); return; }
  const auto bridge = PageBridge(state);
  if (!bridge) { done(unavailable, ""); return; }
  auto message = Message("getPopupData"); message->SetString("tabUrl", url);
  bridge->Send("popupPanel", message, [done, unavailable](CefRefPtr<CefValue> value, const std::string& error) {
    const auto data = Dict(value);
    if (!error.empty() || !data) { done(unavailable, ""); return; }
    auto result = CefDictionaryValue::Create();
    result->SetBool("available", true);
    result->SetBool("enabled", data->GetBool("netFilteringSwitch"));
    // uBO 1.75: pageCounts.blocked.any (what its own popup shows).
    const auto counts = data->GetDictionary("pageCounts");
    result->SetDouble("blocked", Number(counts ? counts->GetDictionary("blocked") : nullptr, "any"));
    done(result, "");
  });
}

void SetSiteBlocking(const std::shared_ptr<State>& state, const std::string& url, bool enabled, BlockerReply done) {
  const auto host = HostOf(url);
  if (!BlockerOn(state) || host.empty() || !ValidTrustedSite(host)) { done(nullptr, "The ad blocker is not available for this page"); return; }
  const auto main = DefaultProfile(state);
  if (!main) { done(nullptr, "The ad blocker is unavailable"); return; }
  // A private window changes only its own uBO; otherwise each profile's own
  // list, with this host added or removed.
  std::vector<std::pair<std::shared_ptr<ContentBlockerBridge>, bool>> targets;
  if (state->private_window) targets.push_back({PageBridge(state), true});
  else for (const auto& profile : state->session->profiles->All()) targets.push_back({BridgeFor(state, profile), profile == main});
  for (const auto& [bridge, is_main] : targets) {
    if (!bridge) { if (is_main) done(nullptr, "The ad blocker is unavailable"); continue; }
    bridge->Send("dashboard", Message("getWhitelist"), [bridge, host, enabled, is_main, done](CefRefPtr<CefValue> value, const std::string& error) {
      const auto data = Dict(value);
      if (!error.empty() || !data) { if (is_main) done(nullptr, error.empty() ? "Unexpected ad blocker reply" : error); return; }
      std::string list;
      if (const auto entries = data->GetList("whitelist"))
        for (size_t i = 0; i < entries->GetSize(); ++i) if (entries->GetType(i) == VTYPE_STRING) list += entries->GetString(i).ToString() + "\n";
      auto set = CefDictionaryValue::Create(); set->SetString("what", "setWhitelist");
      set->SetString("whitelist", WithTrustedSite(list, host, !enabled));
      bridge->Send("dashboard", set, [is_main, done](CefRefPtr<CefValue>, const std::string& error) {
        if (!is_main) return;
        if (!error.empty()) { done(nullptr, error); return; }
        auto result = CefDictionaryValue::Create(); result->SetBool("ok", true); done(result, "");
      });
    });
  }
}
}  // namespace red
