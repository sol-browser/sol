#pragma once

#include <filesystem>
#include <functional>
#include <string>

#include "include/cef_request_context.h"
#include "include/cef_values.h"

namespace red {

// Sol's hidden translator page (docs/page-translation.md): sol://translator/index.html in its own
// "Translator" profile, so its renderer shares no process or storage with websites or Sol's UI and
// has no IPC to Sol. It runs Bergamot and ELD; native code calls window.solTranslator.* through the
// DevTools protocol. The page starts on first use and closes after a few idle minutes. UI thread only.
void ConfigureTranslator(const std::filesystem::path& profile, const std::filesystem::path& program_directory);
// The Translator profile's request context, created on first use (model downloads use it too).
void WithTranslatorContext(std::function<void(CefRefPtr<CefRequestContext>)> use);
// Evaluates |expression| (a call on window.solTranslator) in the page and passes its returned value,
// or nullptr when the page could not run it.
void CallTranslator(std::string expression, std::function<void(CefRefPtr<CefValue>)> reply);
// Sol is quitting: close the page now. Chrome ends its message loop only once every browser has closed.
void CloseTranslator();
// Before CefShutdown: closes the page (waiting up to 3 s), stops model downloads, releases the profile.
void ShutdownTranslator();
// Experiments only: each step of the translator page (probe report).
void SetTranslatorProbe(std::function<void(const std::string& step, const std::string& detail)> probe);

}  // namespace red
