#include "windows/boosts.h"
#include <fstream>
#include <windows.h>
#include "app/session.h"
#include "include/base/cef_callback.h"
#include "include/cef_devtools_message_observer.h"
#include "include/cef_parser.h"
#include "include/wrapper/cef_closure_task.h"
#include "site/boost_store.h"

namespace red {
namespace {
constexpr char kWorld[] = "red-boost";
constexpr char kBinding[] = "redBoostPick";
// Remembers the registration's DevTools identifier so a renewal replaces it, and forwards
// element picks. The binding exists only in the Boost world, so a page cannot report a pick.
class Observer final : public CefDevToolsMessageObserver {
 public:
  explicit Observer(std::weak_ptr<BoostSlot> slot) : slot_(std::move(slot)) {}
  void OnDevToolsMethodResult(CefRefPtr<CefBrowser>, int id, bool success, const void* data, size_t size) override {
    std::string json; if (success && size && size <= 4096) json.assign(static_cast<const char*>(data), size);
    CefPostTask(TID_UI, base::BindOnce([](std::weak_ptr<BoostSlot> weak, int id, std::string json) {
      const auto slot = weak.lock(); if (!slot || json.empty() || slot->pending != id) return;
      const auto value = CefParseJSON(json, JSON_PARSER_RFC);
      if (value && value->GetType() == VTYPE_DICTIONARY) slot->identifier = value->GetDictionary()->GetString("identifier").ToString().substr(0, 64);
    }, slot_, id, std::move(json)));
  }
  void OnDevToolsEvent(CefRefPtr<CefBrowser>, const CefString& method, const void* data, size_t size) override {
    if (method != "Runtime.bindingCalled" || !size || size > 4096) return;
    std::string json(static_cast<const char*>(data), size);
    CefPostTask(TID_UI, base::BindOnce([](std::weak_ptr<BoostSlot> weak, std::string json) {
      const auto slot = weak.lock(); if (!slot || slot->pick.empty() || !slot->picked) return;
      const auto value = CefParseJSON(json, JSON_PARSER_RFC);
      const auto params = value && value->GetType() == VTYPE_DICTIONARY ? value->GetDictionary() : nullptr;
      if (!params || params->GetString("name") != kBinding) return;
      const auto payload = CefParseJSON(params->GetString("payload"), JSON_PARSER_RFC);
      if (!payload || payload->GetType() != VTYPE_DICTIONARY) return;
      auto pick = payload->GetDictionary();
      if (!pick->GetBool("cancel") && (pick->GetString("mode") != slot->pick || pick->GetType("selector") != VTYPE_STRING)) return;
      auto picked = slot->picked; picked(pick);
    }, slot_, std::move(json)));
  }
 private:
  std::weak_ptr<BoostSlot> slot_;
  IMPLEMENT_REFCOUNTING(Observer);
};
void Register(AppSession& session, BoostSlot& slot) {
  if (!slot.page) return;
  if (!slot.identifier.empty()) {
    auto remove = CefDictionaryValue::Create(); remove->SetString("identifier", slot.identifier);
    slot.page->GetHost()->ExecuteDevToolsMethod(0, "Page.removeScriptToEvaluateOnNewDocument", remove);
    slot.identifier.clear();
  }
  auto params = CefDictionaryValue::Create();
  // The picker part also stops a picker left running from before (empty mode).
  params->SetString("source", session.boosts->Loader() + ";" + BoostPicker(slot.pick));
  params->SetString("worldName", kWorld); params->SetBool("runImmediately", true);
  slot.pending = slot.page->GetHost()->ExecuteDevToolsMethod(0, "Page.addScriptToEvaluateOnNewDocument", params);
}
int Number(CefRefPtr<CefDictionaryValue> d, const char* name) { return d->GetType(name) == VTYPE_INT ? d->GetInt(name) : 0; }
}  // namespace

bool ReadBoost(CefRefPtr<CefDictionaryValue> d, Boost& boost) {
  if (!d || d->GetType("css") != VTYPE_STRING) return false;
  boost.enabled = d->GetType("enabled") != VTYPE_BOOL || d->GetBool("enabled");
  boost.css = d->GetString("css");
  if (const auto l = d->GetType("look") == VTYPE_DICTIONARY ? d->GetDictionary("look") : nullptr) {
    auto& look = boost.look;
    look.hue = Number(l, "hue"); look.tint = Number(l, "tint"); look.hue2 = Number(l, "hue2"); look.tint2 = Number(l, "tint2"); look.brightness = Number(l, "brightness");
    look.contrast = Number(l, "contrast"); look.saturation = Number(l, "saturation"); look.dark = Number(l, "dark");
    look.font = Number(l, "font"); look.size = Number(l, "size"); look.caps = Number(l, "caps");
  }
  if (d->GetType("zaps") == VTYPE_LIST) {
    const auto zaps = d->GetList("zaps");
    if (zaps->GetSize() > kMaxBoostZaps) return false;
    for (size_t i = 0; i < zaps->GetSize(); ++i) {
      if (zaps->GetType(i) != VTYPE_STRING) return false;
      boost.zaps.push_back(zaps->GetString(i));
    }
  } else if (d->HasKey("zaps")) return false;
  if (d->GetType("focus") == VTYPE_STRING) boost.focus = d->GetString("focus");
  else if (d->HasKey("focus")) return false;
  return true;
}

CefRefPtr<CefDictionaryValue> WriteBoost(const Boost& boost) {
  auto d = CefDictionaryValue::Create();
  d->SetBool("enabled", boost.enabled); d->SetString("css", boost.css);
  const auto& l = boost.look;
  auto look = CefDictionaryValue::Create();
  look->SetInt("hue", l.hue); look->SetInt("tint", l.tint); look->SetInt("hue2", l.hue2); look->SetInt("tint2", l.tint2); look->SetInt("brightness", l.brightness); look->SetInt("contrast", l.contrast);
  look->SetInt("saturation", l.saturation); look->SetInt("dark", l.dark); look->SetInt("font", l.font); look->SetInt("size", l.size);
  look->SetInt("caps", l.caps);
  d->SetDictionary("look", look);
  auto zaps = CefListValue::Create();
  for (size_t i = 0; i < boost.zaps.size(); ++i) zaps->SetString(i, boost.zaps[i]);
  d->SetList("zaps", zaps); d->SetString("focus", boost.focus);
  return d;
}

bool LoadBoosts(AppSession& session, const std::filesystem::path& file) {
  if (session.boosts) return true;
  session.boosts = std::make_shared<BoostStore>(); session.boosts_path = file;
  std::ifstream in(file, std::ios::binary);
  if (!in) return true;  // First run.
  std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  if (text.size() > 16 * 1024 * 1024) return false;
  const auto root = CefParseJSON(text, JSON_PARSER_RFC);
  const auto items = root && root->GetType() == VTYPE_DICTIONARY ? root->GetDictionary()->GetDictionary("boosts") : nullptr;
  if (!items) return false;
  CefDictionaryValue::KeyList keys; items->GetKeys(keys);
  std::string error;
  BoostStore checked;
  for (const auto& key : keys) {
    const auto item = items->GetType(key) == VTYPE_DICTIONARY ? items->GetDictionary(key) : nullptr;
    Boost boost;
    if (!ReadBoost(item, boost)) continue;
    if (checked.Set(key, std::move(boost), error) != 0) continue;  // A damaged entry is dropped, the rest kept.
  }
  session.boosts->Replace(checked.All());
  return true;
}

std::string SaveBoosts(const AppSession& session) {
  if (!session.boosts || session.boosts_path.empty()) return "Boosts are not available";
  auto items = CefDictionaryValue::Create();
  for (const auto& [key, boost] : session.boosts->All()) items->SetDictionary(key, WriteBoost(boost));
  auto root = CefDictionaryValue::Create(); root->SetInt("version", 1); root->SetDictionary("boosts", items);
  auto value = CefValue::Create(); value->SetDictionary(root);
  const std::string text = CefWriteJSON(value, JSON_WRITER_DEFAULT).ToString();
  std::error_code ignored;
  std::filesystem::create_directories(session.boosts_path.parent_path(), ignored);
  const auto temporary = std::filesystem::path(session.boosts_path).concat(L".tmp");
  { std::ofstream out(temporary, std::ios::binary | std::ios::trunc); out << text; if (!out) return "Could not save the Boost"; }
  if (!MoveFileExW(temporary.c_str(), session.boosts_path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) return "Could not save the Boost";
  return "";
}

void InstallBoosts(const std::shared_ptr<State>& state, CefRefPtr<CefBrowser> page) {
  const auto session = state->session;
  if (!session || !session->boosts) return;
  auto& slot = session->boost_slots[page->GetIdentifier()];
  if (!slot) {
    slot = std::make_shared<BoostSlot>(); slot->page = page;
    slot->observer = page->GetHost()->AddDevToolsMessageObserver(new Observer(slot));
    // Without Page.enable the script is accepted but runs only through runImmediately (after the load):
    // measured with scripts/boost_store_experiment.py, the page showed unstyled first.
    page->GetHost()->ExecuteDevToolsMethod(0, "Page.enable", nullptr);
  } else slot->page = page;
  // Nothing to apply and nothing installed before: leave the page alone.
  if (session->boosts->All().empty() && slot->identifier.empty() && slot->pending == 0 && slot->pick.empty()) return;
  Register(*session, *slot);
}

void RefreshBoosts(AppSession& session) {
  if (!session.boosts) return;
  for (auto& [id, slot] : session.boost_slots) Register(session, *slot);
}

void ForgetBoostPage(AppSession& session, int browser_id) { session.boost_slots.erase(browser_id); }

bool StartBoostPick(AppSession& session, int browser_id, const std::string& mode,
                    std::function<void(CefRefPtr<CefDictionaryValue>)> picked) {
  const auto found = session.boost_slots.find(browser_id);
  if (!session.boosts || found == session.boost_slots.end() || !found->second->page || (mode != "zap" && mode != "focus")) return false;
  auto& slot = *found->second;
  if (!slot.binding) {
    // Runtime events carry the binding calls; the binding is limited to the Boost world by name.
    auto binding = CefDictionaryValue::Create(); binding->SetString("name", kBinding); binding->SetString("executionContextName", kWorld);
    slot.page->GetHost()->ExecuteDevToolsMethod(0, "Runtime.addBinding", binding);
    slot.binding = true;
  }
  slot.page->GetHost()->ExecuteDevToolsMethod(0, "Runtime.enable", nullptr);
  slot.pick = mode; slot.picked = std::move(picked);
  Register(session, slot);
  return true;
}

void StopBoostPick(AppSession& session, int browser_id) {
  const auto found = session.boost_slots.find(browser_id);
  if (found == session.boost_slots.end() || found->second->pick.empty()) return;
  auto& slot = *found->second;
  slot.pick.clear(); slot.picked = nullptr; slot.binding = false;  // Runtime.disable may drop the binding.
  Register(session, slot);
  if (slot.page) slot.page->GetHost()->ExecuteDevToolsMethod(0, "Runtime.disable", nullptr);
}
}  // namespace red
