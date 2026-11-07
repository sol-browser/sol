#include "windows/sidebar_actions.h"
#include "app/updates.h"
#include "navigation/shortcuts.h"
#include "windows/settings_window.h"
#include "windows/sidebar_peek.h"
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
  if(!new_tab && !drag && !batch && !mute && !duplicate && method!="browser.restartToUpdate" && method!="settings.openProfiles")return false;
  if(!s || !s->workspace || persistent){cb->Failure(400,"Invalid sidebar action");return true;}
  if(s->workspace->WindowClosing()){cb->Failure(409,"Window is closing");return true;}
  if(s->probe) {
    auto event=CefDictionaryValue::Create();event->SetString("method",method);
    event->SetString("surface",source && source->GetIdentifier()==s->peek_id ? "floating":"docked");
    s->Record("shared_sidebar_action",event);
  }
  const auto respond=[s,cb]{Reply(cb,s->workspace->Snapshot());};
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
