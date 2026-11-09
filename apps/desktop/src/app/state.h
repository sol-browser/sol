#pragma once
#include <atomic>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "app/theme.h"
#include "include/cef_browser.h"
#include "include/cef_registration.h"
#include "include/cef_request_context.h"
#include "include/cef_values.h"
#include "include/views/cef_window.h"

namespace red {
class TabProbe;
class AppSession;
struct WorkspaceData;
class EmbeddedContent;
class WorkspaceController;
class WindowClose;
class ToolbarProbe;
class CommandBar;
class SidebarMenu;
class DownloadManager;
class HistoryService;
class FaviconCache;
struct BrowsingProfile;
class PanelPopup;
class MenuPopup;
class ExtensionPopup;
class ExtensionCatalog;
class ExtensionsWindow;
class PasswordsWindow;
class SettingsWindow;
class WelcomeWindow;
class BoostWindow;
class SidebarPeek;
class DevToolsDock;
struct State {
  std::shared_ptr<AppSession> session;
  std::shared_ptr<WorkspaceData> workspace_data;
  bool standalone_window = false;
  // Private (incognito) window (docs/private-windows.md): its own off-the-record web context, an
  // in-memory sidebar, and no history, favicon, download record or session written to disk.
  bool private_window = false;
  // This launch created the profile (no Chromium data yet): Sol shows its welcome
  // window first and this window once it finishes.
  bool fresh_profile = false;
  // Windows 11 system backdrop (engine patch 12, docs/acrylic.md). |translucent| is fixed when
  // the window is created; |backdrop| ("none", "mica", "acrylic") follows Settings live.
  bool translucent = false;
  std::string backdrop = "none";
  std::string backdrop_switch;  // Probe override (--probe-backdrop=mica|acrylic).
  CefRefPtr<CefRequestContext> web_context;
  std::string active_profile;
  std::map<int,std::shared_ptr<BrowsingProfile>> browser_profiles;
  // Selected Space's profile (or the private window's OTR context). Background
  // pages keep their own binding in browser_profiles across Space switches.
  CefRefPtr<CefRequestContext> WebContext() const { return web_context ? web_context : CefRequestContext::GetGlobalContext(); }
  std::string initial_title = "New tab";
  bool initial_custom_title = false;
  std::function<void()> detached_ready;
  bool secondary_window = false;
  std::string initial_tab;
  // Set on a window opened for a tab moved live: called with the entry the
  // window would load, it moves the source's live page in (true) instead.
  std::function<bool(const std::string& node_id)> adopt_page;
  std::string initial_page_url, initial_space;
  CefRect initial_window_bounds;
  bool tab_dragging = false;
  bool ui_editing = false;
  int keyboard_target = -1;
  std::ofstream evidence;
  std::ofstream tab_trace;  // Opt-in normal-mode timing only; no URLs/titles/page contents.
  unsigned tab_trace_rows = 0;
  std::map<int, CefRefPtr<CefBrowser>> browsers;
  std::set<int> popup_browsers;
  std::map<int, int> devtools_targets;  // Frontend browser ID -> inspected page ID.
  // When a docked DevTools frontend last closed (GetTickCount64). CEF sends the
  // root window a close request when a docked frontend closes itself after its
  // renderer crashed; WindowClose ignores requests that follow it closely.
  unsigned long long docked_devtools_closed_ms = 0;
  std::shared_ptr<DevToolsDock> devtools_dock;
  std::set<std::pair<int, int>> popup_pending;
  CefRefPtr<CefBrowser> content;
  CefRefPtr<CefWindow> window;
  std::filesystem::path profile;
  std::filesystem::path evidence_directory;
  std::string initial_url;
  std::vector<std::string> external_urls;  // Validated OS launch arguments, never logged.
  std::atomic<int> shell_id{-1};
  std::atomic<int> command_id{-1};
  std::shared_ptr<CommandBar> command_bar;
  std::atomic<int> panel_id{-1};
  std::atomic<int> integration_panel_id{-1};
  std::shared_ptr<PanelPopup> panel_popup;  // Archive/Downloads popover.
  std::shared_ptr<ExtensionPopup> extension_popup;
  std::shared_ptr<SidebarPeek> sidebar_peek;  // Floating sidebar while the docked one is hidden.
  std::shared_ptr<ExtensionsWindow> extensions_window;  // chrome://extensions, on demand.
  std::map<std::string, std::shared_ptr<PasswordsWindow>> password_windows;
  std::atomic<int> settings_id{-1};
  std::shared_ptr<SettingsWindow> settings_window;
  std::atomic<int> welcome_id{-1};
  std::shared_ptr<WelcomeWindow> welcome_window;
  std::atomic<int> boost_id{-1};
  std::atomic<int> menu_id[3] = {-1, -1, -1};  // Menu levels (windows/menu_popup.cpp); read on the IO thread too.
  std::shared_ptr<MenuPopup> menu_popup;
  std::shared_ptr<BoostWindow> boost_window;  // Boost editor, on demand.
  std::shared_ptr<ExtensionCatalog> extensions;  // Web profile extensions (site popover).
  std::shared_ptr<SidebarMenu> sidebar_menu;
  bool probe = false;
  bool passwords_probe = false;
  bool probe_navigation = false;
  bool probe_tabs = false;
  bool native_tabs_reference = false;
  bool embedded_tabs_probe = false;
  bool workspace_mode = false;
  bool sidebar_visible = true;
  int sidebar_width = 256;
  Theme theme;  // Shell theme; persisted in the workspace store.
  bool theme_dark = false;  // Resolved mode (System follows Windows).
  HWND theme_window = nullptr;
  cef_color_t theme_window_background = 0;
  COLORREF theme_caption = 0, theme_caption_text = 0;
  bool theme_window_dark = false;
  std::string theme_window_backdrop;
  // The applied theme for sol://ui/theme.css (served on the IO thread):
  // hue | intensity << 9 | noise << 17 | dark << 24.
  std::atomic<std::uint32_t> theme_packed{307u | 100u << 9};
  std::atomic<std::shared_ptr<const std::string>> theme_palette_css;
  bool workspace_probe = false;
  bool workspace_checks = false;
  bool organization_probe = false, organization_checks = false, organization_armed = false;
  bool input_probe = false;
  bool browsing_probe = false, browsing_checks = false, browsing_preserved = false;
  bool downloads_probe = false, downloads_checks = false;
  bool fullscreen_probe = false, fullscreen_checks = false;
  int fullscreen_phase = 0;
  std::string permissions_probe;  // Permission type requested by the probe.
  bool permissions_observed = false, permissions_started = false;
  bool site_probe = false, site_checks = false, site_stream_ready = false;
  std::string history_probe;  // record | restart
  bool history_checks = false;
  bool tab_actions_probe = false, tab_actions_checks = false;
  int tab_actions_phase = 0;
  bool audible_probe = false, audible_checks = false;
  bool downloads_page_probe = false, downloads_page_checks = false;
  bool extension_popup_probe = false, extension_popup_done = false;
  std::string protocols_probe, protocols_start;  // Local PDF file URL; fixture start page.
  int protocols_phase = 0;
  bool protocols_done = false;
  bool close_motion_probe = false, close_motion_done = false;
  std::atomic<int> peek_id{-1};
  bool sidebar_peek_probe = false, sidebar_peek_done = false;
  bool google_integrations_probe = false, google_integrations_checks = false;
  int google_integrations_phase = 0;
  bool sidebar_perf_probe = false, sidebar_perf_started = false, sidebar_perf_done = false;
  int sidebar_perf_tabs = 80;
  bool pip_probe = false, pip_done = false;
  bool capture_mute_probe = false, capture_mute_done = false;
  bool theme_probe = false, theme_done = false;
  bool palette_probe=false,palette_checks=false;
  bool theme_preview_probe=false,theme_preview_checks=false;
  bool spaces_probe = false, spaces_done = false;
  bool link_headers_probe = false, link_headers_done = false;
  std::string import_probe;  // <source>|<path>|<types> (browser data import smoke)
  bool import_done = false;
  bool platform_codecs_probe = false;
  bool devtools_regression = false;
  bool link_headers_quick = false, link_headers_park = false, link_headers_minimize = false, link_headers_unload = false, link_headers_print = false, link_headers_menu = false, link_headers_tabmenu = false, link_headers_inspect = false, link_headers_zoom = false, link_headers_transfer = false;
  int link_headers_phase = 0, link_headers_cycles = 0;
  bool pin_probe = false, pin_started = false, pin_done = false;
  std::string pin_id;
  CefRefPtr<CefDictionaryValue> pin_result;
  bool private_probe = false, private_started = false, private_done = false;
  bool menu_probe = false, menu_probe_started = false, menu_done = false, menu_page = false, menu_edges = false;
  int menu_loads = 0;
  bool boost_probe = false, boost_done = false, boost_store = false, boost_pick = false;
  int boost_phase = 0;
  std::string boost_origin;
  CefRefPtr<CefRegistration> boost_observer;
  std::map<std::string, std::string> boost_seen;  // frame -> report line.
  std::string ua_brand_probe;  // Experiment brand name, "none" for the baseline.
  bool ua_brand_done = false;
  int ua_brand_phase = 0;
  int spaces_phase = 0;
  std::string spaces_base;
  int theme_phase = 0;
  int capture_mute_phase = 0;
  int pip_phase = 0;
  CefRefPtr<CefBrowser> pip_page;
  int sidebar_peek_phase = 0;
  int close_motion_phase = 0;
  std::vector<std::string> shell_drop_files;  // file:/// URLs of the OS drag over the shell.
  int extension_popup_phase = 0;
  int downloads_page_phase = 0;
  int audible_phase = 0;
  int history_phase = 0;
  int site_phase = 0;
  // Camera/microphone access by page browser ID (OnMediaAccessChange).
  std::map<int, std::pair<bool, bool>> media_access;
  // Website frames currently playing <video>/<audio> with sound, by page browser ID.
  std::map<int, std::set<std::string>> audible_frames;
  std::set<int> auto_pip_pages;  // Pages whose video Red put into PiP.
  // Button-over-videos script per website page (video_pip_button.cpp).
  struct PipButtonScript { CefRefPtr<CefRegistration> observer; int pending = 0; std::string identifier; };
  std::map<int, PipButtonScript> pip_buttons;
  std::shared_ptr<HistoryService> history;
  std::shared_ptr<FaviconCache> favicons;  // Site icons by host (UI and IO threads).
  // Browser IDs of internal page tabs (trusted UI profile, page-specific IPC only).
  std::set<int> internal_pages;
  CefRefPtr<CefRequestContext> ui_context;
  int organization_browser = -1, organization_unload_decisions = 0;
  std::shared_ptr<WorkspaceController> workspace;
  std::shared_ptr<DownloadManager> downloads;
  std::filesystem::path downloads_directory;  // Probe override; default is Windows Downloads.
  std::shared_ptr<WindowClose> window_close;
  bool lifecycle_probe = false, lifecycle_checks = false;
  std::string lifecycle_mode;
  std::set<int> lifecycle_armed;
  int lifecycle_inspections = 0, lifecycle_unload_decisions = 0;
  int window_close_cancelled = 0;
  // Enabled non-component extensions observed in the trusted Shell profile;
  // -1 if Chromium's extension preference could not be read.
  int shell_extensions = 0;
  bool shell_profile_failed = false;
  CefRefPtr<CefRegistration> extension_install_watch;
  unsigned long long last_extension_install_ms = 0;
  // Root windows Chrome created for foreign browsers; kept hidden until closed.
  std::set<HWND> suppressed_windows;
  CefRefPtr<CefRegistration> shell_extension_watch;
  std::shared_ptr<EmbeddedContent> embedded;
  std::shared_ptr<TabProbe> tabs;
  std::shared_ptr<ToolbarProbe> toolbar_probe;  // Experiment B (--probe-chrome-toolbar).
  int content_loads = 0;
  bool navigation_command = false;
  bool shell_ready = false;
  bool content_loaded = false;
  bool content_bridge_absent = false;
  bool shell_sandbox = false;
  bool content_sandbox = false;
  bool layout_ok = false;
  bool ipc_rejections = false;
  int exit_code = 0;
  int revision = 0;
  void Record(const std::string& event, CefRefPtr<CefDictionaryValue> details = nullptr);
};
}  // namespace red
