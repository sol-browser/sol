#pragma once
#include "import/formats/import_types.h"
#include <functional>
#include <memory>

namespace red {
// Saves imported sign-ins into Red's own Chromium password store without
// patching CEF/Chromium: a hidden, Red-owned browser opens Chromium's
// chrome://password-manager WebUI and calls its passwordsPrivate.addPassword
// (the API behind the page's "Add password" dialog) once per login through
// DevTools Runtime.evaluate. Passwords stay in memory: they cross only the
// browser→renderer IPC of that page, are never logged, written or sent to
// Red's React UI. Existing entries (same origin + username) are kept.
struct PasswordSinkResult {
  int added = 0, existing = 0, failed = 0;
  std::string error;  // Whole-sink failure (store unavailable, timeout...).
};
void SavePasswords(std::vector<importer::Login> logins, std::function<void(PasswordSinkResult)> done);
}  // namespace red
