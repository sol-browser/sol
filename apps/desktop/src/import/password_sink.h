#pragma once
#include "import/formats/import_types.h"
#include "include/cef_request_context.h"
#include <functional>
#include <memory>

namespace red {
// Saves sign-ins into Sol's own Chromium password store without patching
// CEF/Chromium: a hidden, Sol-owned browser opens Chromium's
// chrome://password-manager WebUI and calls its passwordsPrivate API (the
// one behind the page's own Add/Edit/Delete) through DevTools
// Runtime.evaluate. Passwords stay in memory: they cross only the
// browser→renderer IPC of that page, are never logged, written or sent to
// Sol's React UI.
struct PasswordSinkResult {
  int added = 0, existing = 0, failed = 0;
  std::string error;  // Whole-sink failure (store unavailable, timeout...).
};
// Browser import: adds each login unless that origin + username exists.
void SavePasswords(std::vector<importer::Login> logins, std::function<void(PasswordSinkResult)> done);

// Cloud sync (docs/sync.md, "Passwords"): one change per sign-in, matched by
// signon realm + username in the device store of |context|'s profile.
struct PasswordOp {
  enum class Kind { Upsert, Remove } kind = Kind::Upsert;
  std::string url, realm, username, password;
  PasswordOp() = default;
  PasswordOp(const PasswordOp&) = delete;
  PasswordOp(PasswordOp&&) noexcept = default;
  PasswordOp& operator=(const PasswordOp&) = delete;
  PasswordOp& operator=(PasswordOp&&) noexcept = default;
  ~PasswordOp() { importer::Wipe(password); }
};
struct PasswordOpsResult {
  std::vector<bool> applied;  // Per op, in order; false ops are retried by a later pass.
  std::string error;
};
void ApplyPasswordOps(CefRefPtr<CefRequestContext> context, std::vector<PasswordOp> ops, std::function<void(PasswordOpsResult)> done);
}  // namespace red
