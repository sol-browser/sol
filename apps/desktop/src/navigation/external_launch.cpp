#include "navigation/external_launch.h"
#include "navigation/file_url.h"
#include "include/cef_parser.h"
#include <algorithm>
namespace red {
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
