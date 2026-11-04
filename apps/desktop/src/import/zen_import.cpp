#include "import/zen_import.h"
#include "windows/settings_window.h"
#include "workspace/controller.h"

namespace red {
// The main menu's import entry opens Settings → Import, where Zen, Chrome, Edge
// and Arc imports share one flow (import/browser_import.cpp).
void ShowZenImport(const std::shared_ptr<State>& state) {
  if (!state->window || !state->workspace || state->workspace->WindowClosing()) return;
  ShowSettings(state);
  if (state->settings_window) state->settings_window->ShowSection("import");
}
}  // namespace red
