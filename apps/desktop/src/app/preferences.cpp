#include "app/preferences.h"
#include "app/windows_locale.h"
#include "navigation/shortcut_settings.h"
#include "translation/model_catalog.h"
#include "translation/page_translation_model.h"
#include <sqlite3.h>
#include <windows.h>
#include <algorithm>
#include <fstream>
#include <set>
#include "include/cef_app.h"
#include "include/cef_parser.h"
#include "include/base/cef_callback.h"
#include "include/wrapper/cef_closure_task.h"

namespace red {
namespace {
constexpr int kApplicationId = 0x52454450;  // REDP; never adopt another app's DB.
struct Database {
  sqlite3* db = nullptr;
  ~Database() { if (db) sqlite3_close(db); }
};
int Number(sqlite3* db, const char* sql) {
  sqlite3_stmt* statement = nullptr;
  if (sqlite3_prepare_v2(db, sql, -1, &statement, nullptr) != SQLITE_OK) return -1;
  const int result = sqlite3_step(statement) == SQLITE_ROW ? sqlite3_column_int(statement, 0) : -1;
  sqlite3_finalize(statement); return result;
}
bool Open(Database& database, const std::filesystem::path& path, bool write) {
  if (sqlite3_open_v2(CefString(path.wstring()).ToString().c_str(), &database.db,
      write ? SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE : SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) return false;
  sqlite3_busy_timeout(database.db, 250);
  const int app = Number(database.db, "PRAGMA application_id"), version = Number(database.db, "PRAGMA user_version");
  if (app == kApplicationId && version == 1) return true;
  if (!write || app != 0 || version != 0 || Number(database.db, "SELECT count(*) FROM sqlite_master WHERE type='table'") != 0) return false;
  return sqlite3_exec(database.db,
      "BEGIN IMMEDIATE; PRAGMA application_id=1380271184; PRAGMA user_version=1;"
      "CREATE TABLE preferences(id INTEGER PRIMARY KEY CHECK(id=1), value TEXT NOT NULL CHECK(length(value)<=65536)); COMMIT;",
      nullptr, nullptr, nullptr) == SQLITE_OK;
}
bool Decode(const std::string& json, Preferences& value) {
  if (json.size() > 65536) return false;
  const auto parsed = CefParseJSON(json, JSON_PARSER_RFC);
  if (!parsed || parsed->GetType() != VTYPE_DICTIONARY) return false;
  const auto data = parsed->GetDictionary();
  if (data->GetInt("schema") != 1 || data->GetType("searchEngine") != VTYPE_STRING || !ValidSearchEngine(data->GetString("searchEngine")) ||
      data->GetType("downloadDirectory") != VTYPE_STRING || data->GetString("downloadDirectory").length() > 32767 ||
      data->GetType("askDownload") != VTYPE_BOOL || data->GetType("startup") != VTYPE_STRING ||
      data->GetType("homepage") != VTYPE_STRING || data->GetString("homepage").length() > 4096 || data->GetType("shortcuts") != VTYPE_DICTIONARY) return false;
  value.search_engine = data->GetString("searchEngine"); value.download_directory = data->GetString("downloadDirectory").ToWString();
  if (data->GetType("interfaceLanguage") == VTYPE_STRING) {
    const auto language = data->GetString("interfaceLanguage").ToString();
    if (ValidInterfaceLanguage(language)) value.interface_language = language;
  }
  if (data->HasKey("integrations")) {
    if (data->GetType("integrations") != VTYPE_DICTIONARY) return false;
    const auto integrations = data->GetDictionary("integrations");
    CefDictionaryValue::KeyList keys; integrations->GetKeys(keys);
    if (keys.size() > std::size(kIntegrationSettings)) return false;
    for (const auto& key : keys) {
      const auto id = key.ToString();
      const auto* provider = FindStoredIntegrationSetting(id);
      if (!provider || integrations->GetType(key) != VTYPE_BOOL) return false;
      if (provider->available) value.integrations[id] = integrations->GetBool(key);
    }
  }
  if (data->HasKey("notesProvider")) {
    if (data->GetType("notesProvider") != VTYPE_STRING ||
        (data->GetString("notesProvider") != "googleDocs" && data->GetString("notesProvider") != "word")) return false;
    value.notes_provider = data->GetString("notesProvider");
  }
  value.ask_download = data->GetBool("askDownload"); value.startup = data->GetString("startup"); value.homepage = data->GetString("homepage");
  if ((!value.download_directory.empty() && !value.download_directory.is_absolute()) ||
      (value.startup != "restore" && value.startup != "newTab" && value.startup != "homepage")) return false;
  if (!ValidHomepage(value.homepage)) return false;
  if (data->HasKey("searchSuggestions")) {
    if (data->GetType("searchSuggestions") != VTYPE_BOOL) return false;
    value.search_suggestions = data->GetBool("searchSuggestions");
  }
  if (data->HasKey("translationTarget")) {
    if (data->GetType("translationTarget") != VTYPE_STRING) return false;
    auto target = data->GetString("translationTarget").ToString();
    if (target != "system" && !HasTranslationTarget(target)) return false;
    value.translation_target = std::move(target);
  }
  if (data->HasKey("translationOffers")) {
    if (data->GetType("translationOffers") != VTYPE_BOOL) return false;
    value.translation_offers = data->GetBool("translationOffers");
  }
  if (data->HasKey("translationKnownLanguages") &&
      !ReadTranslationKnownLanguages(data->GetList("translationKnownLanguages"), value.translation_known_languages)) return false;
  if (data->HasKey("translationExcludedSites")) {
    const auto sites = data->GetList("translationExcludedSites");
    if (!sites || sites->GetSize() > 500) return false;
    for (size_t i = 0; i < sites->GetSize(); ++i) {
      if (sites->GetType(i) != VTYPE_STRING) return false;
      const auto site = sites->GetString(i).ToString();
      if (site.size() > 300 || !ValidHomepage(site)) return false;
      value.translation_excluded_sites.insert(site);
    }
  }
  if (data->HasKey("siteSearches") && (data->GetType("siteSearches") != VTYPE_LIST ||
      !ReadSiteSearches(data->GetList("siteSearches"), value.site_searches))) return false;
  if (data->HasKey("siteSearchActivation")) {
    if (data->GetType("siteSearchActivation") != VTYPE_STRING || !ValidSiteSearchActivation(data->GetString("siteSearchActivation"))) return false;
    value.site_search_activation = data->GetString("siteSearchActivation");
  }
  if (data->HasKey("unloadMinutes")) {
    if (data->GetType("unloadMinutes") != VTYPE_INT || !ValidUnloadMinutes(data->GetInt("unloadMinutes"))) return false;
    value.unload_minutes = data->GetInt("unloadMinutes");
  }
  if (data->HasKey("preloadOnHover")) {
    if (data->GetType("preloadOnHover") != VTYPE_BOOL) return false;
    value.preload_on_hover = data->GetBool("preloadOnHover");
  }
  if (data->HasKey("contentBlocker")) {
    if (data->GetType("contentBlocker") != VTYPE_BOOL) return false;
    value.content_blocker = data->GetBool("contentBlocker");
  }
  if (data->HasKey("pipButton")) {
    if (data->GetType("pipButton") != VTYPE_BOOL) return false;
    value.pip_button = data->GetBool("pipButton");
  }
  for (const auto& [key, field] : {std::pair{"littleSolLinks", &Preferences::little_sol_links},
                                    std::pair{"littleSolClick", &Preferences::little_sol_click},
                                    std::pair{"littleSolHotkey", &Preferences::little_sol_hotkey}}) {
    if (!data->HasKey(key)) continue;
    if (data->GetType(key) != VTYPE_BOOL) return false;
    value.*field = data->GetBool(key);
  }
  if (data->HasKey("autoPipSites")) {
    if (data->GetType("autoPipSites") != VTYPE_DICTIONARY) return false;
    const auto sites = data->GetDictionary("autoPipSites"); CefDictionaryValue::KeyList origins; sites->GetKeys(origins);
    if (origins.size() > 500) return false;
    for (const auto& origin : origins) {
      if (origin.length() > 300 || sites->GetType(origin) != VTYPE_BOOL) return false;
      value.auto_pip_sites[origin.ToString()] = sites->GetBool(origin);
    }
  }
  if (data->HasKey("autoPictureInPicture")) {
    if (data->GetType("autoPictureInPicture") != VTYPE_BOOL) return false;
    value.auto_pip = data->GetBool("autoPictureInPicture");
  }
  if (data->HasKey("backdrop")) {
    if (data->GetType("backdrop") != VTYPE_STRING || !ValidBackdrop(data->GetString("backdrop"))) return false;
    value.backdrop = data->GetString("backdrop");
  }
  if (data->HasKey("clock")) {
    if (data->GetType("clock") != VTYPE_STRING || !ValidClock(data->GetString("clock"))) return false;
    value.clock = data->GetString("clock");
  }
  if (data->HasKey("autoArchiveHours")) {
    if (data->GetType("autoArchiveHours")!=VTYPE_INT || !ValidAutoArchiveHours(data->GetInt("autoArchiveHours"))) return false;
    value.auto_archive_hours=data->GetInt("autoArchiveHours");
  }
  const auto keys = data->GetDictionary("shortcuts"); CefDictionaryValue::KeyList names; keys->GetKeys(names);
  if (names.size() > 64) return false;  // MAX_SHORTCUTS in browser-contracts: every shortcut may be rebound.
  for (const auto& name : names) {
    if (keys->GetType(name) != VTYPE_DICTIONARY || name.length() > 32) return false;
    const auto binding = ReadKeyBinding(keys->GetDictionary(name));
    if (!binding) return false;
    value.shortcuts[name] = *binding;
  }
  for (const auto& [name,key] : value.shortcuts) { std::string error; if (!ConfigureShortcut(value,name,&key,error)) return false; }
  if (data->HasKey("littleSolBinding")) {
    const auto binding = ReadKeyBinding(data->GetDictionary("littleSolBinding"));
    if (!binding || !ValidLittleSolHotkey(*binding)) return false;
    value.little_sol_binding = *binding;
  }
  return true;
}
std::string Write(const std::filesystem::path& path, const std::string& json) {
  std::error_code error; std::filesystem::create_directories(path.parent_path(), error);
  Database database;
  if (error || !Open(database, path, true)) return "Could not open the settings database. Existing data was preserved.";
  sqlite3_stmt* statement = nullptr;
  if (sqlite3_exec(database.db, "PRAGMA synchronous=FULL; BEGIN IMMEDIATE", nullptr, nullptr, nullptr) != SQLITE_OK ||
      sqlite3_prepare_v2(database.db, "INSERT INTO preferences(id,value) VALUES(1,?) ON CONFLICT(id) DO UPDATE SET value=excluded.value", -1, &statement, nullptr) != SQLITE_OK)
    return "Could not save settings.";
  sqlite3_bind_text(statement, 1, json.c_str(), static_cast<int>(json.size()), SQLITE_TRANSIENT);
  const bool saved = sqlite3_step(statement) == SQLITE_DONE; sqlite3_finalize(statement);
  if (!saved || sqlite3_exec(database.db, "COMMIT", nullptr, nullptr, nullptr) != SQLITE_OK) return "Could not commit settings. Please try again.";
  return {};
}
}
CefRefPtr<CefDictionaryValue> IntegrationSettingsDictionary(const IntegrationSettings& settings) {
  auto result = CefDictionaryValue::Create();
  for (const auto& [id, enabled] : settings) result->SetBool(id, enabled);
  return result;
}
bool ValidSearchEngine(const std::string& engine) { return engine == "google" || engine == "duckduckgo" || engine == "bing" || engine == "brave"; }
std::optional<KeyBinding> ReadKeyBinding(CefRefPtr<CefDictionaryValue> binding) {
  if (!binding || binding->GetSize() != 4 || binding->GetType("key") != VTYPE_INT ||
      binding->GetType("ctrl") != VTYPE_BOOL || binding->GetType("shift") != VTYPE_BOOL ||
      binding->GetType("alt") != VTYPE_BOOL) return std::nullopt;
  const int key = binding->GetInt("key");
  if (key < 1 || key > 255) return std::nullopt;
  return KeyBinding{key, binding->GetBool("ctrl"), binding->GetBool("shift"), binding->GetBool("alt")};
}
bool ReadTranslationKnownLanguages(CefRefPtr<CefListValue> list, std::set<std::string>& languages) {
  if (!list || list->GetSize() > 100) return false;
  std::set<std::string> parsed;
  for (size_t i = 0; i < list->GetSize(); ++i) {
    if (list->GetType(i) != VTYPE_STRING) return false;
    auto code = list->GetString(i).ToString();
    if (!ValidLanguageCode(code)) return false;
    const bool added = parsed.insert(std::move(code)).second;
    if (!added) return false;
  }
  languages = std::move(parsed);
  return true;
}
bool ReadSiteSearches(CefRefPtr<CefListValue> list, std::vector<SiteSearch>& sites) {
  if (!list || list->GetSize() > 24) return false;
  std::vector<SiteSearch> result; std::set<std::string> keywords;
  for (size_t i = 0; i < list->GetSize(); ++i) {
    if (list->GetType(i) != VTYPE_DICTIONARY) return false;
    const auto row = list->GetDictionary(i);
    if (row->GetSize() != (row->HasKey("enabled") ? 4u : 3u) || row->GetType("name") != VTYPE_STRING || row->GetType("keyword") != VTYPE_STRING || row->GetType("url") != VTYPE_STRING ||
        (row->HasKey("enabled") && row->GetType("enabled") != VTYPE_BOOL)) return false;
    SiteSearch site{row->GetString("name"), row->GetString("keyword"), row->GetString("url")};
    if (row->HasKey("enabled")) site.enabled = row->GetBool("enabled");
    if (!ValidSiteSearchFields(site) || !keywords.insert(site.keyword).second) return false;
    const auto name = CefString(site.name).ToWString();
    if (std::none_of(name.begin(), name.end(), [](wchar_t c) {
      return c != L' ' && c != 0x00a0 && c != 0x1680 && !(c >= 0x2000 && c <= 0x200a) &&
          c != 0x2028 && c != 0x2029 && c != 0x202f && c != 0x205f && c != 0x3000 && c != 0xfeff;
    })) return false;
    auto address = site.url; address.replace(address.find("%s"), 2, "sol-query");
    CefURLParts parsed;
    if (!ValidHomepage(address) || !CefParseURL(address, parsed) || CefString(&parsed.host).ToString().find("..") != std::string::npos) return false;
    result.push_back(std::move(site));
  }
  sites = std::move(result); return true;
}
CefRefPtr<CefListValue> SiteSearchList(const std::vector<SiteSearch>& sites) {
  auto list = CefListValue::Create();
  for (const auto& site : sites) {
    auto row = CefDictionaryValue::Create(); row->SetString("name", site.name);
    row->SetString("keyword", site.keyword); row->SetString("url", site.url);
    row->SetBool("enabled", site.enabled);
    list->SetDictionary(list->GetSize(), row);
  }
  return list;
}
bool ValidUnloadMinutes(int minutes) { return minutes==0 || minutes==15 || minutes==30 || minutes==60 || minutes==120 || minutes==360; }
bool ValidBackdrop(const std::string& backdrop) { return backdrop == "none" || backdrop == "mica" || backdrop == "acrylic"; }
bool ValidClock(const std::string& clock) { return clock == "system" || clock == "12" || clock == "24"; }
bool SystemTwelveHourClock() {
  wchar_t format[80] = {};
  if (GetLocaleInfoEx(LOCALE_NAME_USER_DEFAULT, LOCALE_SSHORTTIME, format, 80) > 0) {
    // Format pictures: "h" is the 12-hour clock, "H" the 24-hour one; quoted text is literal.
    bool quoted = false;
    for (const wchar_t* c = format; *c; ++c) {
      if (*c == L'\'') quoted = !quoted;
      else if (!quoted && (*c == L'h' || *c == L'H')) return *c == L'h';
    }
  }
  return true;
}
bool TwelveHourClock(const std::string& clock) { return clock == "12" || (clock != "24" && SystemTwelveHourClock()); }
bool ValidAutoArchiveHours(int hours) { return hours==0 || hours==12 || hours==24 || hours==48 || hours==168; }
bool ValidHomepage(const std::string& value) {
  if (value.empty() || value.size() > 4096 || std::any_of(value.begin(),value.end(),[](unsigned char c) { return c < 32 || c == 127; })) return false;
  CefURLParts url;
  return CefParseURL(value,url) && url.host.length && !url.username.length && !url.password.length &&
      (CefString(&url.scheme) == "http" || CefString(&url.scheme) == "https");
}
std::string SearchEngineName(const std::string& engine) {
  return engine == "duckduckgo" ? "DuckDuckGo" : engine == "bing" ? "Bing" : engine == "brave" ? "Brave Search" : "Google";
}
BrowserPreferences::BrowserPreferences(std::filesystem::path path) : path_(std::move(path)) {
  std::error_code error;
  if (std::filesystem::exists(path_, error)) {
    Database database; sqlite3_stmt* statement = nullptr;
    if (!Open(database, path_, false) || sqlite3_prepare_v2(database.db, "SELECT value FROM preferences WHERE id=1", -1, &statement, nullptr) != SQLITE_OK)
      error_ = "Settings could not be read. The existing database was preserved.";
    else {
      const int status = sqlite3_step(statement);
      if (status == SQLITE_ROW) {
        const auto* text = reinterpret_cast<const char*>(sqlite3_column_text(statement, 0));
        const int bytes = sqlite3_column_bytes(statement, 0);
        Preferences loaded;
        if (!text || bytes < 1 || bytes > 65536 || !Decode(std::string(text, bytes), loaded)) error_ = "Saved settings have an unsupported format.";
        else value_ = std::move(loaded);
      } else if (status != SQLITE_DONE) error_ = "Settings could not be read.";
    }
    if (statement) sqlite3_finalize(statement);
  } else if (error) error_ = "Settings location is unavailable.";
  if (error_.empty()) WriteContentBlockerMarker(path_, value_.content_blocker);
  worker_ = CefThread::CreateThread("Browser preferences");
  if (!worker_) error_ = "Could not start the settings worker.";
}
CefRefPtr<CefDictionaryValue> BrowserPreferences::ToDictionary(const Preferences& value) {
  auto data = CefDictionaryValue::Create(); data->SetInt("schema", 1); data->SetString("searchEngine", value.search_engine);
  data->SetString("interfaceLanguage", value.interface_language);
  data->SetString("downloadDirectory", value.download_directory.wstring()); data->SetBool("askDownload", value.ask_download);
  data->SetBool("searchSuggestions", value.search_suggestions);
  data->SetString("translationTarget", value.translation_target);
  data->SetBool("translationOffers", value.translation_offers);
  auto known = CefListValue::Create();
  for (const auto& language : value.translation_known_languages) known->SetString(known->GetSize(), language);
  data->SetList("translationKnownLanguages", known);
  auto excluded = CefListValue::Create();
  for (const auto& site : value.translation_excluded_sites) excluded->SetString(excluded->GetSize(), site);
  data->SetList("translationExcludedSites", excluded);
  data->SetString("notesProvider", value.notes_provider);
  data->SetDictionary("integrations", IntegrationSettingsDictionary(value.integrations));
  data->SetList("siteSearches", SiteSearchList(value.site_searches));
  data->SetString("siteSearchActivation", value.site_search_activation);
  data->SetInt("autoArchiveHours",value.auto_archive_hours);
  data->SetBool("autoPictureInPicture", value.auto_pip);
  auto sites = CefDictionaryValue::Create();
  for (const auto& [origin, on] : value.auto_pip_sites) sites->SetBool(origin, on);
  data->SetDictionary("autoPipSites", sites);
  data->SetBool("pipButton", value.pip_button);
  data->SetBool("preloadOnHover", value.preload_on_hover);
  data->SetBool("contentBlocker", value.content_blocker);
  data->SetBool("littleSolLinks", value.little_sol_links);
  data->SetBool("littleSolClick", value.little_sol_click);
  data->SetBool("littleSolHotkey", value.little_sol_hotkey);
  data->SetInt("unloadMinutes", value.unload_minutes);
  data->SetString("backdrop", value.backdrop);
  data->SetString("clock", value.clock);
  data->SetString("startup", value.startup); data->SetString("homepage", value.homepage);
  const auto encode_binding = [](const KeyBinding& key) {
    auto binding = CefDictionaryValue::Create(); binding->SetInt("key", key.key); binding->SetBool("ctrl", key.ctrl);
    binding->SetBool("shift", key.shift); binding->SetBool("alt", key.alt);
    return binding;
  };
  auto shortcuts = CefDictionaryValue::Create();
  for (const auto& [name, key] : value.shortcuts) shortcuts->SetDictionary(name, encode_binding(key));
  if (value.little_sol_binding != kDefaultLittleSolHotkey)
    data->SetDictionary("littleSolBinding", encode_binding(value.little_sol_binding));
  data->SetDictionary("shortcuts", shortcuts); return data;
}
void BrowserPreferences::Save(Preferences next, Reply done) {
  if (!error_.empty() || !worker_) { done(error_.empty() ? "Settings are unavailable." : error_); return; }
  if (busy_) { done("Another settings change is being saved. Please try again."); return; }
  auto data = CefValue::Create(); data->SetDictionary(ToDictionary(next));
  const auto json = CefWriteJSON(data, JSON_WRITER_DEFAULT).ToString();
  if (json.size() > 65536) { done("Settings are too large to save."); return; }
  busy_ = true;
  worker_->GetTaskRunner()->PostTask(CefCreateClosureTask(base::BindOnce([](std::shared_ptr<BrowserPreferences> self, Preferences next, std::string json, Reply done) {
    auto error = Write(self->path_, json);
    CefPostTask(TID_UI, base::BindOnce([](std::shared_ptr<BrowserPreferences> self, Preferences next, std::string error, Reply done) {
      self->busy_ = false;
      if (error.empty()) { self->value_ = std::move(next); WriteContentBlockerMarker(self->path_, self->value_.content_blocker); }
      done(error);
    }, self, std::move(next), std::move(error), std::move(done)));
  }, shared_from_this(), std::move(next), json, std::move(done))));
}
std::filesystem::path ContentBlockerOffMarker(const std::filesystem::path& profile) {
  return profile / "BrowserData" / "content-blocker-off";
}
void WriteContentBlockerMarker(const std::filesystem::path& settings_database, bool enabled) {
  const auto marker = settings_database.parent_path() / "content-blocker-off";
  std::error_code error;
  if (enabled) std::filesystem::remove(marker, error);
  else if (!std::filesystem::exists(marker, error)) std::ofstream(marker, std::ios::trunc) << "off\n";
}
void BrowserPreferences::Stop() { if (worker_) { worker_->Stop(); worker_ = nullptr; } }
}  // namespace red
