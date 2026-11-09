#include "experiments/google_integrations_probe.h"
#include "integrations/live_reader.h"
#include "integrations/integration_previews.h"
#include "windows/panel_popup.h"
#include "windows/command_actions.h"
#include "workspace/controller.h"
#include "include/wrapper/cef_stream_resource_handler.h"
#include "include/base/cef_callback.h"
#include "include/wrapper/cef_closure_task.h"
#include <cstring>
namespace red {
namespace {
constexpr char kMail[]="https://mail.google.com/mail/u/0/?sol-owned-integration-fixture=1#inbox";
constexpr char kReadMail[]="https://mail.google.com/mail/u/0/?sol-owned-integration-inbox=recent#inbox";
constexpr char kSearchMail[]="https://mail.google.com/mail/u/0/?sol-owned-integration-search=1#search/in%3Ainbox+is%3Aunread";
constexpr char kSecondInbox[]="https://mail.google.com/mail/u/1/?sol-owned-integration-inbox=1#inbox";
constexpr char kEmptySearch[]="https://mail.google.com/mail/u/1/?sol-owned-integration-empty=1#search/in%3Ainbox+is%3Aunread";
constexpr char kRecentSearch[]="https://mail.google.com/mail/u/1/?sol-owned-integration-recent=1#search/in%3Ainbox";
constexpr char kRecentSearchDefault[]="https://mail.google.com/mail/u/0/?sol-owned-integration-recent=1#search/in%3Ainbox";
constexpr char kCalendar[]="https://calendar.google.com/calendar/u/0/r/day?sol-owned-integration-fixture=1";
constexpr char kOutlook[]="https://outlook.live.com/mail/0/inbox?sol-owned-integration-fixture=1";
constexpr char kOutlookRoot[]="https://outlook.live.com/mail/?sol-owned-integration-fixture=1";
constexpr char kOutlookSignedOut[]="https://outlook.live.com/mail/?sol-owned-integration-sign-in=1";
constexpr char kOutlookRestored[]="https://outlook.live.com/mail/0/inbox?sol-owned-integration-restored=1";
constexpr char kOutlookSignInHtml[]=R"HTML(<!doctype html><title>Owned expired Outlook session</title>
<script>location.replace('https://login.live.com/')</script>)HTML";
constexpr char kWhatsAppHtml[]=R"HTML(<!doctype html><title>WhatsApp</title>
<script>setTimeout(() => document.title = '(3) WhatsApp', 100)</script>)HTML";
constexpr char kMailHtml[]=R"HTML(<!doctype html><html><title>Owned Gmail fixture</title><main role="main"><button gh="cm">Compose</button><table role="grid" class="F"><tbody>
<tr class="zA zE" data-legacy-thread-id="1234567890abcdef"><td class="yW"><span class="zF">Owned sender</span></td><td><span class="bog">Owned unread thread</span></td><td class="xW"><span title="Monday 10:00">10:00</span></td></tr>
<tr class="zA" data-legacy-thread-id="abcdef1234567890"><td><span class="bog">Read thread excluded</span></td></tr>
<tr class="zA zE" style="display:none" data-legacy-thread-id="1122334455667788"><td><span class="bog">Hidden thread excluded</span></td></tr>
<tr class="zA zE" data-legacy-thread-id="javascript:alert(1)"><td><span class="bog">Bad token excluded</span></td></tr>
</tbody></table></main></html>)HTML";
constexpr char kSearchMailHtml[]=R"HTML(<!doctype html><title>Owned cross-category Gmail search</title>
<input name="q" value="in:inbox is:unread"><main role="main"><table role="grid"><tbody>
<tr class="zA zE" data-legacy-thread-id="1234567890abcd01"><td><span class="bog">Owned primary thread</span></td></tr>
<tr class="zA zE" data-legacy-thread-id="1234567890abcd02"><td><span class="bog">Owned updates thread</span></td></tr>
<tr class="zA zE" data-legacy-thread-id="1234567890abcd03"><td><span class="bog">Owned promotions thread</span></td></tr>
</tbody></table></main>)HTML";
constexpr char kReadMailHtml[]=R"HTML(<!doctype html><title>Owned read Gmail fixture</title><main role="main">
<button gh="cm">Compose</button><table role="grid" class="F"><tbody>
<tr class="zA" data-legacy-thread-id="abcdef1234567890"><td class="yW"><span class="zG">Owned sender</span></td>
<td><span class="bog">Owned recent thread</span><span class="y2">Private preview excluded</span></td></tr>
</tbody></table></main>)HTML";
constexpr char kEmptySearchHtml[]=R"HTML(<!doctype html><title>Owned empty Gmail search</title>
<input name="q" value="in:inbox is:unread"><main role="main"><table role="grid"></table></main>)HTML";
constexpr char kRecentSearchHtml[]=R"HTML(<!doctype html><title>Owned recent Gmail search</title>
<input name="q" value="in:inbox"><main role="main"><table role="grid"><tbody>
<tr class="zA" data-legacy-thread-id="abcdef1234567890"><td><span class="bog">Owned read updates thread</span></td></tr>
</tbody></table></main>)HTML";
constexpr char kCalendarHtml[]=R"HTML(<!doctype html><html><meta charset="utf-8"><title>Owned calendar fixture</title><main role="main"><div role="grid">
<button data-eventid="owned_event_1" aria-label="10:00 AM – 11:15 AM, Owned meeting"><a href="https://meet.google.com/abc-defg-hij">Join</a></button>
<button data-eventid="owned_event_2" aria-label="Owned second event at 14:00"><a href="https://meet.google.com.evil.test/fake">Spoofed join excluded</a></button>
<button data-eventid="owned_event_3" aria-label="Робоча зустріч, з 18:30 до 19:15">Робоча зустріч</button>
</div></main></html>)HTML";
constexpr char kOutlookHtml[]=R"HTML(<!doctype html><meta charset="utf-8"><title>Owned Outlook fixture</title>
<style>
.stacked { font: 600 14px/20px sans-serif; }
.stacked time { font-size: 12px; margin-left: 120px; }
.snippet { font-weight: 400; }
</style>
<main role="main">
<button onclick="this.hidden=true;localStorage.removeItem('sol-owned-opened')">New mail</button><div role="listbox">
<div role="option" data-convid="AAMk/Owned+Row==" aria-label="Unread, Preview must stay in the page"
  onclick="this.hidden=true;localStorage.setItem('sol-owned-opened','1')">
  <span data-testid="message-subject">Owned Outlook subject</span><span data-testid="message-sender">Owned Outlook sender</span>
</div>
<div role="option" data-convid="Read_3" aria-label="Read, already read">Read row</div>
<div role="option" hidden data-convid="Hidden_4" aria-label="Unread, hidden">Hidden row</div>
<div role="option" data-convid="Invalid <token>" aria-label="Unread, malformed">Invalid row</div>
</div><div data-convid="Localized_2"><div role="option" class="stacked" aria-label="Непрочитане Owned local sender Owned local subject Yesterday Private preview">
  <div>
    <div><div><span title="Owned local sender">Owned local sender</span></div></div>
    <div><div><span>Owned local subject</span><time title="Yesterday">Yesterday</time></div></div>
  </div>
  <div class="snippet"><span>Private preview</span></div>
</div></div>
<div role="option" class="stacked" data-convid="Unrecognized_5" aria-label="Unread Owned sender Private preview without a subject slot">
  <div><div><div><span title="Owned sender">Owned sender</span></div></div></div>
  <div><div><div><span>Private preview without a subject slot</span></div></div></div>
</div></main>
<script>if(localStorage.getItem('sol-owned-opened'))document.querySelector('[data-convid="AAMk/Owned+Row=="]').hidden=true</script>)HTML";
bool Enabled(const std::shared_ptr<State>& state) {
  return state && state->probe && state->google_integrations_probe && state->profile.filename()==L"profile" &&
      state->profile.parent_path().filename().wstring().starts_with(L"google-integrations-fixture-") &&
      state->profile.parent_path().parent_path().filename()==L"artifacts";
}
class Fixture final : public CefResourceRequestHandler {
 public:
  explicit Fixture(const char* html):html_(html){}
  CefRefPtr<CefResourceHandler> GetResourceHandler(CefRefPtr<CefBrowser>,CefRefPtr<CefFrame>,CefRefPtr<CefRequest>) override {
    CefResponse::HeaderMap headers;
    // Only these owned documents use scripts for simulated website updates.
    headers.emplace("Content-Security-Policy",html_==kOutlookHtml || html_==kOutlookSignInHtml || html_==kWhatsAppHtml ?
      "default-src 'none'; script-src 'unsafe-inline'; style-src 'unsafe-inline'; base-uri 'none'" :
      "default-src 'none'; style-src 'unsafe-inline'; base-uri 'none'");
    return new CefStreamResourceHandler(200,"OK","text/html",headers,CefStreamReader::CreateForData(const_cast<char*>(html_),std::strlen(html_)));
  }
 private:
  const char* html_;
  IMPLEMENT_REFCOUNTING(Fixture);
};
void Hold(std::shared_ptr<LiveRead> read) { CefPostDelayedTask(TID_UI,base::BindOnce([](std::shared_ptr<LiveRead>){},std::move(read)),2700); }
void Record(const std::shared_ptr<State>& state,const char* event,bool passed) {
  auto details=CefDictionaryValue::Create();details->SetBool("passed",passed);state->Record(event,details);
  state->google_integrations_checks=state->google_integrations_checks&&passed;
}
void Pin(const std::shared_ptr<State>& state, const std::string& node, WorkspaceController::Reply done) {
  auto command=CefDictionaryValue::Create();command->SetString("method","tree.pin");command->SetString("id",node);
  command->SetBool("value",true);command->SetDouble("expectedRevision",state->workspace->Snapshot()->GetDouble("treeRevision"));
  state->workspace->Handle(command,std::move(done));
}
void CheckOutlookRecovery(std::weak_ptr<State> weak, int attempts) {
  const auto s = weak.lock(); if (!s) return;
  const auto cache = s->workspace->integration_previews();
  const auto snapshot = cache ? cache->Cached("https://outlook.live.com/mail/") : nullptr;
  const bool ready = snapshot && snapshot->GetString("status") == "ready" && snapshot->GetList("items")->GetSize() == 2;
  if (!ready && attempts > 0) {
    CefPostDelayedTask(TID_UI,base::BindOnce(&CheckOutlookRecovery,weak,attempts-1),200);
    return;
  }
  Record(s,"outlook_recovers_from_loaded_inbox",ready);
}
void CheckOutlookSignIn(std::weak_ptr<State> weak, int attempts) {
  const auto s = weak.lock(); if (!s) return;
  const auto cache = s->workspace->integration_previews();
  const auto snapshot = cache ? cache->Cached("https://outlook.live.com/mail/") : nullptr;
  const bool sign_in = snapshot && snapshot->GetString("status") == "signInRequired" && !snapshot->GetList("items")->GetSize();
  if (!sign_in && attempts > 0) {
    CefPostDelayedTask(TID_UI,base::BindOnce(&CheckOutlookSignIn,weak,attempts-1),200);
    return;
  }
  Record(s,"outlook_sign_in_redirect_blocked",sign_in);
  if (sign_in) {
    s->google_integrations_phase = 6;
    const auto browser = s->workspace->LiveBrowser(s->workspace->ActiveNode());
    if (browser && browser->GetMainFrame()) browser->GetMainFrame()->LoadURL(kOutlookRestored);
    CheckOutlookRecovery(weak,25);
  }
}
void CheckFavoriteRefresh(std::weak_ptr<State> weak, int attempts) {
  const auto s = weak.lock(); if (!s) return;
  const auto cache = s->workspace->integration_previews();
  const auto snapshot = cache ? cache->Cached("https://outlook.live.com/mail/") : nullptr;
  const auto rows = snapshot ? snapshot->GetList("items") : nullptr;
  const bool refreshed = rows && rows->GetSize() == 2 && rows->GetDictionary(0)->GetString("token") == "Localized_2";
  if (!refreshed && attempts > 0) {
    CefPostDelayedTask(TID_UI,base::BindOnce(&CheckFavoriteRefresh,weak,attempts-1),200);
    return;
  }
  Record(s,"preview_refreshes_after_mail_open",refreshed);
  if (refreshed) {
    s->google_integrations_phase = 5;
    cache->RefreshAfterMailOpen("https://outlook.live.com/mail/");
    CheckOutlookSignIn(weak,25);
  }
}
void CheckFavoriteDestination(std::weak_ptr<State> weak, std::string node, size_t count, int attempts) {
  const auto s=weak.lock();
  if (!s) return;
  const auto browser=s->workspace->LiveBrowser(node);
  if (attempts>0 && (!browser || s->workspace->ActiveNode()!=node || browser->IsLoading())) {
    CefPostDelayedTask(TID_UI,base::BindOnce(&CheckFavoriteDestination,weak,node,count,attempts-1),200);
    return;
  }
  const auto snapshot=s->workspace->Snapshot();
  const auto seed=s->workspace->DetachedSeed(node);
  const auto frame=browser?browser->GetMainFrame():nullptr;
  const auto source=frame?frame->GetURL().ToString():std::string{};
  const auto provider=LiveIntegration(source);
  if (!seed || !seed->favorite || s->workspace->ActiveNode()!=node || !provider ||
      snapshot->GetList("nodes")->GetSize()!=count || !IsPreviewPage("https://outlook.live.com/mail/",source)) {
    Record(s,"preview_opens_original_favorite",false);
    return;
  }
  Hold(LiveRead::Start(browser,*provider,source,[weak,node,count,attempts](CefRefPtr<CefDictionaryValue> value){
    const auto s=weak.lock();
    if (!s) return;
    const auto items=value?value->GetList("items"):nullptr;
    const bool clicked=items && items->GetSize()==2 && items->GetDictionary(0)->GetString("token")=="Localized_2";
    if (!clicked && attempts>0) {
      CefPostDelayedTask(TID_UI,base::BindOnce(&CheckFavoriteDestination,weak,node,count,attempts-1),200);
    } else {
      Record(s,"preview_opens_original_favorite",clicked);
      if (clicked) CheckFavoriteRefresh(weak,30);
    }
  }));
}
void ClickFavoritePreview(std::weak_ptr<State> weak, std::string node, size_t count, int attempts) {
  const auto s=weak.lock();
  if (!s) return;
  const auto panel=s->panel_popup->hover_panel();
  if (!panel || !panel->is_presented() || !panel->browser()) {
    if (attempts>0) CefPostDelayedTask(TID_UI,base::BindOnce(&ClickFavoritePreview,weak,node,count,attempts-1),100);
    else Record(s,"preview_opens_original_favorite",false);
    return;
  }
  const auto frame=panel->browser()->GetMainFrame();
  frame->ExecuteJavaScript(
      "document.querySelector('.integration-item .archive-row')?.click()",frame->GetURL(),0);
  CefPostDelayedTask(TID_UI,base::BindOnce(&CheckFavoriteDestination,weak,node,count,20),200);
}
void PreviewFavorite(const std::shared_ptr<State>& s, const std::string& node, size_t count) {
  const auto shell=s->browsers.find(s->shell_id);
  if (shell==s->browsers.end() || !s->window || !s->panel_popup) {
    Record(s,"preview_opens_original_favorite",false);
    return;
  }
  const auto root=s->window->GetWindowHandle();
  s->window->Activate();
  if (!SetForegroundWindow(root) && !PopupOwnerIsForeground(s->window)) {
    // Use the same disposable-window activation as the sidebar probe.
    INPUT keys[2]{};
    keys[0].type=keys[1].type=INPUT_KEYBOARD;
    keys[0].ki.wVk=keys[1].ki.wVk=VK_MENU;
    keys[1].ki.dwFlags=KEYEVENTF_KEYUP;
    SendInput(2,keys,sizeof(INPUT));
    SetForegroundWindow(root);
  }
  POINT original{};
  const bool restore_cursor=GetCursorPos(&original);
  const auto surface=shell->second->GetHost()->GetWindowHandle();
  POINT origin{}, owner{};
  POINT p{60,116};
  const float scale=GetDpiForWindow(root)/96.f;
  p.x=static_cast<LONG>(p.x*scale);
  p.y=static_cast<LONG>(p.y*scale);
  const bool positioned=ClientToScreen(surface,&p)&&ClientToScreen(surface,&origin)&&ClientToScreen(root,&owner)&&SetCursorPos(p.x,p.y);
  const CefRect anchor(16+static_cast<int>((origin.x-owner.x)/scale),100+static_cast<int>((origin.y-owner.y)/scale),160,32);
  const bool presented=positioned&&s->panel_popup->PreviewIntegration(node,anchor,surface);
  auto evidence=CefDictionaryValue::Create();
  evidence->SetBool("foreground",PopupOwnerIsForeground(s->window));
  evidence->SetBool("positioned",positioned);
  evidence->SetBool("cursor_hit",GetAncestor(WindowFromPoint(p),GA_ROOT)==GetAncestor(surface,GA_ROOT));
  evidence->SetBool("surface_visible",IsWindowVisible(surface)!=FALSE);
  evidence->SetBool("saved_preview",s->workspace->SavedPreview(node)=="https://outlook.live.com/mail/");
  s->Record("preview_favorite_probe_setup",evidence);
  if (!presented) {
    if (restore_cursor) SetCursorPos(original.x,original.y);
    Record(s,"preview_opens_original_favorite",false);
    return;
  }
  ClickFavoritePreview(s,node,count,20);
  if (restore_cursor) CefPostDelayedTask(TID_UI,base::BindOnce([](POINT p){SetCursorPos(p.x,p.y);},original),1400);
}
void CheckBackground(std::weak_ptr<State> weak, std::string outlook_node, int attempts) {
  const auto s=weak.lock();if(!s||!s->workspace)return;
  const auto cache=s->workspace->integration_previews();
  const auto gmail=cache?cache->Cached("https://mail.google.com/mail/u/0/#inbox"):nullptr;
  const auto outlook=cache?cache->Cached("https://outlook.live.com/mail/"):nullptr;
  const auto calendar=cache?cache->Cached("https://calendar.google.com/calendar/u/0/r"):nullptr;
  const auto gmail_recent=cache?cache->Cached("https://mail.google.com/mail/u/1/#inbox"):nullptr;
  if ((!gmail||!outlook||!calendar||!gmail_recent) && attempts>0) {
    CefPostDelayedTask(TID_UI,base::BindOnce(&CheckBackground,weak,outlook_node,attempts-1),400);
    return;
  }
  bool away=false, calendar_unloaded=false;
  const auto snapshot=s->workspace->Snapshot();
  const auto nodes=snapshot->GetList("nodes");
  for(size_t i=0;nodes&&i<nodes->GetSize();++i) {
    const auto row=nodes->GetDictionary(i);
    const auto id=row->GetString("id").ToString();
    if(s->workspace->SavedPreview(id)=="https://mail.google.com/mail/u/0/#inbox") {
      const auto browser=s->workspace->LiveBrowser(id);
      away=browser&&browser->GetMainFrame()->GetURL()==s->initial_url+"?away=mail";
    }
    if(s->workspace->SavedPreview(id)=="https://calendar.google.com/calendar/u/0/r")
      calendar_unloaded=!s->workspace->LiveBrowser(id);
  }
  Record(s,"background_gmail_while_away",away&&gmail&&gmail->GetString("status")=="ready"&&gmail->GetString("mailView")=="unreadAll"&&
      gmail->GetList("items")->GetSize()==3&&gmail->GetList("items")->GetDictionary(1)->GetString("title")=="Owned updates thread");
  Record(s,"background_gmail_recent_across_categories",gmail_recent&&gmail_recent->GetString("status")=="ready"&&
      gmail_recent->GetString("mailView")=="recentAll"&&gmail_recent->GetList("items")->GetSize()==1&&
      gmail_recent->GetList("items")->GetDictionary(0)->GetString("title")=="Owned read updates thread");
  Record(s,"background_outlook_while_unloaded",!s->workspace->LiveBrowser(outlook_node)&&outlook&&
      outlook->GetString("status")=="ready"&&outlook->GetList("items")->GetSize()==3&&
      outlook->GetList("items")->GetDictionary(1)->GetString("title")=="Owned local subject"&&
      outlook->GetList("items")->GetDictionary(1)->GetString("detail")=="Owned local sender");
  Record(s,"background_calendar_while_unloaded",calendar_unloaded&&calendar&&calendar->GetString("status")=="ready"&&
      calendar->GetList("items")->GetSize()==3&&calendar->GetList("items")->GetDictionary(0)->GetInt("startMinute")==600);
  if (outlook && outlook->GetString("status")=="ready") PreviewFavorite(s,outlook_node,nodes->GetSize());
}
void VerifyOutlook(std::weak_ptr<State> weak,CefRefPtr<CefBrowser> browser,LiveProvider provider) {
  Hold(LiveRead::Start(browser,provider,kOutlook,[weak,browser,provider](CefRefPtr<CefDictionaryValue> result){
    const auto state=weak.lock();if(!state)return;
    const auto rows=result?result->GetList("items"):nullptr;
    const bool passed=result&&result->GetString("status")=="ready"&&result->GetBool("composeAvailable")&&rows&&rows->GetSize()==3&&
      rows->GetDictionary(0)->GetString("token")=="AAMk/Owned+Row=="&&rows->GetDictionary(0)->GetString("title")=="Owned Outlook subject"&&
      rows->GetDictionary(0)->GetString("detail")=="Owned Outlook sender"&&
      rows->GetDictionary(1)->GetString("title")=="Owned local subject"&&
      rows->GetDictionary(1)->GetString("detail")=="Owned local sender"&&
      rows->GetDictionary(1)->GetString("displayTime")=="Yesterday"&&
      rows->GetDictionary(2)->GetString("title")=="Unread conversation"&&
      rows->GetDictionary(2)->GetString("detail").empty();
    Record(state,"outlook_mail_reader",passed);
    const auto shape=result?result->GetList("outlookShape"):nullptr;
    Record(state,"outlook_shape_numeric",shape&&shape->GetSize()>=4&&
        shape->GetList(0)->GetSize()==12&&shape->GetList(0)->GetType(7)==VTYPE_INT);
    LiveRead::Click(browser,provider,kOutlook,LiveRead::Action::Open,"AAMk/Owned+Row==",[weak,browser,provider](bool opened){
      const auto state=weak.lock();if(!state)return;Record(state,"outlook_mail_open",opened);
      LiveRead::Click(browser,provider,kOutlook,LiveRead::Action::Open,"AAMk/Owned+Row==",[weak,browser,provider](bool stale){
        const auto state=weak.lock();if(!state)return;Record(state,"outlook_mail_stale_row",!stale);
        LiveRead::Click(browser,provider,kOutlook,LiveRead::Action::Compose,{},[weak,browser,provider](bool composed){
          const auto state=weak.lock();if(!state)return;
          Hold(LiveRead::Start(browser,provider,kOutlook,[weak,composed](CefRefPtr<CefDictionaryValue> after){
            const auto state=weak.lock();if(!state)return;
            const auto rows=after?after->GetList("items"):nullptr;
            Record(state,"outlook_mail_compose",composed&&after&&after->GetString("status")=="ready"&&!after->GetBool("composeAvailable")&&
              rows&&rows->GetSize()==2&&rows->GetDictionary(0)->GetString("token")=="Localized_2");
            const auto node=state->workspace->ActiveNode();
            auto favorite=CefDictionaryValue::Create();
            favorite->SetString("method","tree.place");
            favorite->SetString("id",node);
            favorite->SetString("parentId","");
            favorite->SetInt("position",-1);
            favorite->SetString("section","favorites");
            favorite->SetDouble("expectedRevision",state->workspace->Snapshot()->GetDouble("treeRevision"));
            state->workspace->Handle(favorite,[weak,node](int code,const std::string&,CefRefPtr<CefDictionaryValue>){
              const auto s=weak.lock();if(!s||code)return;
              s->workspace->Close(node,false,[weak,node](int code,const std::string&,CefRefPtr<CefDictionaryValue>){
                if(!code)CheckBackground(weak,node,35);
              });
            });
          }));
        });
      });
    });
  }, true));
}
}
CefRefPtr<CefResourceRequestHandler> IntegrationFixtureResource(const std::shared_ptr<State>& state,const std::string& url) {
  if (state && state->probe && state->sidebar_peek_probe && state->profile.filename()==L"profile" &&
      state->profile.parent_path().parent_path().filename()==L"artifacts") {
    if (url=="https://www.figma.com/files" || url==CommandDocumentUrl("newFigmaDesign"))
      return new Fixture("<!doctype html><title>Owned Figma navigation fixture</title>");
    const auto badge = StartupBadgeFor(url, false);
    if (badge == StartupBadgeSite::X || url=="https://x.com/notifications")
      return new Fixture("<!doctype html><title>(4) Home / X</title>");
    if (badge == StartupBadgeSite::WhatsApp || url=="https://web.whatsapp.com/send") return new Fixture(kWhatsAppHtml);
  }
  if(!Enabled(state))return nullptr;
  if(url.substr(0,url.find('#'))==std::string(kMail).substr(0,std::string(kMail).find('#')))return new Fixture(kMailHtml);
  const auto document=url.substr(0,url.find('#'));
  const auto same_document=[&](std::string_view page) { return document==page.substr(0,page.find('#')); };
  if(same_document(kReadMail)||same_document(kSecondInbox))return new Fixture(kReadMailHtml);
  if(same_document(kEmptySearch))return new Fixture(kEmptySearchHtml);
  if(same_document(kRecentSearch)||same_document(kRecentSearchDefault))return new Fixture(kRecentSearchHtml);
  if(url.substr(0,url.find('#'))==std::string(kSearchMail).substr(0,std::string(kSearchMail).find('#')))return new Fixture(kSearchMailHtml);
  if(url==kCalendar)return new Fixture(kCalendarHtml);
  if(url==kOutlook||url==kOutlookRoot||url==kOutlookRestored)return new Fixture(kOutlookHtml);
  if(url==kOutlookSignedOut)return new Fixture(kOutlookSignInHtml);
  return nullptr;
}
std::string PreviewFixtureUrl(const std::shared_ptr<State>& state, const std::string& canonical) {
  if (!Enabled(state)) return {};
  if (canonical == "https://mail.google.com/mail/u/0/#inbox") return kReadMail;
  if (canonical == "https://mail.google.com/mail/u/1/#inbox") return kSecondInbox;
  if (canonical == GmailInboxSearch("https://mail.google.com/mail/u/0/#inbox",true)) return kSearchMail;
  if (canonical == GmailInboxSearch("https://mail.google.com/mail/u/0/#inbox",false)) return kRecentSearchDefault;
  if (canonical == GmailInboxSearch("https://mail.google.com/mail/u/1/#inbox",true)) return kEmptySearch;
  if (canonical == GmailInboxSearch("https://mail.google.com/mail/u/1/#inbox",false)) return kRecentSearch;
  if (canonical == "https://outlook.live.com/mail/") return state->google_integrations_phase == 5 ? kOutlookSignedOut : kOutlookRoot;
  if (canonical == "https://calendar.google.com/calendar/u/0/r" || canonical == "https://calendar.google.com/calendar/u/0/r/day") return kCalendar;
  return {};
}
void OnGoogleIntegrationsProbeLoad(const std::shared_ptr<State>& state,CefRefPtr<CefBrowser> browser) {
  if(!Enabled(state)||!state->workspace)return;
  const auto url=browser->GetMainFrame()->GetURL().ToString();
  if(state->google_integrations_phase==0 && url==state->initial_url) {
    state->google_integrations_phase=1;state->workspace->OpenLink(kMail,true);return;
  }
  const bool mail=state->google_integrations_phase==1&&url==kMail;
  const bool calendar=state->google_integrations_phase==2&&url==kCalendar;
  if (state->google_integrations_phase==2&&url==kSecondInbox) {
    const auto node=state->workspace->ActiveNode();
    Pin(state,node,[weak=std::weak_ptr<State>(state),node](int code,const std::string&,CefRefPtr<CefDictionaryValue>){
      const auto s=weak.lock();
      if(!s)return;
      if(code){s->workspace->OpenLink(kCalendar,true);return;}
      s->workspace->Close(node,false,[weak](int,const std::string&,CefRefPtr<CefDictionaryValue>){
        if(const auto s=weak.lock())s->workspace->OpenLink(kCalendar,true);
      });
    });
    return;
  }
  if(state->google_integrations_phase==3&&url==kOutlook) {
    ++state->google_integrations_phase;
    if (const auto provider=LiveIntegration(url)) VerifyOutlook(state,browser,*provider);
    return;
  }
  if(!mail&&!calendar)return;
  ++state->google_integrations_phase;
  const auto provider=GoogleIntegration(url);if(!provider)return;
  Hold(LiveRead::Start(browser,*provider,url,[weak=std::weak_ptr<State>(state),browser,mail](CefRefPtr<CefDictionaryValue> value){
    const auto state=weak.lock();if(!state)return;
    const auto items=value?value->GetList("items"):nullptr;
    bool passed=value&&value->GetString("status")=="ready"&&items&&items->GetSize()==(mail?1u:3u);
    if(passed && mail)passed=items->GetDictionary(0)->GetString("title")=="Owned unread thread" && items->GetDictionary(0)->GetString("token")=="1234567890abcdef"&&
        items->GetDictionary(0)->GetString("displayTime")=="10:00"&&value->GetBool("composeAvailable");
    if(passed && !mail)passed=items->GetDictionary(0)->GetString("join")=="https://meet.google.com/abc-defg-hij"&&
        items->GetDictionary(0)->GetString("title")=="Owned meeting"&&items->GetDictionary(0)->GetInt("startMinute")==600&&
        items->GetDictionary(0)->GetInt("endMinute")==675&&!items->GetDictionary(1)->HasKey("join")&&
        !items->GetDictionary(1)->HasKey("startMinute")&&items->GetDictionary(2)->GetInt("startMinute")==1110&&
        items->GetDictionary(2)->GetInt("endMinute")==1155;
    auto details=CefDictionaryValue::Create();details->SetBool("passed",passed);details->SetInt("rows",items?static_cast<int>(items->GetSize()):0);
    state->Record(mail?"google_mail_reader":"google_calendar_reader",details);
    if(mail){
      state->google_integrations_checks=passed;
      Pin(state,state->workspace->ActiveNode(),[weak,browser](int code,const std::string&,CefRefPtr<CefDictionaryValue>){
        const auto s=weak.lock();if(!s)return;
        Record(s,"background_fixture_pinned",!code);
        if(!code)browser->GetMainFrame()->LoadURL(s->initial_url+"?away=mail");
        s->workspace->OpenLink(kSecondInbox,true);
      });
    }
    else {
      state->google_integrations_checks=state->google_integrations_checks&&passed;
      const auto current=browser->GetMainFrame()->GetURL().ToString();const auto provider=GoogleIntegration(current);
      Hold(LiveRead::Start(browser,*provider,current+"#changed",[weak](CefRefPtr<CefDictionaryValue> stale){
        if(const auto state=weak.lock()){
          Record(state,"google_stale_reader",!stale);
          const auto node=state->workspace->ActiveNode();
          Pin(state,node,[weak,node](int code,const std::string&,CefRefPtr<CefDictionaryValue>){
            const auto s=weak.lock();if(!s)return;
            if(code){s->workspace->OpenLink(kOutlook,true);return;}
            s->workspace->Close(node,false,[weak](int,const std::string&,CefRefPtr<CefDictionaryValue>){
              if(const auto s=weak.lock())s->workspace->OpenLink(kOutlook,true);
            });
          });
        }
      }));
    }
  }));
}
}
