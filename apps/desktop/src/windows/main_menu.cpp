#include "app/browsing_profiles.h"
#include "windows/main_menu.h"
#include "app/session.h"
#include "navigation/shortcut_settings.h"
#include <windows.h>
#include "include/cef_request_context.h"
#include "extensions/extension_catalog.h"
#include "navigation/shortcuts.h"
#include "windows/default_browser.h"
#include "windows/extension_popup.h"
#include "windows/extensions_window.h"
#include "windows/menu_popup.h"
#include "windows/panel_popup.h"
#include "windows/settings_window.h"
#include "windows/passwords_window.h"
#include "include/cef_id_mappers.h"
#include "workspace/controller.h"
#include "windows/update_window.h"

namespace red {
namespace {
// The first chord of an action ("Ctrl + T / Ctrl + N" -> "Ctrl+T"), as a menu shows it.
std::string Chord(CefRefPtr<CefDictionaryValue> labels, const char* action) {
  auto label = labels->GetString(action).ToString();
  if (const auto more = label.find(" / "); more != std::string::npos) label.resize(more);
  for (size_t at; (at = label.find(" + ")) != std::string::npos;) label.replace(at, 3, "+");
  return label;
}
void Workspace(const std::shared_ptr<State>& state, CefRefPtr<CefDictionaryValue> request) {
  if (state->workspace) state->workspace->Handle(request, [](int, const std::string&, CefRefPtr<CefDictionaryValue>) {});
}
}  // namespace

void PinExtension(const std::shared_ptr<State>& state, const std::string& id, bool pinned) {
  const auto pins = ActiveProfilePins(state);
  if (!pins || !state->extensions || (pinned && !state->extensions->Find(id))) return;
  if (!pins->Set(id, pinned)) { if (state->workspace && pinned) state->workspace->Notify("Could not pin the extension."); return; }
  for (const auto& window : state->session->Windows()) if (window->workspace) window->workspace->Refresh();
  auto details = CefDictionaryValue::Create(); details->SetBool("pinned", pinned); state->Record("extension_pinned", details);
}

void ShowExtensionMenu(const std::shared_ptr<State>& state, const std::string& id, const CefPoint& at, const CefPoint& popup,
                       CefRefPtr<CefWindow> return_to, std::function<void()> chosen, std::function<void()> dismissed) {
  const auto* item = state->extensions ? state->extensions->Find(id) : nullptr;
  if (!item || !state->window) return;
  if (!state->menu_popup) state->menu_popup = std::make_shared<MenuPopup>(state);
  const std::weak_ptr<State> weak = state;
  const auto pins = ActiveProfilePins(state);
  const bool pinned = pins && pins->Contains(id);
  MenuPopup::Builder menu;
  const auto first = [chosen] { if (chosen) chosen(); };
  // Opening the extension replaces the popover it was asked from, as a click on the extension does.
  menu.Add("Open " + item->name, "puzzle", [weak, id, popup, first] {
    first(); const auto s = weak.lock(); if (!s) return;
    if (s->panel_popup) s->panel_popup->Hide(false);
    RunExtension(s, id, popup);
  }, "", true, false, item->icon);
  menu.Add(pinned ? "Unpin from Top Bar" : "Pin to Top Bar", pinned ? "pinOff" : "pin",
           [weak, id, pinned, first] { first(); if (const auto s = weak.lock()) PinExtension(s, id, !pinned); }, "", pins != nullptr);
  menu.Separator();
  menu.Add("Manage Extensions", "settings", [weak, first] {
    first(); const auto s = weak.lock(); if (!s) return;
    if (s->panel_popup) s->panel_popup->Hide(false);
    if (!s->extensions_window) s->extensions_window = std::make_shared<ExtensionsWindow>(s);
    s->extensions_window->Show();
  });
  static unsigned openings = 0;
  state->menu_popup->Open(menu, at, "extension:" + std::to_string(++openings), std::move(dismissed), return_to);
}

void ShowCopyMenu(const std::shared_ptr<State>& state, const CefRect& anchor) {
  if (!state->window || !state->workspace) return;
  if (!state->menu_popup) state->menu_popup = std::make_shared<MenuPopup>(state);
  const std::weak_ptr<State> weak = state;
  const auto run = [weak](std::function<void(const std::shared_ptr<State>&)> body) {
    return [weak, body = std::move(body)] { if (const auto s = weak.lock(); s && s->window && s->workspace && !s->workspace->WindowClosing()) body(s); };
  };
  const auto prefs = state->session && state->session->preferences ? state->session->preferences->Get() : Preferences{};
  const auto labels = ShortcutLabels(prefs);
  const auto origin = state->window->GetClientAreaBoundsInScreen();
  const CefPoint below(origin.x + anchor.x, origin.y + anchor.y + anchor.height + 6);
  MenuPopup::Builder menu;
  menu.Add("Copy Link", "copy", run([](const std::shared_ptr<State>& s) { s->workspace->CopyActiveLink(); }));
  menu.Add("Super Copy (without trackers)", "link", run([](const std::shared_ptr<State>& s) { s->workspace->CopyCleanLink(); }), Chord(labels, "copyUrl"));
  state->menu_popup->Open(menu, below, "copy");
  state->Record("copy_menu_shown");
}

void ShowLittleSpaces(const std::shared_ptr<State>& state, const CefRect& anchor) {
  if (!state->little_window || !state->window || !state->workspace || !state->session) return;
  const auto& spaces = state->session->workspace->tree.spaces;
  if (spaces.empty()) return;
  if (!state->menu_popup) state->menu_popup = std::make_shared<MenuPopup>(state);
  const auto origin = state->window->GetClientAreaBoundsInScreen();
  MenuPopup::Builder menu;
  for (const auto& space : spaces) {
    const auto label = space.name + (space.profile == state->active_profile ? "" : " (another profile)");
    menu.Add(label, "externalLink", [weak = std::weak_ptr<WorkspaceController>(state->workspace), id = space.id] {
      if (const auto workspace = weak.lock()) workspace->SendLittlePage(id, [weak](int code, const std::string& error, CefRefPtr<CefDictionaryValue>) {
        if (code) if (const auto source = weak.lock()) source->Notify(error);
      });
    });
  }
  state->menu_popup->Open(menu, CefPoint(origin.x + anchor.x, origin.y + anchor.y + anchor.height + 6), "little-spaces");
}

void ShowMainMenu(const std::shared_ptr<State>& state, const CefRect& anchor) {
  if (!state->window || !state->workspace) return;
  if (!state->menu_popup) state->menu_popup = std::make_shared<MenuPopup>(state);
  const std::weak_ptr<State> weak = state;
  // Every action looks the window up again: the menu may outlive a closing window.
  const auto run = [weak](std::function<void(const std::shared_ptr<State>&)> body) {
    return [weak, body = std::move(body)] { if (const auto s = weak.lock(); s && s->window && s->workspace && !s->workspace->WindowClosing()) body(s); };
  };
  const auto shortcut = [run](Shortcut action) { return run([action](const std::shared_ptr<State>& s) { ExecuteShortcut(s, action); }); };
  const auto prefs = state->session && state->session->preferences ? state->session->preferences->Get() : Preferences{};
  const auto labels = ShortcutLabels(prefs);
  const auto snapshot = state->workspace->Snapshot();
  const bool standalone = state->standalone_window, sidebar = state->sidebar_visible && !standalone;
  const auto origin = state->window->GetClientAreaBoundsInScreen();
  const CefPoint below(origin.x + anchor.x, origin.y + anchor.y + anchor.height + 6);

  MenuPopup::Builder menu;
  const auto status = DefaultBrowserStatus();
  if (status->GetType("isDefault") == VTYPE_BOOL && !status->GetBool("isDefault"))
    menu.Add("Set Sol as Default Browser", "heart", run([](const std::shared_ptr<State>& s) {
      std::string error; OpenDefaultBrowserSettings(s->window->GetWindowHandle(), error);
    }));
  menu.Add("Check for Updates", "reload", run([](const std::shared_ptr<State>& s) { ShowUpdateWindow(s); }));
  menu.Separator();
  menu.Add("New Tab", "plus", shortcut(Shortcut::NewTab), Chord(labels, "newTab"));
  menu.Add("New Window", "window", shortcut(Shortcut::NewWindow), Chord(labels, "newWindow"));
  menu.Add("New Private Window", "private", shortcut(Shortcut::NewPrivateWindow), Chord(labels, "newPrivateWindow"));
  menu.Separator();

  // Spaces: create/rename run the sidebar's own flows; the list switches like Ctrl+1…9.
  auto& spaces = menu.Submenu("Spaces", "spaces", !standalone);
  spaces.Add("New Space", "plus", run([](const std::shared_ptr<State>& s) { s->workspace->RequestSidebar("newSpace"); }), "", sidebar);
  spaces.Add("Rename Space", "pencil", run([](const std::shared_ptr<State>& s) { s->workspace->RequestSidebar("editSpace"); }), "", sidebar);
  spaces.Separator();
  const auto list = snapshot->GetList("spaces");
  const auto active = snapshot->GetString("activeSpace").ToString();
  const size_t count = list ? list->GetSize() : 0;
  size_t current = 0;
  for (size_t i = 0; i < count; ++i) if (list->GetDictionary(i)->GetString("id") == active) current = i;
  const auto switch_to = [run](const std::string& id) {
    return run([id](const std::shared_ptr<State>& s) {
      auto request = CefDictionaryValue::Create(); request->SetString("method", "space.switch"); request->SetString("id", id); Workspace(s, request);
    });
  };
  if (count > 1) {
    spaces.Add("Next Space", "arrowRight", switch_to(list->GetDictionary((current + 1) % count)->GetString("id")));
    spaces.Add("Previous Space", "arrowLeft", switch_to(list->GetDictionary((current + count - 1) % count)->GetString("id")));
    spaces.Separator();
  }
  for (size_t i = 0; i < count; ++i) {
    const auto space = list->GetDictionary(i);
    const auto name = space->GetString("name").ToString();
    const std::string key = "space" + std::to_string(i + 1);
    spaces.Add(name.empty() ? "Space " + std::to_string(i + 1) : name, "", switch_to(space->GetString("id")),
               i < 9 ? Chord(labels, key.c_str()) : "", true, space->GetString("id") == active);
  }

  auto& tabs = menu.Submenu("Tabs", "tabs");
  tabs.Add("Open Command Bar", "search", shortcut(Shortcut::Address), Chord(labels, "address"));
  tabs.Add("New Tab", "plus", shortcut(Shortcut::NewTab), Chord(labels, "newTab"));
  tabs.Add("Pin Tab", "pin", shortcut(Shortcut::Pin), Chord(labels, "pin"), state->content != nullptr && !standalone);
  tabs.Add("New Folder", "folder", run([](const std::shared_ptr<State>& s) { s->workspace->RequestSidebar("newFolder"); }), "", sidebar);
  tabs.Separator();
  tabs.Add("Recent Tab", "history", shortcut(Shortcut::RecentTab), Chord(labels, "recentTab"));
  tabs.Add("Next Tab", "arrowDown", shortcut(Shortcut::NextTab), Chord(labels, "nextTab"));
  tabs.Add("Previous Tab", "arrowUp", shortcut(Shortcut::PreviousTab), Chord(labels, "previousTab"));
  tabs.Separator();
  tabs.Add("Reopen Last Closed Tab", "undo", shortcut(Shortcut::ReopenClosed), Chord(labels, "reopenClosed"));
  if (!standalone) tabs.Add("Archive", "archive", run([anchor](const std::shared_ptr<State>& s) { if (s->panel_popup) s->panel_popup->Toggle("archive", anchor); }));

  // Extensions: the web profile's installed list, each opened as from a toolbar button.
  auto& extensions = menu.Submenu("Extensions", "puzzle");
  extensions.Add("Add Extension", "plus", run([](const std::shared_ptr<State>& s) { s->workspace->OpenLink(kChromeWebStoreUrl, true); }));
  extensions.Add("Manage Extensions", "settings", run([](const std::shared_ptr<State>& s) {
    if (!s->extensions_window) s->extensions_window = std::make_shared<ExtensionsWindow>(s);
    s->extensions_window->Show();
  }));
  if (state->extensions && !state->private_window) {
    const auto installed = state->extensions->List(state->WebContext());
    const auto pins = ActiveProfilePins(state);
    bool first = true;
    for (const auto& item : installed) {
      if (!item.action && item.popup.empty() && item.options.empty()) continue;
      if (first) { extensions.Separator(); first = false; }
      const std::string id = item.id;
      extensions.Add(item.name, "puzzle", run([id, below](const std::shared_ptr<State>& s) { RunExtension(s, id, below); }), "", true, false, item.icon);
    }
    // Pins: a checked row is on the top bar; choosing it toggles.
    if (pins && !first) {
      extensions.Separator();
      auto& pin = extensions.Submenu("Pin to Top Bar", "pin");
      for (const auto& item : installed) {
        if (!item.action && item.popup.empty() && item.options.empty()) continue;
        const std::string id = item.id; const bool pinned = pins->Contains(id);
        pin.Add(item.name, "", run([id, pinned](const std::shared_ptr<State>& s) { PinExtension(s, id, !pinned); }), "", true, pinned);
      }
    }
  }

  auto& developer = menu.Submenu("Developer", "code");
  developer.Add("Developer Tools", "code", shortcut(Shortcut::DevTools), Chord(labels, "devTools"), state->content != nullptr);
  developer.Add("Reload Ignoring Cache", "reload", shortcut(Shortcut::ReloadBypass), Chord(labels, "reloadBypass"), state->content != nullptr);
  menu.Separator();

  menu.Add("History", "history", shortcut(Shortcut::History), Chord(labels, "history"));
  menu.Add("Downloads", "download", shortcut(Shortcut::Downloads), Chord(labels, "downloads"));
  auto& passwords = menu.Submenu("Passwords", "lock");
  passwords.Add("Passwords for This Page", "lock", run([](const std::shared_ptr<State>& s) {
    if (s->content && s->content->GetHost()->CanExecuteChromeCommand(cef_id_for_command_id_name("IDC_MANAGE_PASSWORDS_FOR_PAGE")))
      s->content->GetHost()->ExecuteChromeCommand(cef_id_for_command_id_name("IDC_MANAGE_PASSWORDS_FOR_PAGE"), CEF_WOD_CURRENT_TAB);
  }), "", state->content && state->content->GetHost()->CanExecuteChromeCommand(cef_id_for_command_id_name("IDC_MANAGE_PASSWORDS_FOR_PAGE")));
  passwords.Add("Manage Passwords", "settings", run([](const std::shared_ptr<State>& s) {
    ShowPasswords(s, s->active_profile);
  }), "", !state->private_window && !standalone);
  menu.Separator();
  if (!standalone) menu.Add("Theme", "palette", run([anchor](const std::shared_ptr<State>& s) { if (s->panel_popup) s->panel_popup->Toggle("theme", anchor); }));
  menu.Add("Settings", "settings", run([](const std::shared_ptr<State>& s) { ShowSettings(s); }), Chord(labels, "settings"));
  menu.Add("Print…", "print", shortcut(Shortcut::Print), Chord(labels, "print"), state->content != nullptr);
  menu.Add("Copy Link Without Trackers", "link", shortcut(Shortcut::CopyUrl), Chord(labels, "copyUrl"), state->content != nullptr);
  auto& capture = menu.Submenu("Screenshot", "image", state->content != nullptr);
  capture.Add("Select Area or Element…", "image", shortcut(Shortcut::Capture), Chord(labels, "capture"));
  capture.Add("Visible Area", "image", shortcut(Shortcut::Screenshot), Chord(labels, "screenshot"));
  capture.Add("Full Page", "image", shortcut(Shortcut::FullPageScreenshot), Chord(labels, "fullPageScreenshot"));
  menu.Separator();
  menu.Add("Close Window", "close", run([](const std::shared_ptr<State>& s) { s->window->Close(); }), Chord(labels, "closeWindow"));

  state->menu_popup->Open(menu, below, "main");
  state->Record("main_menu_shown");
}
}  // namespace red
