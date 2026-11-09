#include "windows/sidebar_actions.h"
#include "app/updates.h"
#include "app/session.h"
#include "navigation/shortcuts.h"
#include "windows/settings_window.h"
#include "windows/sidebar_peek.h"
#include "windows/panel_popup.h"
#include "windows/focus.h"
#include "workspace/controller.h"
#include "include/cef_parser.h"
namespace red {
namespace {
void Reply(CefRefPtr<CefMessageRouterBrowserSide::Callback> callback,CefRefPtr<CefDictionaryValue> snapshot) {
  auto value=CefValue::Create();value->SetDictionary(snapshot);callback->Success(CefWriteJSON(value,JSON_WRITER_DEFAULT));
}
}
bool HandleSidebarAction(const std::shared_ptr<State>& s,CefRefPtr<CefBrowser> source,const std::string& method,
    CefRefPtr<CefDictionaryValue> d,bool persistent,CefRefPtr<CefMessageRouterBrowserSide::Callback> cb) {
  const bool new_tab=method=="browser.shortcut" && d->GetType("action")==VTYPE_STRING && d->GetString("action")=="newTab";
  const bool drag=method=="tab.detach" || method=="tab.dragStarted" || method=="tab.dragCancel" || method=="tab.dragEnded";
  const bool home=method=="tab.setHome";
  const bool batch=home || method=="tab.copyLink" || method=="tab.archiveOthers" || method=="tab.archiveBelow" || method=="tab.clearToday" || method=="tab.moveToNewSpace";
  const bool mute=method=="tab.mute", duplicate=method=="tab.duplicate";
  const bool integration=method=="integration.preview";
  const bool x_badge=method=="tab.setXBadgeHidden";
  const bool toggle_integration=method=="tab.setIntegration";
  if(!new_tab && !drag && !batch && !mute && !duplicate && !integration && !x_badge && !toggle_integration && method!="browser.restartToUpdate" && method!="settings.openProfiles")return false;
  if(!s || !s->workspace || persistent){cb->Failure(400,"Invalid sidebar action");return true;}
  if(s->workspace->WindowClosing()){cb->Failure(409,"Window is closing");return true;}
  if(s->probe) {
    auto event=CefDictionaryValue::Create();event->SetString("method",method);
    event->SetString("surface",source && source->GetIdentifier()==s->peek_id ? "floating":"docked");
    s->Record("shared_sidebar_action",event);
  }
  const auto respond=[s,cb]{Reply(cb,s->workspace->Snapshot());};
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
  if(x_badge) {
    if(d->GetSize()!=3 || d->GetType("id")!=VTYPE_STRING || d->GetString("id").length()!=36 || d->GetType("hidden")!=VTYPE_BOOL) {
      cb->Failure(400,"Invalid X badge command");return true;
    }
    s->workspace->SetXBadgeHidden(d->GetString("id"),d->GetBool("hidden"),[cb](int code,const std::string& error,CefRefPtr<CefDictionaryValue> snapshot) {
      if(code)cb->Failure(code,error);else Reply(cb,snapshot);
    });return true;
  }
  const bool edited=home && d->GetSize()==4 && d->GetType("url")==VTYPE_STRING && d->GetType("title")==VTYPE_STRING;
  const bool fields=batch ? d->GetSize()==2 || edited : d->GetSize()==(mute?3u:2u);
  if(!fields || d->GetType("id")!=VTYPE_STRING || d->GetString("id").length()!=36 || (mute && d->GetType("muted")!=VTYPE_BOOL)){
    cb->Failure(400,"Invalid tab command");return true;
  }
  std::string error;const auto id=d->GetString("id").ToString();
  const int code=method=="tab.copyLink" ? s->workspace->CopyTabLink(id,error)
      : home ? s->workspace->SetPinnedPage(id,edited?d->GetString("url").ToString():"",edited?d->GetString("title").ToString():"",error)
      : method=="tab.moveToNewSpace" ? s->workspace->MoveToNewSpace(id,error)
      : mute ? s->workspace->SetMuted(id,d->GetBool("muted"),error)
      : duplicate ? s->workspace->Duplicate(id,error)
      : s->workspace->ArchiveTodayTabs(id,method=="tab.archiveBelow",error,method=="tab.clearToday");
  if(code)cb->Failure(code,error);else respond();return true;
}
}
