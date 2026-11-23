#pragma once

#include <windows.h>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include "include/cef_values.h"

namespace red {
// One protected service. Sol sends the definition to the helper with each
// tab, so adding a service needs no new helper package or VMP signature.
// Host lists are space-separated canonical hosts.
struct ProtectedMediaService {
  const char* id;
  const char* title;
  const char* player_host;   // Where a bare domain and new tabs go; the favicon key.
  const char* other_hosts;   // More pages that open in the helper.
  const char* sign_in_hosts; // Reachable inside the helper, never routed there.
  const char* site;          // Registrable domain whose pages skip Link Peek ("" for none).
  const char* alias;         // A bare domain that redirects to player_host ("" for none).
  const char* icon_host;
  const char* icon_path;     // Favicons are accepted only below this path on icon_host.
  const char* media;         // Its media elements: "audio", "video" or "audio, video".
  bool seek;                 // Setting currentTime works (Netflix rejects it).
  const char* controls;      // Spotify's transport test IDs, "key=id key=id" ("" for none).
};
// A service's player page (https on its player host, or a bare domain that
// redirects there). Only these open in the helper.
const ProtectedMediaService* ProtectedMediaForUrl(const std::string& url);
bool IsProtectedMediaPlayerUrl(const std::string& url);
// |url| on the service's player host (netflix.com → www.netflix.com), else |url|.
std::string ProtectedMediaPlayerUrl(const std::string& url);
// Any https page of a protected service's site (*.netflix.com, its player and
// sign-in hosts), which Link Peek leaves to a tab.
bool IsProtectedMediaSiteUrl(const std::string& url);

// One helper process per Sol session. Chromium assigns a unique browser ID
// to each mount, and the helper uses the Sol profile ID only for its partition.
class ProtectedMediaHost : public std::enable_shared_from_this<ProtectedMediaHost> {
 public:
  struct Tab {
    HWND parent = nullptr, child = nullptr;
    const ProtectedMediaService* service = nullptr;
    std::string profile, initial_url, url, favicon;
    bool loading = true, failed = false, committed = false, back = false, forward = false;
    bool audible = false, muted = false;
    int corner_radius = 6, applied_diameter = -1;
    SIZE shaped_size{};
    CefRefPtr<CefDictionaryValue> media;
    std::function<void()> changed;
    std::function<void(const std::string&)> shortcut;
    std::function<void(const std::string&)> external;  // Opens a link off the service as an ordinary tab.
    ULONGLONG last_external = 0;
  };

  // |base|: the folder of helper versions (integrations/protected_media_layout.h).
  ProtectedMediaHost(std::filesystem::path base, std::filesystem::path storage);
  ~ProtectedMediaHost();
  bool Available() const;
  std::filesystem::path Runtime() const;
  const std::filesystem::path& Storage() const { return storage_; }
  // Ends the helper (closing its pipe makes it exit) and marks open protected
  // tabs failed. Returns a handle the caller waits on and closes, or nullptr.
  HANDLE Shutdown();
  // The helper surface (or its child) under |point|, for wheel forwarding.
  HWND SurfaceAt(POINT point) const;
  // Reopens protected tabs that failed because no helper was installed (after an install).
  void RetryFailed();
  void Open(int id, HWND parent, std::string profile, std::string url, std::function<void()> changed,
            std::function<void(const std::string&)> shortcut, std::function<void(const std::string&)> external);
  const Tab* Get(int id) const;
  bool Control(int id, const std::string& action, double value = 0);
  void SetCornerRadius(int id, int radius);
  void Close(int id);
  // Sol's current bindings (Settings → Shortcuts), as (action name, Windows key
  // code and modifiers). The helper takes only the window-level actions among
  // them from the page, so a changed shortcut needs no new helper package.
  struct Key { std::string action; int key = 0; bool ctrl = false, shift = false, alt = false; };
  void SetShortcuts(std::vector<Key> keys);

 private:
  bool Start();
  void Reset();
  void Fail();
  void Poll();
  void Message(const std::string& line);
  bool Queue(CefRefPtr<CefDictionaryValue> message);
  void OpenRemote(int id, const Tab& tab);
  void SendShortcuts();
  std::filesystem::path base_, storage_;
  std::map<int, Tab> tabs_;
  std::vector<Key> shortcuts_;
  HANDLE pipe_ = INVALID_HANDLE_VALUE, process_ = nullptr;
  DWORD pid_ = 0;
  bool connected_ = false;
  std::string input_, output_;
  ULONGLONG connecting_since_ = 0;
};
}  // namespace red
