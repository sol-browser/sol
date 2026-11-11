#include "experiments/integration_live_check.h"
#include "integrations/live_reader.h"
#include "include/cef_command_line.h"
#include "include/cef_devtools_message_observer.h"
#include "include/cef_parser.h"
#include "include/base/cef_callback.h"
#include "include/wrapper/cef_closure_task.h"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <map>

namespace red {
namespace {
// Explicitly enabled only in the user's isolated account-test profile. Never
// copy arbitrary page strings to a diagnostic artifact, even in this mode.
bool Enabled(const std::shared_ptr<State>& s) {
  return s && !s->probe && !s->private_window && s->profile.filename()==L"profile" &&
      s->profile.parent_path().filename()==L"integrations-account-review" && s->profile.parent_path().parent_path().filename()==L"artifacts" &&
      CefCommandLine::GetGlobalCommandLine()->HasSwitch("integration-diagnostics");
}
void Write(const std::shared_ptr<State>& s,CefRefPtr<CefDictionaryValue> data) {
  if(!Enabled(s))return;
  auto v=CefValue::Create();v->SetDictionary(data);
  std::ofstream out(s->profile.parent_path()/"live-structure.jsonl",std::ios::binary|std::ios::app);
  out<<CefWriteJSON(v,JSON_WRITER_DEFAULT).ToString()<<'\n';
}
std::string Service(const std::string& url) {
  const auto a=IntegrationUrl(url);if(!a)return {};
  if(a->host=="mail.google.com")return "gmail";
  if(a->host=="calendar.google.com")return "googleCalendar";
  if(a->host=="outlook.live.com"||a->host=="outlook.office.com") {
    if(a->path=="/mail"||a->path.starts_with("/mail/"))return "outlookMail";
    if(a->path=="/calendar"||a->path.starts_with("/calendar/"))return "outlookCalendar";
  }
  return {};
}
// Fixed selectors and numeric counts only. No HTML, URL, subjects, event
// titles, addresses, credentials, tokens or attributes' values leave the page.
const std::map<std::string,std::string> kSelectors{
  {"main","[role=main],main"},{"grid","[role=grid]"},{"listbox","[role=listbox]"},{"options","[role=option]"},
  {"gmailRows","tr.zA"},{"gmailUnread","tr.zA.zE"},{"gmailLegacy","tr.zA [data-legacy-thread-id],tr.zA[data-legacy-thread-id]"},
  {"gmailModern","tr.zA [data-thread-id],tr.zA[data-thread-id]"},{"gmailCompose","[gh=cm]"},
  {"calendarEventId","[data-eventid]"},{"calendarEventLinks","[data-eventid] a[href]"},{"calendarStart","[data-start-time],[data-start-date],time[datetime]"},
  {"outlookItemId","[data-itemid],[data-item-id],[data-message-id]"},{"outlookConversationId","[data-convid],[data-conversation-id]"},
  {"outlookUnreadFlag","[data-unread=true],[data-is-unread=true],[data-isread=false]"},
  {"outlookUnreadLabel","[role=option][aria-label^=Unread i],[role=option][aria-label^=Непрочитан i],[role=option][aria-label^=Непрочитане i]"},
  {"outlookSubjectField","[data-subject],[data-testid=message-subject],[data-automationid=messageSubject]"},
  {"outlookSenderField","[data-sender],[data-testid=message-sender],[data-automationid=messageSender]"},
  {"outlookEventId","[data-eventid],[data-event-id],[data-itemid]"},{"meetAnchors","a[href^='https://meet.google.com/']"}
};
class StructureObserver final : public CefDevToolsMessageObserver {
 public:
  StructureObserver(std::weak_ptr<State> state,int browser,std::string source,std::string service)
      :state_(std::move(state)),browser_(browser),source_(std::move(source)),service_(std::move(service)){}
  void OnDevToolsMethodResult(CefRefPtr<CefBrowser> browser,int id,bool success,const void* bytes,size_t size)override {
    if(!success||id!=query||browser->GetIdentifier()!=browser_||!size||size>8192)return;
    CefPostTask(TID_UI,base::BindOnce([](std::weak_ptr<State> weak,CefRefPtr<CefBrowser> browser,std::string source,std::string service,std::string json){
      const auto s=weak.lock();if(!Enabled(s)||!browser->IsValid()||browser->GetMainFrame()->GetURL()!=source)return;
      const auto parsed=CefParseJSON(json,JSON_PARSER_RFC);const auto result=parsed&&parsed->GetType()==VTYPE_DICTIONARY?parsed->GetDictionary()->GetDictionary("result"):nullptr;
      const auto value=result?result->GetDictionary("value"):nullptr;if(!value)return;
      auto data=CefDictionaryValue::Create();data->SetString("service",service);data->SetString("phase","structure");
      data->SetBool("readerRoute",LiveIntegration(source).has_value());
      for(const auto& [name,selector]:kSelectors)if(value->GetType(name)==VTYPE_INT)data->SetInt(name,std::clamp(value->GetInt(name),0,2000));
      Write(s,data);
    },state_,browser,source_,service_,std::string(static_cast<const char*>(bytes),size)));
  }
  int query=0;
 private:
  std::weak_ptr<State> state_;
  int browser_;
  std::string source_,service_;
  IMPLEMENT_REFCOUNTING(StructureObserver);
};
void Inspect(std::weak_ptr<State> weak,CefRefPtr<CefBrowser> browser) {
  const auto s=weak.lock();if(!Enabled(s)||!browser->IsValid()||!browser->GetMainFrame())return;
  const auto source=browser->GetMainFrame()->GetURL().ToString(),service=Service(source);if(service.empty())return;
  auto selectors=CefDictionaryValue::Create();for(const auto& [name,selector]:kSelectors)selectors->SetString(name,selector);
  auto value=CefValue::Create();value->SetDictionary(selectors);
  auto params=CefDictionaryValue::Create();params->SetBool("returnByValue",true);params->SetInt("timeout",800);
  params->SetString("expression","(()=>{const selectors="+CefWriteJSON(value,JSON_WRITER_DEFAULT).ToString()+R"JS(;
    const out={};for(const [key,selector] of Object.entries(selectors))out[key]=Math.min(2000,[...document.querySelectorAll(selector)].filter(e=>e.getClientRects().length).length);return out;})())JS");
  CefRefPtr<StructureObserver> observer=new StructureObserver(weak,browser->GetIdentifier(),source,service);
  const auto registration=browser->GetHost()->AddDevToolsMessageObserver(observer);
  observer->query=browser->GetHost()->ExecuteDevToolsMethod(0,"Runtime.evaluate",params);
  CefPostDelayedTask(TID_UI,base::BindOnce([](CefRefPtr<CefRegistration>){},registration),3000);
  if(const auto provider=LiveIntegration(source)) {
    auto read=LiveRead::Start(browser,*provider,source,[weak,service](CefRefPtr<CefDictionaryValue> result){
      const auto s=weak.lock();if(!Enabled(s))return;
      auto data=CefDictionaryValue::Create();data->SetString("service",service);data->SetString("phase","reader");
      data->SetString("status",result&&result->GetString("status")=="ready"?"ready":"unsupported");
      data->SetInt("rows",result?static_cast<int>(result->GetList("items")->GetSize()):0);
      data->SetBool("composeAvailable",result&&result->GetBool("composeAvailable"));Write(s,data);
    });
    CefPostDelayedTask(TID_UI,base::BindOnce([](std::shared_ptr<LiveRead>){},std::move(read)),2700);
  }
}
using Clock = std::chrono::steady_clock;
// UI thread only. A single pending timer per browser also covers changes
// between Outlook's mail and calendar views without a document load.
std::map<int,Clock::time_point> deadlines;
void InspectWhenSettled(std::weak_ptr<State> weak,CefRefPtr<CefBrowser> browser,int id) {
  const auto it=deadlines.find(id);if(it==deadlines.end())return;
  if(!browser->IsValid()||!Enabled(weak.lock())){deadlines.erase(it);return;}
  const auto now=Clock::now();
  if(now<it->second) {
    const auto remaining=std::chrono::ceil<std::chrono::milliseconds>(it->second-now).count();
    CefPostDelayedTask(TID_UI,base::BindOnce(&InspectWhenSettled,weak,browser,id),remaining);
    return;
  }
  deadlines.erase(it);Inspect(weak,browser);
}
}
bool IntegrationLiveCheckEnabled(const std::shared_ptr<State>& state) { return Enabled(state); }
void ScheduleIntegrationLiveCheck(const std::shared_ptr<State>& state,CefRefPtr<CefBrowser> browser) {
  if(!Enabled(state))return;
  const auto frame=browser->GetMainFrame();
  if(!frame||Service(frame->GetURL()).empty())return;
  const auto id=browser->GetIdentifier();
  const auto [it,inserted]=deadlines.insert_or_assign(id,Clock::now()+std::chrono::milliseconds(4500));
  if(inserted)CefPostDelayedTask(TID_UI,base::BindOnce(&InspectWhenSettled,std::weak_ptr<State>(state),browser,id),4500);
}
void RecordIntegrationPreviewCheck(const std::shared_ptr<State>& state, const std::string& service,
                                   CefRefPtr<CefDictionaryValue> result, bool foreground) {
  if (!Enabled(state) || !result) return;
  auto data=CefDictionaryValue::Create();data->SetString("service",service);data->SetString("phase",foreground?"foreground":"background");
  data->SetString("status",result->GetString("status"));
  data->SetInt("rows",static_cast<int>(result->GetList("items")->GetSize()));
  data->SetBool("composeAvailable",result->GetBool("composeAvailable"));
  if (result->HasKey("mailView")) data->SetString("mailView",result->GetString("mailView"));
  if (service=="twitter") data->SetInt("httpStatus",result->GetInt("httpStatus"));
  if ((service=="twitter" || service=="whatsapp") && result->HasKey("unread")) data->SetInt("unread",result->GetInt("unread"));
  if (service=="googleCalendar") {
    const auto items=result->GetList("items");
    int timed=0;
    for(size_t i=0;i<items->GetSize();++i) if (items->GetDictionary(i)->HasKey("startMinute")) ++timed;
    data->SetInt("timedEvents",timed);
  }
  if (service=="outlookMail") {
    const auto items=result->GetList("items");
    int named=0, with_sender=0;
    for(size_t i=0;i<items->GetSize();++i) {
      const auto item=items->GetDictionary(i);
      const auto title=item->GetString("title");
      if (title!="Unread conversation" && title!="Conversation") {
        ++named;
        if (!item->GetString("detail").empty()) ++with_sender;
      }
    }
    data->SetInt("rowsWithMetadata",named);
    data->SetInt("rowsWithSubjectAndSender",with_sender);
    if (result->GetType("outlookShape") == VTYPE_LIST)
      data->SetList("outlookShape",result->GetList("outlookShape")->Copy());
  }
  Write(state,data);
}
}
