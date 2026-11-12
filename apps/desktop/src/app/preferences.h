#pragma once
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include "include/cef_thread.h"
#include "include/cef_values.h"
#include "navigation/site_search.h"
#include "integrations/providers.h"

namespace red {
struct KeyBinding {
  int key = 0;
  bool ctrl = false, shift = false, alt = false;
  bool operator==(const KeyBinding&) const = default;
};
struct Preferences {
  Preferences() = default;
  Preferences(const Preferences&) = default;
  Preferences(Preferences&&) noexcept = default;
  Preferences& operator=(const Preferences&) = default;
  Preferences& operator=(Preferences&&) noexcept = default;
  ~Preferences() = default;
  std::string search_engine = "google";
  std::string notes_provider = "googleDocs";
  IntegrationSettings integrations;
  std::filesystem::path download_directory;
  bool ask_download = false;
  bool search_suggestions = true;
  std::vector<SiteSearch> site_searches = DefaultSiteSearches();
  std::string site_search_activation = "spaceOrTab";
  int auto_archive_hours = 12;  // 0 disables it; shared by all Spaces/windows.
  bool auto_pip = false;  // Video pops out when you leave its tab (default off).
  std::map<std::string, bool> auto_pip_sites;  // Per-site override (origin -> on/off).
  bool pip_button = true;  // Picture-in-picture button over website videos.
  int unload_minutes = 60;  // Idle tabs leave memory (0: never).
  // Times on History and Downloads: "system" (the Windows short time format), "12" or "24".
  std::string clock = "system";
  std::string backdrop = "none";  // Window background: "none", "mica" or "acrylic" (Windows 11).
  std::string startup = "restore", homepage = "https://www.google.com/";
  std::map<std::string, KeyBinding> shortcuts;
};
// One per profile, shared by all window kinds. Only Settings may mutate it.
// A save is acknowledged/published after its SQLite transaction commits.
class BrowserPreferences : public std::enable_shared_from_this<BrowserPreferences> {
 public:
  using Reply = std::function<void(const std::string&)>;
  explicit BrowserPreferences(std::filesystem::path path);
  const Preferences& Get() const { return value_; }
  const std::string& error() const { return error_; }
  bool busy() const { return busy_; }
  void Save(Preferences next, Reply done);
  void Stop();
  static CefRefPtr<CefDictionaryValue> ToDictionary(const Preferences& value);
 private:
  std::filesystem::path path_;
  Preferences value_;
  std::string error_;
  CefRefPtr<CefThread> worker_;
  bool busy_ = false;
};
bool ValidSearchEngine(const std::string& engine);
CefRefPtr<CefDictionaryValue> IntegrationSettingsDictionary(const IntegrationSettings& settings);
bool ReadSiteSearches(CefRefPtr<CefListValue> list, std::vector<SiteSearch>& sites);
CefRefPtr<CefListValue> SiteSearchList(const std::vector<SiteSearch>& sites);
bool ValidAutoArchiveHours(int hours);
bool ValidUnloadMinutes(int minutes);
bool ValidHomepage(const std::string& url);
bool ValidBackdrop(const std::string& backdrop);
bool ValidClock(const std::string& clock);
// Whether Windows shows the short time with AM/PM (Region > Short time).
bool SystemTwelveHourClock();
bool TwelveHourClock(const std::string& clock);
std::string SearchEngineName(const std::string& engine);
}  // namespace red
