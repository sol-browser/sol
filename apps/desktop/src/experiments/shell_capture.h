#pragma once
#include <filesystem>
#include "include/cef_browser.h"
#include "include/cef_registration.h"

namespace red {
// Trusted shell only. Native website child windows are separate surfaces.
CefRefPtr<CefRegistration> CaptureShell(CefRefPtr<CefBrowser> shell, const std::filesystem::path& output);
}
