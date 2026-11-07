#include "trust_policy.h"
#include "windows/workspace_layout.h"
#include <cstdio>

int main() {
  int failures = 0;
  auto check = [&](bool condition, const char* description) {
    if (!condition) { std::fprintf(stderr, "%s\n", description); ++failures; }
  };
  check(red::IsTrustedUi(7, 7, true, red::kUiUrl), "registered shell main frame");
  check(!red::IsTrustedUi(8, 7, true, red::kUiUrl), "content browser cannot impersonate shell");
  check(!red::IsTrustedUi(7, 7, false, red::kUiUrl), "shell subframes denied");
  check(!red::IsTrustedUi(0, 0, true, red::kUiUrl), "unregistered identity denied");
  for (auto url : {"https://example.com/", "sol://ui.evil/index.html", "sol://ui/index.html?x=1",
                   "sol://ui/index.html#x", "sol://ui/other.html", "sol://evil@ui/index.html",
                   "sol://ui:99/index.html", "about:blank"}) {
    check(!red::IsTrustedUi(7, 7, true, url), "only exact shell document authorized");
  }
  check(red::IsAllowedMethod("navigation.load"), "navigation allowed");
  const auto expanded = red::WorkspaceContentArea(1200, 820, true);
  const auto collapsed = red::WorkspaceContentArea(1200, 820, false);
  check(expanded.x == 264 && expanded.y == 31 && expanded.width == 928 && expanded.height == 781, "expanded native geometry");
  check(collapsed.x == 8 && collapsed.width == 1184 && collapsed.y == expanded.y && collapsed.height == expanded.height,
        "hidden sidebar gives page the full released width without vertical drift");
  const auto minimum = red::WorkspaceContentArea(1, 1, true);
  check(minimum.width == 1 && minimum.height == 1, "transient small bounds never produce invalid native sizes");
  check(red::WorkspaceContentArea(1200, 820, true, 336).x == 344, "resized sidebar moves the native page");
  check(red::SidebarWidth(480, 700) == 300 && red::SidebarWidth(0, 1200) == 200 &&
        red::SidebarWidth(9999, 1200) == 480, "sidebar limits reserve page space at minimum window size");
  check(!red::IsAllowedMethod("eval"), "code execution denied");
  check(red::IsLocalFileUrl("file:///C:/Users/me/a%20b.pdf"), "local file URL");
  for (auto url : {"file://server/share/a.pdf", "file:///", "file:///C:/a\\b.pdf", "file:///C:/a\nb", "https://x/"})
    check(!red::IsLocalFileUrl(url), "UNC, empty, backslash, control and non-file URLs are not local files");
  for (auto scheme : {"mailto", "tg", "zoommtg", "ms-teams", "steam", "vscode"})
    check(red::IsExternalProtocolScheme(scheme), "app schemes go to Chrome's external protocol handler");
  for (auto scheme : {"http", "https", "file", "chrome", "chrome-extension", "javascript", "data", "blob", "sol", "devtools", "", "1abc", "a b", "a:b"})
    check(!red::IsExternalProtocolScheme(scheme), "browser schemes and malformed names are never external");
  check(!red::IsAllowedMethod("files.read"), "file access denied");
  check(!red::IsAllowedMethod("Navigation.load"), "methods case sensitive");
  check(red::IsForbiddenSwitch("no-sandbox"), "sandbox escape switch denied");
  check(red::IsForbiddenSwitch("remote-debugging-port"), "debug interface denied");
  check(!red::IsAllowedBrowserSwitch("disable-features", false), "unrecognized browser switches denied");
  check(!red::IsAllowedBrowserSwitch("red-translucent-child-windows", false), "page transparency is opted into by the host, not external arguments");
  check(!red::IsAllowedBrowserSwitch("load-extension", false), "extension loading limited to probe mode");
  check(red::IsAllowedBrowserSwitch("load-extension", true), "owned extension experiment allowed");
  check(red::IsAllowedBrowserSwitch("probe-palette",true)&&!red::IsAllowedBrowserSwitch("probe-palette",false),"palette experiment requires probe mode");
  check(red::IsAllowedBrowserSwitch("probe-theme-preview",true)&&!red::IsAllowedBrowserSwitch("probe-theme-preview",false),"theme preview experiment requires probe mode");
  check(!red::IsAllowedBrowserSwitch("probe-native-tabs", false), "native reference requires probe mode");
  check(!red::IsAllowedBrowserSwitch("probe-organization", false), "organization input automation requires probe mode");
  check(!red::IsAllowedBrowserSwitch("probe-lifecycle", false), "renderer crash automation requires explicit probe mode");
  check(red::IsExtensionPageUrl("chrome-extension://mnjggcdmjocbbbhaepdhchncahnbgone/help/index.html"), "extension page accepted");
  for (auto url : {"chrome-extension://mnjggcdmjocbbbhaepdhchncahnbgone", "chrome-extension://MNJGGCDMJOCBBBHAEPDHCHNCAHNBGONE/x",
                   "chrome-extension://mnjggcdmjocbbbhaepdhchncahnbgonz/x", "chrome-extension://a@mnjggcdmjocbbbhaepdhchncahnbgone/x",
                   "chrome-extension://mnjggcdmjocbbbhaepdhchncahnbgone:1/x", "chrome-extension://mnjggcdmjocbbbhaepdhchncahnbgone/\nx",
                   "https://mnjggcdmjocbbbhaepdhchncahnbgone/x", "chrome://extensions/"}) {
    check(!red::IsExtensionPageUrl(url), "malformed or non-extension URL rejected");
  }
  using red::ForeignPage;
  for(auto url:{"chrome://password-manager/","chrome://password-manager/passwords","chrome://password-manager/settings?referrer=1"})
    check(red::ClassifyForeignPage(url)==ForeignPage::Passwords,"native password UI routes to the dedicated manager");
  for(auto url:{"chrome://password-manager.evil/","chrome://password-manager@evil/","https://password-manager/","sol://password-manager/"})
    check(!red::IsPasswordManagerUrl(url),"password manager requires the exact internal host");

  check(red::ClassifyForeignPage("") == ForeignPage::Wait && red::ClassifyForeignPage("about:blank") == ForeignPage::Wait, "initial blank waits");
  check(red::ClassifyForeignPage("chrome://newtab/") == ForeignPage::NewTab, "empty tabs.create maps to new-tab composer");
  check(red::ClassifyForeignPage("chrome://new-tab-page/") == ForeignPage::NewTab, "observed Chromium 154 new tab page maps to composer");
  check(red::ClassifyForeignPage("https://darkreader.org/help/en/") == ForeignPage::Adopt, "extension-opened website adopted");
  check(red::ClassifyForeignPage("chrome-extension://ddkjiahejlhfcafbddmgiahcphecmpfh/dashboard.html") == ForeignPage::Adopt, "options page adopted");
  for (auto url : {"chrome://settings/", "file:///C:/Windows/win.ini", "javascript:alert(1)", "data:text/html,x", "devtools://devtools/x", "sol://ui/index.html"})
    check(red::ClassifyForeignPage(url) == ForeignPage::Reject, "unsupported foreign page rejected");
  check(red::AllowedInTrustedProfile(5, true) && red::AllowedInTrustedProfile(10, true), "component extensions allowed in shell profile");
  check(red::AllowedInTrustedProfile(6, false), "disabled default external extension tolerated");
  check(!red::AllowedInTrustedProfile(8, true) && !red::AllowedInTrustedProfile(1, true) && !red::AllowedInTrustedProfile(4, true),
        "command-line, store or unpacked extensions denied in shell profile");
  check(red::AnnouncedInstallLocation(1) && red::AnnouncedInstallLocation(4), "store and chrome://extensions installs are announced");
  check(!red::AnnouncedInstallLocation(8) && !red::AnnouncedInstallLocation(5) && !red::AnnouncedInstallLocation(6),
        "command-line, component and default external extensions are not announced");
  check(red::IsCommandUi(9, 9, true, red::kCommandUrl), "exact command document accepted");
  check(!red::IsTrustedUi(9, 9, true, red::kCommandUrl), "command document cannot call full shell IPC");
  check(!red::IsCommandUi(9, 9, true, red::kUiUrl), "shell document cannot call command IPC");
  check(!red::IsCommandUi(8, 9, true, red::kCommandUrl) && !red::IsCommandUi(9, 9, false, red::kCommandUrl), "browser and main frame required");
  check(!red::IsCommandUi(9, 9, true, "sol://ui/command.html?x") && !red::IsCommandUi(-1, -1, true, red::kCommandUrl), "exact URL and created browser required");
  return failures ? 1 : 0;
}
