#pragma once
#include <functional>
#include <string>
namespace red {
// Velopack (https://velopack.io) installs, updates and uninstalls Sol by
// starting sol.exe with its own arguments. Call first in the browser process,
// before any command-line validation: for those launches Velopack runs the hook
// and exits the process; for ordinary launches it returns immediately (applying
// an update that was downloaded earlier, then restarting into it).
//
// After an install or update the new version's folder must grant the sandbox's
// LPAC SID read/execute, as Sol's build and the Inno setup do (SET_LPAC_ACLS),
// or Chromium's sandboxed processes cannot start.
void RunUpdateHooks();

// When Sol runs from a Velopack install: on a background thread, checks the
// release feed (GitHub Releases of sol-browser/sol, or SOL_UPDATE_FEED: a local
// folder or http(s) URL for testing) and downloads a newer version. It is
// applied on the next start. |ready| runs on that background thread with the
// version that waits for a restart. Outside a Velopack install it does nothing.
void StartUpdateCheck(std::function<void(const std::string& version)> ready);

// The version downloaded and waiting for a restart; empty when none (any thread).
std::string ReadyUpdate();

// "Restart to update": hands the downloaded version to Velopack's updater,
// which waits for Sol to exit, applies it and starts Sol again. Returns false
// when nothing is waiting; on true the caller closes Sol's windows.
bool PrepareRestartToUpdate();
}  // namespace red
