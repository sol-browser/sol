#pragma once
#include <memory>
#include <vector>
#include <string>
#include <string_view>
#include "include/cef_command_line.h"
#include "include/cef_values.h"
struct URegularExpression;
namespace red {
class AppSession;
inline constexpr std::string_view kLinkLittleSol = "littleSol";
inline constexpr std::string_view kLinkRecentSpace = "recent";

enum class LinkMatch { Contains, Exact, Regex };
struct LinkRoute {
  LinkMatch match;
  std::string pattern;
  std::string destination;
  std::shared_ptr<URegularExpression> regex;
};
struct LinkRouting {
  std::vector<LinkRoute> rules;
  std::string fallback;  // Empty in existing settings. Use littleSolLinks in that case.
};
bool ReadLinkRouting(CefRefPtr<CefDictionaryValue> value, LinkRouting& routing, std::string* error = nullptr);
CefRefPtr<CefDictionaryValue> DescribeLinkRouting(const LinkRouting& routing);

// A Space ID can be provisional while the workspace loads. Resolve again before opening a tab.
std::string_view ResolveExternalLink(const AppSession& session, std::string_view url);
// Only web URLs or explicit existing local files; no internal/extension origin.
std::vector<std::string> ExternalLaunchUrls(CefRefPtr<CefCommandLine> command);
}  // namespace red
