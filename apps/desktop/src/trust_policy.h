#pragma once
#include <algorithm>
#include <cctype>
#include <string_view>

namespace red {
inline constexpr std::string_view kUiUrl = "sol://ui/index.html";
inline constexpr std::string_view kCommandUrl = "sol://ui/command.html";
inline constexpr std::string_view kPanelUrl = "sol://ui/panel.html";
inline constexpr std::string_view kSettingsUrl = "sol://ui/settings.html";
// The first-run welcome (windows/welcome_window.cpp).
inline constexpr std::string_view kWelcomeUrl = "sol://ui/welcome.html";
inline bool IsSettingsUi(int browser, int trusted, bool main_frame, std::string_view url) {
  return trusted > 0 && browser == trusted && main_frame && url == kSettingsUrl;
}
// Boost editor window: edits the Boost of the host it was opened for.
inline constexpr std::string_view kBoostUrl = "sol://ui/boost.html";
inline bool IsBoostUi(int browser, int trusted, bool main_frame, std::string_view url) {
  return trusted > 0 && browser == trusted && main_frame && url == kBoostUrl;
}
// Red's own menus (windows/menu_popup.cpp), one document per menu level.
inline constexpr std::string_view kMenuUrl = "sol://ui/menu.html";
inline bool IsMenuUi(int browser, int trusted, bool main_frame, std::string_view url) {
  return trusted > 0 && browser == trusted && main_frame && url == kMenuUrl;
}
// Floating sidebar shown from the left edge while the docked one is hidden.
inline constexpr std::string_view kPeekUrl = "sol://ui/peek.html";
inline bool IsPeekUi(int browser, int trusted, bool main_frame, std::string_view url) {
  return trusted > 0 && browser == trusted && main_frame && url == kPeekUrl;
}
// Internal pages (history, downloads): workspace tabs created in the trusted
// UI profile, each limited to its own IPC commands.
inline constexpr std::string_view kHistoryUrl = "sol://ui/history.html";
inline constexpr std::string_view kDownloadsUrl = "sol://ui/downloads.html";
inline bool IsInternalPageUrl(std::string_view url) { return url == kHistoryUrl || url == kDownloadsUrl; }
// The short addresses the address bar shows and accepts for them.
inline std::string_view InternalPageForAddress(std::string_view address) {
  return address == "sol://history" ? kHistoryUrl : address == "sol://downloads" ? kDownloadsUrl : std::string_view{};
}
// What Copy link puts on the clipboard: the short address for those pages.
inline std::string_view ShownAddress(std::string_view url) {
  return url == kHistoryUrl ? "sol://history" : url == kDownloadsUrl ? "sol://downloads" : url;
}
inline bool IsInternalPageUi(bool registered_page, bool main_frame, std::string_view url) {
  return registered_page && main_frame && IsInternalPageUrl(url);
}
inline bool IsPanelUi(int browser, int trusted, bool main_frame, std::string_view url) {
  return trusted > 0 && browser == trusted && main_frame && url == kPanelUrl;
}
inline bool IsCommandUi(int browser, int trusted, bool main_frame, std::string_view url) {
  return trusted > 0 && browser == trusted && main_frame && url == kCommandUrl;
}
// chrome-extension://<32 chars a-p>/path. The fixed-length ID followed by '/'
// leaves no room for credentials or a port.
inline bool IsExtensionPageUrl(std::string_view url) {
  constexpr std::string_view prefix = "chrome-extension://";
  if (!url.starts_with(prefix) || url.size() < prefix.size() + 33 || url.size() > 4096 || url[prefix.size() + 32] != '/') return false;
  const auto id = url.substr(prefix.size(), 32);
  return std::all_of(id.begin(), id.end(), [](char c) { return c >= 'a' && c <= 'p'; }) &&
      std::none_of(url.begin(), url.end(), [](unsigned char c) { return c < 32 || c == 127; });
}
// A local file the person opened (typed path or a file dropped on a page):
// file:/// only, no UNC host (file://server/ would send Windows credentials).
// Websites cannot navigate here: Chrome blocks web-to-file navigation, and our
// link/popup routing (OpenLink) never accepts it.
inline bool IsLocalFileUrl(std::string_view url) {
  return url.starts_with("file:///") && url.size() > 8 && url.size() <= 4096 &&
      std::none_of(url.begin(), url.end(), [](unsigned char c) { return c < 32 || c == 127 || c == '\\'; });
}
// Schemes the browser itself handles. Anything else is an external protocol
// (mailto:, tg:, zoommtg:, ...) that Chrome may hand to a Windows app after its
// own checks (denied schemes such as shell:/vbscript:, gesture, prompt).
inline bool IsBrowserScheme(std::string_view scheme) {
  for (auto known : {"http", "https", "file", "about", "data", "blob", "javascript", "chrome", "chrome-extension",
                     "chrome-untrusted", "chrome-search", "chrome-error", "devtools", "view-source", "filesystem",
                     "ws", "wss", "ftp", "sol", "isolated-app"})
    if (scheme == known) return true;
  return false;
}
inline bool IsExternalProtocolScheme(std::string_view scheme) {
  return !scheme.empty() && scheme.size() <= 64 && !IsBrowserScheme(scheme) &&
      std::isalpha(static_cast<unsigned char>(scheme[0])) &&
      std::all_of(scheme.begin(), scheme.end(), [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '+' || c == '-' || c == '.'; });
}
// A browser Chrome created on an extension's behalf (tabs.create, onboarding,
// options page). The host never adopts it in place; it reopens the address as
// a workspace page or rejects it visibly.
inline bool IsPasswordManagerUrl(std::string_view url) {
  return url == "chrome://password-manager" || url.starts_with("chrome://password-manager/");
}
enum class ForeignPage { Wait, Adopt, NewTab, Passwords, Reject };
inline ForeignPage ClassifyForeignPage(std::string_view url) {
  if (url.empty() || url == "about:blank") return ForeignPage::Wait;
  // Chromium 154 loads its new tab page as chrome://new-tab-page/.
  if (url.starts_with("chrome://newtab") || url.starts_with("chrome://new-tab-page")) return ForeignPage::NewTab;
  if (IsPasswordManagerUrl(url)) return ForeignPage::Passwords;
  if (IsExtensionPageUrl(url) || url.starts_with("https://") || url.starts_with("http://")) return ForeignPage::Adopt;
  return ForeignPage::Reject;
}
// Installs the user made: kInternal = 1 (Web Store/CRX), kUnpacked = 4
// (chrome://extensions). Command-line test loads (8) and policy or default
// external installs are not announced as "added".
inline bool AnnouncedInstallLocation(int location) { return location == 1 || location == 4; }
// Chromium ManifestLocation values: kComponent = 5, kExternalComponent = 10.
// The trusted UI profile may hold only built-in or disabled extensions.
inline bool AllowedInTrustedProfile(int location, bool enabled) {
  return location == 5 || location == 10 || !enabled;
}
inline bool IsTrustedUi(int browser, int trusted_browser, bool main_frame,
                        std::string_view url) {
  return trusted_browser > 0 && browser == trusted_browser && main_frame && url == kUiUrl;
}
inline bool IsAllowedMethod(std::string_view method) {
  return method == "browser.getSnapshot" || method == "navigation.load" ||
         method == "navigation.back" || method == "navigation.forward" ||
         method == "navigation.reload" || method == "navigation.stop";
}
inline bool IsTreeMethod(std::string_view method) {
  if (method=="linkPeek.control" || method=="media.control") return true;
  if (method == "split.create" || method == "split.remove" || method == "split.rename" || method == "split.layout" || method == "split.swap" ||
      method == "split.focus" || method == "split.expand" || method == "split.preview" || method == "split.beginResize" || method == "split.navigate" || method == "split.site") return true;
  return method == "tree.createFolder" || method == "tree.createTab" || method == "tree.rename" ||
      method == "tree.move" || method == "tree.pin" || method == "tree.expand" || method == "tree.activate" ||
      method == "tree.place" || method == "tree.close" || method == "tree.archive" || method == "tree.restore" || method == "tree.reset" || method == "tree.removeFolder" || method == "tree.usePageTitle" ||
      method == "tree.moveToSpace" || method == "space.create" || method == "space.edit" || method == "space.delete" ||
      method == "profile.create" || method == "profile.rename" || method == "space.profile" || method == "space.export" ||
      method == "space.switch" || method == "space.move";
}
inline bool IsForbiddenSwitch(std::string_view name) {
  return name == "no-sandbox" || name == "disable-web-security" ||
         name == "disable-site-isolation-trials" || name == "disable-gpu-sandbox" ||
         name == "single-process" || name == "remote-debugging-port" ||
         name == "remote-debugging-pipe" || name == "ignore-certificate-errors";
}
inline bool IsAllowedBrowserSwitch(std::string_view name, bool probe) {
  if(name=="probe-palette")return probe;
  if(name=="probe-theme-preview")return probe;
  if(name=="probe-google-integrations")return probe;
  if(name=="integration-diagnostics")return !probe;
  return name == "profile-dir" || name == "workspace" || name == "trace-tab-switches" || name == "make-default-browser" || name == "auto-archive-test-clock" || (probe && (name == "probe-report" ||
      name == "probe-url" || name == "probe-navigation" || name == "probe-browsing" || name == "probe-lifecycle" || name == "probe-tabs" || name == "probe-window-transfer" ||
      name == "probe-native-tabs" || name == "probe-embedded-tabs" || name == "probe-workspace" || name == "probe-organization" || name == "probe-input" || name == "probe-chrome-toolbar" || name == "probe-downloads-dir" || name == "probe-downloads" || name == "probe-fullscreen" || name == "probe-permissions" || name == "probe-site-settings" || name == "probe-history" || name == "probe-tab-actions" || name == "probe-audible" || name == "probe-downloads-page" || name == "probe-extension-popup" || name == "probe-protocols" || name == "probe-close-motion" || name == "probe-sidebar-peek" || name == "probe-sidebar-perf" || name == "probe-pip" || name == "probe-capture-mute" || name == "probe-theme" || name == "probe-spaces" || name == "probe-ua-brand" || name == "probe-boost" || name == "probe-menu" || name == "probe-private" || name == "probe-pin" || name == "probe-backdrop" || name == "probe-link-headers" || name == "probe-unload" || name == "probe-import" || name == "load-extension"));
}
}  // namespace red
