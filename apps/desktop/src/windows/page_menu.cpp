#include "windows/page_menu.h"
#include <map>
#include <windows.h>
#include "include/cef_id_mappers.h"
#include "include/views/cef_display.h"
#include "windows/menu_popup.h"
#include "workspace/controller.h"

namespace red {
namespace {
// Icons for Chrome's own commands, resolved by name for the pinned engine.
const std::map<int, std::string>& Icons() {
  static const std::map<int, std::string> icons = [] {
    static constexpr std::pair<const char*, const char*> kNames[] = {
      {"IDC_BACK", "arrowLeft"}, {"IDC_FORWARD", "arrowRight"}, {"IDC_RELOAD", "reload"}, {"IDC_PRINT", "print"},
      {"IDC_SAVE_PAGE", "save"}, {"IDC_VIEW_SOURCE", "source"}, {"IDC_CONTENT_CONTEXT_VIEWPAGESOURCE", "source"},
      {"IDC_CONTENT_CONTEXT_CUT", "cut"}, {"IDC_CONTENT_CONTEXT_COPY", "copy"}, {"IDC_CONTENT_CONTEXT_PASTE", "paste"},
      {"IDC_CONTENT_CONTEXT_PASTE_AND_MATCH_STYLE", "paste"}, {"IDC_CONTENT_CONTEXT_SELECTALL", "selectAll"}, {"IDC_CONTENT_CONTEXT_UNDO", "undo"},
      {"IDC_CONTENT_CONTEXT_REDO", "redo"}, {"IDC_CONTENT_CONTEXT_OPENLINKNEWTAB", "externalLink"},
      {"IDC_CONTENT_CONTEXT_OPENLINKNEWWINDOW", "window"}, {"IDC_CONTENT_CONTEXT_OPENLINKOFFTHERECORD", "private"}, {"IDC_CONTENT_CONTEXT_COPYLINKLOCATION", "link"},
      {"IDC_CONTENT_CONTEXT_SAVELINKAS", "download"}, {"IDC_CONTENT_CONTEXT_SAVEIMAGEAS", "save"}, {"IDC_CONTENT_CONTEXT_COPYIMAGE", "image"},
      {"IDC_CONTENT_CONTEXT_COPYIMAGELOCATION", "link"}, {"IDC_CONTENT_CONTEXT_OPENIMAGENEWTAB", "image"},
      {"IDC_CONTENT_CONTEXT_SEARCHWEBFOR", "search"}, {"IDC_CONTENT_CONTEXT_SEARCHWEBFORIMAGE", "search"},
      {"IDC_CONTENT_CONTEXT_TRANSLATE", "translate"}, {"IDC_CONTENT_CONTEXT_EMOJI", "emoji"},
      {"IDC_CONTENT_CONTEXT_SAVEAVAS", "save"}, {"IDC_CONTENT_CONTEXT_COPYAVLOCATION", "link"}, {"IDC_CONTENT_CONTEXT_PICTUREINPICTURE", "pip"},
      {"IDC_CONTENT_CONTEXT_LOOP", "reload"}, {"IDC_CONTENT_CONTEXT_CONTROLS", "sliders"}, {"IDC_CONTENT_CONTEXT_COPYLINKTEXT", "copy"}};
    std::map<int, std::string> result;
    for (const auto& [name, icon] : kNames) if (const int id = cef_id_for_command_id_name(name); id >= 0) result[id] = icon;
    return result;
  }();
  return icons;
}
// "&Copy" -> "Copy" ("&&" stays one "&").
std::string Plain(const std::string& label) {
  std::string out;
  for (size_t i = 0; i < label.size(); ++i) {
    if (label[i] == '&') { if (i + 1 < label.size() && label[i + 1] == '&') { out += '&'; ++i; } continue; }
    if (static_cast<unsigned char>(label[i]) >= 32) out += label[i];
  }
  return out;
}
std::string Accelerator(CefRefPtr<CefMenuModel> model, size_t index) {
  int key = 0; bool shift = false, ctrl = false, alt = false;
  if (!model->GetAcceleratorAt(index, key, shift, ctrl, alt)) return "";
  std::string name;
  if ((key >= 'A' && key <= 'Z') || (key >= '0' && key <= '9')) name = std::string(1, static_cast<char>(key));
  else if (key >= VK_F1 && key <= VK_F12) name = "F" + std::to_string(key - VK_F1 + 1);
  else if (key == VK_DELETE) name = "Del";
  else return "";
  return std::string(ctrl ? "Ctrl+" : "") + (shift ? "Shift+" : "") + (alt ? "Alt+" : "") + name;
}
// One answer to Chrome per menu: the choice, or a cancel when the menu goes away without one.
struct Answer {
  CefRefPtr<CefRunContextMenuCallback> callback;
  std::function<void()> closed;
  void Continue(int command) { if (auto c = std::exchange(callback, nullptr)) c->Continue(command, EVENTFLAG_NONE); Done(); }
  void Cancel() { if (auto c = std::exchange(callback, nullptr)) c->Cancel(); Done(); }
  void Done() { if (auto f = std::exchange(closed, nullptr)) f(); }
};
void Copy(MenuPopup::Builder& menu, CefRefPtr<CefMenuModel> model, const std::shared_ptr<Answer>& answer, int depth) {
  for (size_t i = 0; i < model->GetCount(); ++i) {
    if (!model->IsVisibleAt(i)) continue;
    const auto type = model->GetTypeAt(i);
    if (type == MENUITEMTYPE_SEPARATOR) { menu.Separator(); continue; }
    const auto label = Plain(model->GetLabelAt(i).ToString());
    if (label.empty()) continue;
    const int command = model->GetCommandIdAt(i);
    const auto icon = Icons().find(command);
    const std::string glyph = icon == Icons().end() ? "" : icon->second;
    if (type == MENUITEMTYPE_SUBMENU) {
      // Red's menu has three levels; deeper submenus are rare and flattened into their parent.
      if (const auto sub = model->GetSubMenuAt(i); sub && depth < 2) Copy(menu.Submenu(label, glyph, model->IsEnabledAt(i)), sub, answer, depth + 1);
      else if (sub) { menu.Separator(); Copy(menu, sub, answer, depth); menu.Separator(); }
      continue;
    }
    menu.Add(label, glyph, [answer, command] { answer->Continue(command); }, Accelerator(model, i), model->IsEnabledAt(i),
             (type == MENUITEMTYPE_CHECK || type == MENUITEMTYPE_RADIO) && model->IsCheckedAt(i));
  }
}
}  // namespace

bool OpensLinkInProfile(int command) {
  static const int first = cef_id_for_command_id_name("IDC_OPEN_LINK_IN_PROFILE_FIRST");
  static const int submenu = cef_id_for_command_id_name("IDC_CONTENT_CONTEXT_OPENLINKINPROFILE");
  return (submenu >= 0 && command == submenu) || (first >= 0 && command >= first && command < first + 100);
}

bool ShowPageMenu(const std::shared_ptr<State>& state, CefRefPtr<CefMenuModel> model, CefRefPtr<CefRunContextMenuCallback> callback,
                  CefRefPtr<CefWindow> return_to, std::function<void()> closed) {
  if (!state->window || !state->workspace || state->workspace->WindowClosing() || !model || !model->GetCount()) return false;
  POINT cursor{};
  if (!GetCursorPos(&cursor)) return false;
  if (!state->menu_popup) state->menu_popup = std::make_shared<MenuPopup>(state);
  auto answer = std::make_shared<Answer>(); answer->callback = callback; answer->closed = std::move(closed);
  MenuPopup::Builder menu;
  Copy(menu, model, answer, 0);
    // Keep the actual pointer as the anchor. Adding an offset at a shared
    // monitor edge can select the neighboring display instead of flipping.
  const auto at = CefDisplay::ConvertScreenPointFromPixels(CefPoint(cursor.x, cursor.y));
  static unsigned openings = 0;
  state->menu_popup->Open(menu, at, "page:" + std::to_string(++openings), [answer] { answer->Cancel(); }, return_to);
  state->Record("page_menu_shown");
  return true;
}
}  // namespace red
