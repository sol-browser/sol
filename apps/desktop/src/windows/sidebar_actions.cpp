#include "windows/sidebar_actions.h"
#include <cmath>
#include "app/updates.h"
#include "app/session.h"
#include "navigation/shortcuts.h"
#include "windows/settings_window.h"
#include "windows/sidebar_peek.h"
#include "windows/panel_popup.h"
#include "windows/focus.h"
#include "windows/workspace_layout.h"
#include "workspace/controller.h"
#include "include/cef_parser.h"
namespace red {
namespace {
void Reply(CefRefPtr<CefMessageRouterBrowserSide::Callback> callback,CefRefPtr<CefDictionaryValue> snapshot) {
  auto value=CefValue::Create();value->SetDictionary(snapshot);callback->Success(CefWriteJSON(value,JSON_WRITER_DEFAULT));
}
bool PointerOverSidebar(const State& state, CefRefPtr<CefBrowser> source) {
  if (!source || !state.window || state.window->IsMinimized()) return false;
  const auto widget = source->GetHost()->GetWindowHandle();
  POINT cursor{};
  if (!widget || !GetCursorPos(&cursor)) return false;
  const HWND hit = WindowFromPoint(cursor);
  if (hit != widget && !IsChild(widget, hit)) return false;
  if (source->GetIdentifier() == state.peek_id) {
    return state.sidebar_peek && state.sidebar_peek->is_open() && state.sidebar_peek->window_visible();
  }
  if (!state.sidebar_visible) return false;
  const HWND root = state.window->GetWindowHandle();
  RECT client{};
  if (!ScreenToClient(root, &cursor) || !GetClientRect(root, &client) || !PtInRect(&client, cursor)) return false;
  const double scale = GetDpiForWindow(root) / 96.0;
  const int width = SidebarWidth(state.sidebar_width, state.window->GetClientAreaBoundsInScreen().width);
  return cursor.x < std::lround(width * scale) && cursor.y >= std::lround(kToolbarHeight * scale);
}
// x/y/width/height (DIP in |source|, the docked or floating sidebar) as root client DIP.
// False when they are not numbers inside that sidebar document.
bool SidebarAnchor(const State& s, CefRefPtr<CefBrowser> source, CefRefPtr<CefDictionaryValue> d, int max_width, int max_height, CefRect& out) {
  for (const auto* key : {"x", "y", "width", "height"}) if (d->GetType(key) != VTYPE_INT || d->GetInt(key) < 0 || d->GetInt(key) > 16384) return false;
  if (!source || !s.window) return false;
  const auto handle = source->GetHost()->GetWindowHandle(); const auto root = s.window->GetWindowHandle();
  POINT origin{}, parent{}; RECT client{}; const float scale = GetDpiForWindow(root) / 96.f;
  const int x = d->GetInt("x"), y = d->GetInt("y"), w = d->GetInt("width"), h = d->GetInt("height");
  if (!handle || !GetClientRect(handle, &client) || !ClientToScreen(handle, &origin) || !ClientToScreen(root, &parent) ||
      w < 1 || h < 1 || w > max_width || h > max_height || (x + w) * scale > client.right + 2 || (y + h) * scale > client.bottom + 2) return false;
  out = CefRect(x + static_cast<int>((origin.x - parent.x) / scale), y + static_cast<int>((origin.y - parent.y) / scale), w, h);
  return true;
}
}
bool HandleSidebarAction(const std::shared_ptr<State>& s,CefRefPtr<CefBrowser> source,const std::string& method,
    CefRefPtr<CefDictionaryValue> d,bool persistent,CefRefPtr<CefMessageRouterBrowserSide::Callback> cb) {
  if (method == "workspace.sidebarHovered") {
    if (!s || !s->workspace || persistent || d->GetSize() != 1) cb->Failure(400, "Invalid sidebar hover request");
    else cb->Success(!s->workspace->WindowClosing() && PointerOverSidebar(*s, source) ? "true" : "false");
    return true;
  }
  const bool new_tab=method=="browser.shortcut" && d->GetType("action")==VTYPE_STRING && d->GetString("action")=="newTab";
  const bool drag=method=="tab.detach" || method=="tab.dragStarted" || method=="tab.dragCancel" || method=="tab.dragEnded";
  const bool home=method=="tab.setHome";
  const bool edit_pinned=method=="tab.editPinned";
  const bool confirm_move=method=="tab.confirmMoveToSpace";
  const bool batch=home || method=="tab.copyLink" || method=="tab.archiveOthers" || method=="tab.archiveBelow" || method=="tab.clearToday" || method=="tab.moveToNewSpace";
  const bool mute=method=="tab.mute", duplicate=method=="tab.duplicate";
  const bool integration=method=="integration.preview";
  const bool x_badge=method=="tab.setXBadgeHidden";
  const bool toggle_integration=method=="tab.setIntegration";
  const bool preload=method=="tab.preload";
  const bool library=method=="library.setOpen" || method=="library.setSection";
  const bool creating=method=="space.startCreate" || method=="space.createDraft" || method=="space.cancelDraft";
  const bool confirm_delete=method=="space.confirmDelete";
  const bool folder_icon=method=="folder.chooseIcon";
  const bool folder_delete=method=="folder.confirmDelete";
  if(!library && !creating && !confirm_delete && !folder_icon && !folder_delete && !new_tab && !drag && !batch && !edit_pinned && !confirm_move && !mute && !duplicate && !integration && !x_badge && !toggle_integration && !preload && method!="browser.restartToUpdate" && method!="settings.openProfiles")return false;
  if(!s || !s->workspace || persistent){cb->Failure(400,"Invalid sidebar action");return true;}
  if(s->workspace->WindowClosing()){cb->Failure(409,"Window is closing");return true;}
  if(s->probe) {
    auto event=CefDictionaryValue::Create();event->SetString("method",method);
    event->SetString("surface",source && source->GetIdentifier()==s->peek_id ? "floating":"docked");
    s->Record("shared_sidebar_action",event);
  }
  const auto respond=[s,cb]{Reply(cb,s->workspace->Snapshot());};
  if (library) {
    const bool opening = method == "library.setOpen";
    if (d->GetSize() != 2 || (opening ? d->GetType("open") != VTYPE_BOOL : d->GetType("section") != VTYPE_STRING))
      cb->Failure(400, "Invalid Library action");
    else if (!(opening ? s->workspace->SetLibraryOpen(d->GetBool("open")) :
                          s->workspace->SetLibrarySection(d->GetString("section").ToString())))
      cb->Failure(409, "Library action is unavailable in this window");
    else respond();
    return true;
  }
  if (confirm_delete) {
    if (d->GetSize() != 2 || d->GetType("id") != VTYPE_STRING || d->GetString("id").length() != 36) { cb->Failure(400, "Invalid Space command"); return true; }
    if (!s->panel_popup || !s->panel_popup->ConfirmSpaceDelete(d->GetString("id").ToString())) cb->Failure(409, "Delete confirmation is unavailable");
    else respond();
    return true;
  }
  if (folder_delete) {
    // A folder with contents: the window's modal asks first (an empty one is removed directly).
    const bool floating = source && source->GetIdentifier() == s->peek_id;
    if (d->GetSize() != 2 || d->GetType("id") != VTYPE_STRING || d->GetString("id").length() != 36) { cb->Failure(400, "Invalid folder command"); return true; }
    if (!s->panel_popup || (floating ? !s->sidebar_peek || !s->sidebar_peek->is_open() : !s->sidebar_visible) ||
        !s->panel_popup->ConfirmFolderDelete(d->GetString("id").ToString())) cb->Failure(409, "Delete confirmation is unavailable");
    else respond();
    return true;
  }
  if (folder_icon) {
    // The folder's glyph picker (docs/folder-icons.md), hanging from its row in this host.
    CefRect anchor;
    const bool floating = source && source->GetIdentifier() == s->peek_id;
    if (d->GetSize() != 6 || d->GetType("id") != VTYPE_STRING || d->GetString("id").length() != 36 ||
        !SidebarAnchor(*s, source, d, 480, 120, anchor)) { cb->Failure(400, "Invalid folder icon command"); return true; }
    if (!s->panel_popup || (floating ? !s->sidebar_peek || !s->sidebar_peek->is_open() : !s->sidebar_visible) ||
        !s->panel_popup->ChooseFolderIcon(d->GetString("id").ToString(), anchor)) cb->Failure(409, "Folder icon picker is unavailable");
    else respond();
    return true;
  }
  if (creating) {
    // The sidebar's Create Space form (both hosts). Its theme is previewed on the
    // window by the spaceTheme editor; Create keeps that preview as the new
    // Space's theme, Cancel reverts it.
    if (s->standalone_window) { cb->Failure(403, "Spaces are unavailable in this window"); return true; }
    const auto close_theme = [s] { if (s->panel_popup && s->panel_popup->is_open() && s->panel_popup->kind() == "spaceTheme") s->panel_popup->Hide(false); };
    if (method == "space.startCreate") {
      if (d->GetSize() != 1) { cb->Failure(400, "Invalid Space command"); return true; }
      s->workspace->RequestSidebar("newSpaceFromLibrary"); respond(); return true;
    }
    if (method == "space.cancelDraft") {
      if (d->GetSize() != 3 || d->GetType("themed") != VTYPE_BOOL || d->GetType("library") != VTYPE_BOOL) { cb->Failure(400, "Invalid Space command"); return true; }
      close_theme();
      if (d->GetBool("themed")) s->workspace->RevertThemePreview();
      // Started from Library's +: back to Library.
      if (d->GetBool("library")) s->workspace->SetLibraryOpen(true);
      respond(); return true;
    }
    const bool profile = d->HasKey("profileId");
    if (d->GetSize() != (profile ? 6U : 5U) || d->GetType("title") != VTYPE_STRING || d->GetType("icon") != VTYPE_STRING ||
        d->GetType("themed") != VTYPE_BOOL || d->GetType("library") != VTYPE_BOOL ||
        (profile && d->GetType("profileId") != VTYPE_STRING)) { cb->Failure(400, "Invalid Space command"); return true; }
    auto command = CefDictionaryValue::Create();
    command->SetString("method", "space.create"); command->SetString("title", d->GetString("title")); command->SetString("icon", d->GetString("icon"));
    if (profile) command->SetString("profileId", d->GetString("profileId"));
    command->SetDouble("expectedRevision", s->workspace->tree_revision());
    close_theme();
    const Theme theme = s->theme; const bool themed = d->GetBool("themed"), to_library = d->GetBool("library");
    const auto before = s->workspace->Snapshot()->GetString("activeSpace").ToString();
    s->workspace->Handle(command, [weak = std::weak_ptr<State>(s), cb, theme, themed, to_library, before](int code, const std::string& error, CefRefPtr<CefDictionaryValue>) {
      const auto state = weak.lock(); if (!state || !state->workspace) return;
      if (code) { cb->Failure(code, error); return; }
      const auto created = state->workspace->Snapshot()->GetString("activeSpace").ToString();
      if (created.empty() || created == before) { cb->Failure(500, "The new space did not open"); return; }
      // Started from Library's +: back to Library, where the new column now is.
      const auto finish = [weak, cb, to_library] {
        const auto state = weak.lock(); if (!state || !state->workspace) return;
        if (to_library) state->workspace->SetLibraryOpen(true);
        Reply(cb, state->workspace->Snapshot());
      };
      if (!themed) { finish(); return; }
      state->workspace->SetTheme(theme, true, [cb, finish](int code, const std::string& error, CefRefPtr<CefDictionaryValue>) {
        if (code) cb->Failure(code, error); else finish();
      }, created);
    });
    return true;
  }
  if (preload) {
    // A hint, not a command: no snapshot in reply, and anything not eligible is ignored.
    if (d->GetSize()!=2 || d->GetType("id")!=VTYPE_STRING || d->GetString("id").length()!=36) { cb->Failure(400,"Invalid preload"); return true; }
    s->workspace->PreloadSaved(d->GetString("id").ToString());
    cb->Success("{}"); return true;
  }
  if (toggle_integration) {
    if (s->private_window || s->standalone_window || d->GetSize()!=4 || d->GetType("id")!=VTYPE_STRING ||
        d->GetString("id").length()!=36 || d->GetType("enabled")!=VTYPE_BOOL || d->GetType("provider")!=VTYPE_STRING ||
        !FindIntegrationSetting(d->GetString("provider").ToString())) { cb->Failure(400,"Invalid integration command"); return true; }
    const auto snapshot = s->workspace->Snapshot();
    const auto nodes = snapshot->GetList("nodes");
    for (size_t i = 0; nodes && i < nodes->GetSize(); ++i) {
      const auto node = nodes->GetDictionary(i);
      if (node->GetString("id") != d->GetString("id") || !node->GetBool("pinned") || node->GetBool("archived") || node->GetBool("closing")) continue;
      const auto id = IntegrationForUrl(node->GetString("url").ToString());
      if (id != d->GetString("provider").ToString()) break;
      SaveIntegrationPreference(s, id, d->GetBool("enabled"), [weak = std::weak_ptr<State>(s), cb](const std::string& error) {
        if (const auto state = weak.lock(); error.empty() && state && state->workspace) Reply(cb, state->workspace->Snapshot());
        else cb->Failure(409, error.empty() ? "Workspace is unavailable" : error);
      });
      return true;
    }
    cb->Failure(404,"No integration for this pin"); return true;
  }
  if(integration) {
    const auto reject=[s,cb](int code,const char* reason){
      if(s->probe){auto event=CefDictionaryValue::Create();event->SetString("reason",reason);s->Record("integration_route_rejected",event);}
      cb->Failure(code,reason);return true;
    };
    if(d->GetSize()!=6 || d->GetType("id")!=VTYPE_STRING || d->GetString("id").length()!=36) {cb->Failure(400,"Invalid integration anchor");return true;}
    for(const auto* key:{"x","y","width","height"})if(d->GetType(key)!=VTYPE_INT || d->GetInt(key)<0 || d->GetInt(key)>16384){cb->Failure(400,"Invalid integration bounds");return true;}
    const bool floating=source && source->GetIdentifier()==s->peek_id;
    if(!source || !s->window || !s->panel_popup ||
       (floating ? !s->sidebar_peek || !s->sidebar_peek->is_open() : !s->sidebar_visible)) return reject(409,"Sidebar is inactive");
    const auto handle=source->GetHost()->GetWindowHandle(); const auto root=s->window->GetWindowHandle();
    POINT origin{}, parent{}; RECT client{}; const float scale=GetDpiForWindow(root)/96.f;
    const int x=d->GetInt("x"),y=d->GetInt("y"),w=d->GetInt("width"),h=d->GetInt("height");
    if(!handle || !GetClientRect(handle,&client) || !ClientToScreen(handle,&origin) || !ClientToScreen(root,&parent) ||
       w<1 || h<1 || w>480 || h>200 || (x+w)*scale>client.right+2 || (y+h)*scale>client.bottom+2) return reject(400,"Integration anchor is outside sidebar");
    const CefRect anchor(x+static_cast<int>((origin.x-parent.x)/scale),y+static_cast<int>((origin.y-parent.y)/scale),w,h);
    if(!s->panel_popup->PreviewIntegration(d->GetString("id"),anchor,handle))return reject(409,"Integration is unavailable");
    respond();return true;
  }
  if(method=="browser.restartToUpdate") {
    if(d->GetSize()!=1){cb->Failure(400,"Invalid update command");return true;}
    if(!RequestUpdateRestart(s)){cb->Failure(409,"No update is ready");return true;}
    respond();return true;
  }
  if(method=="settings.openProfiles") {
    if(d->GetSize()!=1){cb->Failure(400,"Invalid profile settings command");return true;}
    ShowProfileSettings(s,true);respond();return true;
  }
  if(new_tab) {
    if(d->GetSize()!=2){cb->Failure(400,"Invalid shortcut command");return true;}
    ExecuteShortcut(s,Shortcut::NewTab);respond();
    if(source && source->GetIdentifier()==s->peek_id && s->sidebar_peek)s->sidebar_peek->Hide();
    return true;
  }
  if(drag) {
    s->workspace->HandleTabDrag(d,[cb](int code,const std::string& error,CefRefPtr<CefDictionaryValue> snapshot){
      if(code)cb->Failure(code,error);else Reply(cb,snapshot);
    });return true;
  }
  if(confirm_move) {
    // Moving live pages to a Space of another profile: the window's modal asks first.
    if(d->GetSize()!=3 || d->GetType("id")!=VTYPE_STRING || d->GetString("id").length()!=36 ||
       d->GetType("space")!=VTYPE_STRING || d->GetString("space").length()!=36) { cb->Failure(400,"Invalid move command");return true; }
    const bool floating = source && source->GetIdentifier() == s->peek_id;
    if (!source || !s->panel_popup || (floating ? !s->sidebar_peek || !s->sidebar_peek->is_open() : !s->sidebar_visible) ||
        !s->panel_popup->ConfirmProfileMove(d->GetString("id").ToString(),d->GetString("space").ToString()))
      cb->Failure(409,"Move confirmation is unavailable");
    else respond();
    return true;
  }
  if(x_badge) {
    if(d->GetSize()!=3 || d->GetType("id")!=VTYPE_STRING || d->GetString("id").length()!=36 || d->GetType("hidden")!=VTYPE_BOOL) {
      cb->Failure(400,"Invalid X badge command");return true;
    }
    s->workspace->SetXBadgeHidden(d->GetString("id"),d->GetBool("hidden"),[cb](int code,const std::string& error,CefRefPtr<CefDictionaryValue> snapshot) {
      if(code)cb->Failure(code,error);else Reply(cb,snapshot);
    });return true;
  }
  if(d->GetSize()!=(mute?3u:2u) || d->GetType("id")!=VTYPE_STRING || d->GetString("id").length()!=36 || (mute && d->GetType("muted")!=VTYPE_BOOL)){
    cb->Failure(400,"Invalid tab command");return true;
  }
  std::string error;const auto id=d->GetString("id").ToString();
  if (edit_pinned) {
    const bool floating = source && source->GetIdentifier() == s->peek_id;
    if (!source || !s->panel_popup || (floating ? !s->sidebar_peek || !s->sidebar_peek->is_open() : !s->sidebar_visible) ||
        !s->panel_popup->EditPinnedPage(id)) cb->Failure(409, "Pinned page editor is unavailable");
    else respond();
    return true;
  }
  const int code=method=="tab.copyLink" ? s->workspace->CopyTabLink(id,error)
      : home ? s->workspace->SetPinnedPage(id,"","",error)
      : method=="tab.moveToNewSpace" ? s->workspace->MoveToNewSpace(id,error)
      : mute ? s->workspace->SetMuted(id,d->GetBool("muted"),error)
      : duplicate ? s->workspace->Duplicate(id,error)
      : s->workspace->ArchiveTodayTabs(id,method=="tab.archiveBelow",error,method=="tab.clearToday");
  if(code)cb->Failure(code,error);else respond();return true;
}
}
