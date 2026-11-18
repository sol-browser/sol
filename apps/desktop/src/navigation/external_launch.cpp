#include "navigation/external_launch.h"
#include "app/session.h"
#include "navigation/file_url.h"
#include "web_url.h"
#include "include/cef_parser.h"
#include <windows.h>
#include <icu.h>
#include <algorithm>
namespace red {
namespace {
constexpr size_t kMaxRules = 24;
constexpr size_t kMaxPatternBytes = 1024;

template <typename Function>
Function RegexExport(HMODULE library, const char* name) {
  return library ? reinterpret_cast<Function>(GetProcAddress(library, name)) : nullptr;
}

struct RegexApi {
  HMODULE library;
  decltype(&uregex_open) open = RegexExport<decltype(open)>(library, "uregex_open");
  decltype(&uregex_close) close = RegexExport<decltype(close)>(library, "uregex_close");
  decltype(&uregex_clone) clone = RegexExport<decltype(clone)>(library, "uregex_clone");
  decltype(&uregex_setText) set_text = RegexExport<decltype(set_text)>(library, "uregex_setText");
  decltype(&uregex_setTimeLimit) set_time_limit = RegexExport<decltype(set_time_limit)>(library, "uregex_setTimeLimit");
  decltype(&uregex_setStackLimit) set_stack_limit = RegexExport<decltype(set_stack_limit)>(library, "uregex_setStackLimit");
  decltype(&uregex_find) find = RegexExport<decltype(find)>(library, "uregex_find");
  bool available() const { return open && close && clone && set_text && set_time_limit && set_stack_limit && find; }
};
const RegexApi& Regex() {
  // Keep system ICU loaded while compiled rules exist. Older Windows 10 has a split ICU.
  static const RegexApi api = [] {
    RegexApi combined{LoadLibraryExW(L"icu.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32)};
    if (combined.available()) return combined;
    if (combined.library) FreeLibrary(combined.library);
    return RegexApi{LoadLibraryExW(L"icuin.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32)};
  }();
  return api;
}

std::shared_ptr<URegularExpression> CompileRegex(const std::string& pattern) {
  const auto& api = Regex();
  if (!api.available()) return {};
  const auto text = CefString(pattern).ToString16();
  UErrorCode status = U_ZERO_ERROR;
  auto* compiled = api.open(text.data(), static_cast<int32_t>(text.size()), UREGEX_CASE_INSENSITIVE, nullptr, &status);
  if (!compiled) return {};
  std::shared_ptr<URegularExpression> owned(compiled, api.close);
  return U_SUCCESS(status) ? std::move(owned) : nullptr;
}

bool Matches(const LinkRoute& rule, std::string_view url) {
  switch (rule.match) {
    case LinkMatch::Contains: return url.find(rule.pattern) != std::string_view::npos;
    case LinkMatch::Exact: return url == rule.pattern;
    case LinkMatch::Regex: break;
  }
  if (!rule.regex) return false;
  const auto text = CefString(std::string(url)).ToString16();
  const auto& api = Regex();
  UErrorCode status = U_ZERO_ERROR;
  const std::unique_ptr<URegularExpression, decltype(api.close)> matcher(api.clone(rule.regex.get(), &status), api.close);
  if (!matcher || U_FAILURE(status)) return false;
  // ICU time units count matching work, not wall clock. Both limits apply to each URL;
  // exhausting either is a non-match so the remaining rules can still run.
  api.set_time_limit(matcher.get(), 4, &status);
  api.set_stack_limit(matcher.get(), 128 * 1024, &status);
  api.set_text(matcher.get(), text.data(), static_cast<int32_t>(text.size()), &status);
  return api.find(matcher.get(), 0, &status) && U_SUCCESS(status);
}

bool ValidDestination(const std::string& value) {
  return value == kLinkLittleSol || value == kLinkRecentSpace ||
      (value.size() == 36 && ValidBrowsingProfileId(value));  // Spaces use the same UUID format.
}
}  // namespace

bool ReadLinkRouting(CefRefPtr<CefDictionaryValue> value, LinkRouting& routing, std::string* error) {
  if (!value || value->GetSize() != 2 || value->GetType("fallback") != VTYPE_STRING) return false;
  const auto rules = value->GetList("rules");
  LinkRouting next;
  next.fallback = value->GetString("fallback");
  if (!ValidDestination(next.fallback) || !rules || rules->GetSize() > kMaxRules) return false;
  for (size_t i = 0; i < rules->GetSize(); ++i) {
    const auto row = rules->GetDictionary(i);
    if (!row || row->GetSize() != 3 || row->GetType("match") != VTYPE_STRING ||
        row->GetType("pattern") != VTYPE_STRING || row->GetType("destination") != VTYPE_STRING) return false;
    const auto match = row->GetString("match").ToString();
    if (match != "contains" && match != "exact" && match != "regex") return false;
    LinkRoute rule{match == "regex" ? LinkMatch::Regex : match == "exact" ? LinkMatch::Exact : LinkMatch::Contains,
                   row->GetString("pattern"), row->GetString("destination"), {}};
    if (!ValidDestination(rule.destination) || rule.pattern.empty() ||
        rule.pattern.size() > kMaxPatternBytes || rule.pattern.front() == ' ' || rule.pattern.back() == ' ' ||
        std::any_of(rule.pattern.begin(), rule.pattern.end(), [](unsigned char c) { return c < 32 || c == 127; }) ||
        (rule.match == LinkMatch::Exact && (!HasWebScheme(rule.pattern) || !ValidHomepage(rule.pattern)))) return false;
    if (rule.match == LinkMatch::Regex) {
      rule.regex = CompileRegex(rule.pattern);
      // Preserve saved settings if this Windows installation lacks a compatible ICU.
      if (!rule.regex && error) {
        *error = Regex().available() ? "Invalid regular expression in rule " + std::to_string(i + 1) + "." :
                                       "Regular expressions are unavailable on this Windows installation.";
        return false;
      }
    }
    next.rules.push_back(std::move(rule));
  }
  routing = std::move(next);
  return true;
}

CefRefPtr<CefDictionaryValue> DescribeLinkRouting(const LinkRouting& routing) {
  auto value = CefDictionaryValue::Create();
  auto rules = CefListValue::Create();
  for (const auto& rule : routing.rules) {
    auto row = CefDictionaryValue::Create();
    row->SetString("match", rule.match == LinkMatch::Regex ? "regex" : rule.match == LinkMatch::Exact ? "exact" : "contains");
    row->SetString("pattern", rule.pattern);
    row->SetString("destination", rule.destination);
    rules->SetDictionary(rules->GetSize(), row);
  }
  value->SetList("rules", rules);
  value->SetString("fallback", routing.fallback);
  return value;
}

std::string_view ResolveExternalLink(const AppSession& session, std::string_view url) {
  if (!HasWebScheme(url)) return kLinkRecentSpace;
  if (!session.preferences) return kLinkLittleSol;
  const auto& preferences = session.preferences->Get();
  const auto available = [&](std::string_view id) {
    return id == kLinkLittleSol || id == kLinkRecentSpace || !session.workspace->ready ||
        std::any_of(session.workspace->tree.spaces.begin(), session.workspace->tree.spaces.end(),
                    [id](const auto& space) { return space.id == id; });
  };
  for (const auto& rule : preferences.link_routing.rules) {
    if (available(rule.destination) && Matches(rule, url))
      return rule.destination;
  }
  const auto& fallback = preferences.link_routing.fallback;
  if (!fallback.empty() && available(fallback)) return fallback;
  return preferences.little_sol_links ? kLinkLittleSol : kLinkRecentSpace;
}

std::vector<std::string> ExternalLaunchUrls(CefRefPtr<CefCommandLine> command) {
  CefCommandLine::ArgumentList arguments; command->GetArguments(arguments);
  std::vector<std::string> urls;
  for (const auto& argument : arguments) {
    if (urls.size() >= 16) break;
    const auto text = argument.ToString();
    if (text.empty() || text.size() > 4096 || std::any_of(text.begin(), text.end(), [](unsigned char c) { return c < 32 || c == 127; })) continue;
    CefURLParts parts;
    if (CefParseURL(text, parts) && (CefString(&parts.scheme) == "https" || CefString(&parts.scheme) == "http") && parts.host.length) {
      urls.push_back(text); continue;
    }
    // The filename comes from an explicit OS launch, never from site IPC.
    const auto url = LocalFileUrl(std::filesystem::path(argument.ToWString()));
    if (!url.empty()) urls.push_back(url);
  }
  return urls;
}
}  // namespace red
