#include "navigation/shortcuts.h"
#include "windows/auto_pip.h"
#include "app/state.h"
#include "app/session.h"
#include "workspace/controller.h"
#include "windows/focus.h"
#include "windows/devtools.h"
#include "windows/embedded_content.h"
#include "windows/main_menu.h"
#include "windows/settings_window.h"
#include "windows/panel_popup.h"
#include "windows/sidebar_peek.h"
#include "include/cef_id_mappers.h"
namespace red {
bool ExecuteShortcut(const std::shared_ptr<State>& state, Shortcut action) {
  if (!state->workspace) return false;
  if (state->workspace->WindowClosing()) return true;
  if (state->panel_popup && action != Shortcut::Suppressed) state->panel_popup->DismissIntegration();
  auto page = state->content;
  switch (action) {
    case Shortcut::Suppressed: return true;
    case Shortcut::Address: state->workspace->FocusAddress(); break;
    case Shortcut::NewTab: state->workspace->OpenNewTab(); break;
    case Shortcut::CloseTab: state->workspace->CloseActive(); break;
    case Shortcut::NextTab: state->workspace->CycleTab(false); break;
    case Shortcut::Space1: case Shortcut::Space2: case Shortcut::Space3: case Shortcut::Space4: case Shortcut::Space5:
    case Shortcut::Space6: case Shortcut::Space7: case Shortcut::Space8: case Shortcut::Space9:
      state->workspace->SwitchSpaceAt(static_cast<int>(action) - static_cast<int>(Shortcut::Space1)); break;
    case Shortcut::PreviousTab: state->workspace->CycleTab(true); break;
    case Shortcut::Sidebar: state->workspace->SetSidebarVisible(!state->sidebar_visible); FocusBrowser(page); break;
    case Shortcut::Pin:
      if (state->workspace->HasLinkPeek()) state->workspace->Notify("Open the preview as a tab to pin it");
      else state->workspace->TogglePin(); break;
    case Shortcut::Back: state->workspace->NavigateHistory(false); break;
    case Shortcut::Forward: state->workspace->NavigateHistory(true); break;
    case Shortcut::Reload: state->workspace->Reload(); break;
    case Shortcut::ReloadBypass: state->workspace->Reload(true); break;
    case Shortcut::Stop:
      if (state->workspace->HasLinkPeek() && (!state->embedded || !state->embedded->IsPageFullscreen())) { state->workspace->DismissLinkPeek(); break; }
      if (state->sidebar_peek && state->sidebar_peek->is_resizing()) { state->sidebar_peek->EndResize(true); break; }
      if (state->embedded && state->embedded->ExitBrowserFullscreen()) break;
      if (state->embedded && page && state->embedded->IsFullscreenPage(page->GetIdentifier())) return false;
      if (!page || !page->IsLoading()) return false;
      page->StopLoad(); FocusBrowser(page); break;
    case Shortcut::Fullscreen: if (state->embedded) state->embedded->ToggleBrowserFullscreen(); break;
    case Shortcut::FocusPage: FocusBrowser(page); break;
    case Shortcut::CloseWindow: if (state->window) state->window->Close(); break;
    case Shortcut::DevTools: ShowPageDevTools(state, page, CefPoint(), true); break;  // Never inspect Shell.
    case Shortcut::History: if (state->panel_popup) state->panel_popup->Toggle("history", kMainMenuAnchor); break;
    case Shortcut::Settings: ShowSettings(state); break;
    case Shortcut::NewWindow: if (state->session && state->session->new_window) state->session->new_window(state, ""); break;
    case Shortcut::NewPrivateWindow: if (state->session && state->session->new_private_window) state->session->new_private_window(state); break;
    // Ctrl+J opens the full downloads page (the popover belongs to the top-bar button).
    case Shortcut::Downloads: if (state->panel_popup) state->panel_popup->Hide(false); state->workspace->OpenDownloadsPage(); break;
    case Shortcut::ReopenClosed: state->workspace->ReopenClosed(); break;
    case Shortcut::PictureInPicture: if (page) TogglePictureInPicture(page); break;
    case Shortcut::Print: if (page) page->GetHost()->Print(); break;  // Chrome's print preview.
    case Shortcut::HistoryPage: if (state->panel_popup) state->panel_popup->Hide(false); state->workspace->OpenHistoryPage(); break;
  }
  auto data = CefDictionaryValue::Create(); data->SetString("action", std::string(kShortcutNames[static_cast<int>(action)]));
  state->Record("workspace_shortcut", data);
  return true;
}
std::optional<Shortcut> ChromeShortcut(int command) {
  const std::pair<const char*, Shortcut> commands[] = {
    {"IDC_FOCUS_LOCATION", Shortcut::Address}, {"IDC_NEW_TAB", Shortcut::NewTab},
    {"IDC_NEW_WINDOW", Shortcut::NewWindow},
    {"IDC_CLOSE_TAB", Shortcut::CloseTab}, {"IDC_SELECT_NEXT_TAB", Shortcut::NextTab},
    {"IDC_CLOSE_WINDOW", Shortcut::CloseWindow},
    {"IDC_DEV_TOOLS", Shortcut::DevTools},
    {"IDC_FULLSCREEN", Shortcut::Fullscreen},
    {"IDC_SELECT_PREVIOUS_TAB", Shortcut::PreviousTab}, {"IDC_BOOKMARK_THIS_TAB", Shortcut::Pin},
    {"IDC_BACK", Shortcut::Back}, {"IDC_FORWARD", Shortcut::Forward},
    {"IDC_RELOAD", Shortcut::Reload}, {"IDC_RELOAD_BYPASSING_CACHE", Shortcut::ReloadBypass}, {"IDC_STOP", Shortcut::Stop},
    {"IDC_RESTORE_TAB", Shortcut::ReopenClosed}, {"IDC_SHOW_HISTORY", Shortcut::History}, {"IDC_SHOW_DOWNLOADS", Shortcut::Downloads},
  };
  for (const auto& [name, action] : commands) if (command >= 0 && command == cef_id_for_command_id_name(name)) return action;
  return std::nullopt;
}
}  // namespace red
