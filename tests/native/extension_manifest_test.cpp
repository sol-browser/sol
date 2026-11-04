#include "extensions/extension_manifest.h"
#include <cstdio>

int main() {
  int failures = 0;
  auto check = [&](bool condition, const char* description) {
    if (!condition) { std::fprintf(stderr, "%s\n", description); ++failures; }
  };
  check(red::IsExtensionId("eimadpbcbfnmbkopoojfekhnkhdbieeh"), "store id accepted");
  check(!red::IsExtensionId("eimadpbcbfnmbkopoojfekhnkhdbiee"), "short id rejected");
  check(!red::IsExtensionId("eimadpbcbfnmbkopoojfekhnkhdbiezh"), "letters outside a-p rejected");
  check(!red::IsExtensionId("../dpbcbfnmbkopoojfekhnkhdbieeh/x"), "path characters rejected");
  check(red::IsListedLocation(1) && red::IsListedLocation(4) && red::IsListedLocation(8), "user installs listed");
  check(!red::IsListedLocation(5) && !red::IsListedLocation(10) && !red::IsListedLocation(0), "component extensions hidden");
  check(red::ExtensionRelativePath("popup.html") == "popup.html", "plain popup path");
  check(red::ExtensionRelativePath("/ui/popup.html") == "ui/popup.html", "leading slash stripped");
  check(red::ExtensionRelativePath("popup.html?tab=1#x", true) == "popup.html?tab=1#x", "query allowed for pages");
  check(!red::ExtensionRelativePath("popup.html?tab=1"), "query rejected for files");
  for (auto bad : {"", "../secret", "ui/../../x", "ui//x", "C:/x.html", "https://evil/x", "a\\b", "ui/./x", "//host/x", "a b.html"})
    check(!red::ExtensionRelativePath(bad, true), "unsafe package path rejected");
  check(red::MessageKey("__MSG_extName__") == "extName", "message reference");
  check(red::MessageKey("Dark Reader").empty() && red::MessageKey("__MSG___").empty(), "plain names stay");
  const auto locales = red::LocaleCandidates("ru-RU", "en");
  check(locales.size() == 3 && locales[0] == "ru_RU" && locales[1] == "ru" && locales[2] == "en", "locale fallback order");
  check(red::LocaleCandidates("en-US", "../x").size() == 2, "unsafe default locale ignored");
  check(red::PickIconSize({16, 48, 128}, 32) == 48, "smallest icon at least target");
  check(red::PickIconSize({128, 32, 48}, 32) == 32, "exact size wins");
  check(red::PickIconSize({16, 19}, 32) == 19, "largest when all are small");
  check(red::PickIconSize({}, 32) == 0, "no icons");
  check(red::InsideDirectory("C:/p/Extensions", "C:/p/Extensions/id/1.0"), "package inside profile");
  check(!red::InsideDirectory("C:/p/Extensions", "C:/p/Extensions/../Secure"), "escape rejected");
  check(!red::InsideDirectory("C:/p/Extensions", "C:/p/Extensions"), "root itself is not a package");
  check(!red::InsideDirectory("C:/p/Ext", "C:/p/Extensions/x"), "prefix of a name is not containment");
  return failures;
}
