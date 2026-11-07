#include "windows/browser_dialogs.h"
#include "workspace/controller.h"
#include "windows/embedded_content.h"
#include "windows/focus.h"
#include "windows/motion.h"
#include "include/cef_parser.h"
#include "include/views/cef_display.h"
#include "include/base/cef_callback.h"
#include "include/wrapper/cef_closure_task.h"
#include <algorithm>
#include <cctype>
#include <objbase.h>
namespace red {
namespace {
std::string Site(const std::string& url) {
  CefURLParts parts;
  if (!CefParseURL(url,parts) || !parts.host.length || parts.username.length || parts.password.length ||
      (CefString(&parts.scheme)!="https" && CefString(&parts.scheme)!="http")) return {};
  auto host=CefString(&parts.host).ToString();
  std::transform(host.begin(),host.end(),host.begin(),[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  if (host.starts_with("www.")) host.erase(0,4);
  return host+":"+CefString(&parts.port).ToString();
}
}
bool WorkspaceController::IsPeekPage(const std::string& id) const {
  return id==link_peek_id_ || closing_peeks_.contains(id) || std::any_of(parked_peeks_.begin(),parked_peeks_.end(),[&](const auto& entry) { return entry.second.id==id; });
}
bool WorkspaceController::ParkLinkPeek() {
  const auto state=state_.lock(); const auto browser=LiveBrowser(link_peek_id_);
  if (!state || !state->embedded || !browser || link_peek_closing_ || link_peek_promoting_ || shutting_down_ || link_peek_opener_.empty()) return false;
  CloseParkedPeek(link_peek_opener_);
  const auto origin=state->embedded->LinkPeekOrigin();
  parked_peeks_[link_peek_opener_]={link_peek_id_,link_peek_url_,origin.x,origin.y};
  link_peek_id_.clear(); link_peek_opener_.clear(); link_peek_url_.clear();
  state->embedded->ClearLinkPeek();  // Hidden, not closed: page, input and history stay.
  state->Record("link_peek_parked"); Publish(); return true;
}
void WorkspaceController::ResumeLinkPeek() {
  const auto parked=parked_peeks_.find(tree_.active_id); const auto state=state_.lock();
  if (parked==parked_peeks_.end() || HasLinkPeek() || !state || !state->embedded || shutting_down_ || state->embedded->IsPageFullscreen()) return;
  const auto browser=LiveBrowser(parked->second.id);
  if (!browser) { parked_peeks_.erase(parked); return; }
  link_peek_id_=parked->second.id; link_peek_opener_=parked->first; link_peek_url_=parked->second.url; link_peek_closing_=false;
  state->embedded->SetLinkPeekOrigin(POINT{parked->second.origin_x,parked->second.origin_y});
  parked_peeks_.erase(parked);
  state->embedded->SetLinkPeek(browser->GetIdentifier(),true);
#if CEF_API_ADDED(CEF_EXPERIMENTAL)
  browser->GetHost()->NotifyTabActivated();
#endif
  state->Record("link_peek_resumed"); FocusBrowser(browser); Publish();
}
bool WorkspaceController::ReleasePeekForTransfer(const std::string& opener, std::optional<MovingPeek>& peek) {
  peek.reset();
  if (HasLinkPeek() && link_peek_opener_ == opener && !ParkLinkPeek()) return false;
  const auto parked = parked_peeks_.find(opener);
  if (parked == parked_peeks_.end()) return true;  // No preview: nothing to move.
  const auto browser = LiveBrowser(parked->second.id);
  if (!browser) { parked_peeks_.erase(parked); return true; }
  peek = MovingPeek{parked->second.id, parked->second.url, browser, parked->second.origin_x, parked->second.origin_y};
  const int id = browser->GetIdentifier();
  parked_peeks_.erase(parked); sessions_.erase(peek->id); browser_nodes_.erase(id);
  loaded_.erase(id); last_urls_.erase(id);
  Publish(); return true;
}
void WorkspaceController::AdoptPeek(const std::string& opener, const MovingPeek& peek) {
  sessions_[peek.id] = peek.browser; browser_nodes_[peek.browser->GetIdentifier()] = peek.id;
  if (!peek.browser->IsLoading()) loaded_.insert(peek.browser->GetIdentifier());
  parked_peeks_[opener] = {peek.id, peek.url, peek.origin_x, peek.origin_y};
}
void WorkspaceController::CloseParkedPeek(const std::string& source) {
  const auto parked=parked_peeks_.find(source); if (parked==parked_peeks_.end()) return;
  const auto id=parked->second.id; parked_peeks_.erase(parked);
  if (const auto browser=LiveBrowser(id)) { closing_peeks_.insert(id); CloseBrowserWithDialogs(state_, browser, true); }
  Publish();
}
bool WorkspaceController::IsLinkPeekBrowser(int browser) const {
  const auto found=sessions_.find(link_peek_id_); return HasLinkPeek() && found!=sessions_.end() && found->second->GetIdentifier()==browser;
}
bool WorkspaceController::CanSplitLinkPeek() const {
  if (!HasLinkPeek()) return false;
  const auto source=std::find_if(tree_.nodes.begin(),tree_.nodes.end(),[&](const auto& node) { return node.id==link_peek_opener_ && !node.folder && !node.archived; });
  return source!=tree_.nodes.end() && (source->pinned || std::none_of(tree_.splits.begin(),tree_.splits.end(),[&](const auto& split) { return split.anchor==source->id || split.other==source->id; }));
}
bool WorkspaceController::RouteLinkPeek(CefRefPtr<CefBrowser> source,const std::string& url,bool force) {
  const auto state=state_.lock();
  if (!state || !state->embedded || !source || !ready_ || stopped_ || shutting_down_ || HasLinkPeek() ||
      !closing_.empty() || closing_commits_ || state->embedded->IsPageFullscreen() || url.size()>4096 || Site(url).empty()) return false;
  const auto binding=browser_nodes_.find(source->GetIdentifier());
  if (binding==browser_nodes_.end()) return false;
  const auto* split=SelectedSplit();
  const bool visible_split=split && (split->anchor==binding->second || split->other==binding->second) &&
    (expanded_split_pane_.empty() || expanded_split_pane_==binding->second);
  if (tree_.active_id!=binding->second && !visible_split) return false;
  const auto node=std::find_if(tree_.nodes.begin(),tree_.nodes.end(),[&](const auto& n) { return n.id==binding->second; });
  if (node==tree_.nodes.end() || (!force && (!node->pinned || Site(source->GetMainFrame()->GetURL())==Site(url)))) return false;
  GUID guid{}; wchar_t name[40]{};
  if (FAILED(CoCreateGuid(&guid)) || !StringFromGUID2(guid,name,40)) return false;
  link_peek_id_=CefString(std::wstring(name+1,36)).ToString();
  link_peek_opener_=binding->second; link_peek_url_=url; link_peek_closing_=false;
  POINT origin{}; RECT source_frame{}; GetWindowRect(source->GetHost()->GetWindowHandle(),&source_frame); GetCursorPos(&origin);
  if (!PtInRect(&source_frame,origin)) origin={(source_frame.left+source_frame.right)/2,(source_frame.top+source_frame.bottom)/2};
  ScreenToClient(state->window->GetWindowHandle(),&origin); state->embedded->SetLinkPeekOrigin(origin);
  creating_.insert(link_peek_id_); state->embedded->SetLinkPeek(-2); Publish();
  // The preview loads as the source page's link (initiator, referrer), not as
  // a typed address.
  link_initiators_[link_peek_id_] = {url, source->GetMainFrame()->GetURL().ToString()};
  const auto id=link_peek_id_;
  // Creating a native Chrome browser blocks this UI thread for ~250 ms on
  // the measured build. Let the source compositor present its 300-ms retreat
  // first. The already-composed neutral card still starts at 220 ms.
#if defined(RED_CEF_PAGE_CONTAINER)
  const int create_delay=NativeMotionEnabled() ? LinkPeekScene::kReturnMs : 0;
#else
  const int create_delay=0;
#endif
  CefPostDelayedTask(TID_UI,base::BindOnce([](std::weak_ptr<WorkspaceController> weak,std::string id,std::string url) {
    if (auto self=weak.lock(); self && self->link_peek_id_==id && self->creating_.contains(id) && !self->link_peek_closing_ && !self->shutting_down_) if (const auto state=self->state_.lock(); state && state->embedded) {
      if (!self->CreateLive(url,id)) {
        self->creating_.erase(id); self->link_initiators_.erase(id); self->link_peek_id_.clear(); self->link_peek_opener_.clear(); self->link_peek_url_.clear();
        auto after=std::move(self->link_peek_after_); self->link_peek_closing_=false;
        state->embedded->ClearLinkPeek(); self->EnsureActive(); self->Notify("Could not open link preview"); if (after) after(false);
      }
    }
  },weak_from_this(),id,url),create_delay);
  CefPostDelayedTask(TID_UI,base::BindOnce([](std::weak_ptr<WorkspaceController> weak,std::string id) {
    if (auto self=weak.lock(); self && self->link_peek_id_==id && !self->LiveBrowser(id)) {
      self->creating_.erase(id); self->link_peek_id_.clear(); self->link_peek_opener_.clear(); self->link_peek_url_.clear();
      if (const auto state=self->state_.lock(); state && state->embedded) state->embedded->ClearLinkPeek();
      auto after=std::move(self->link_peek_after_); self->link_peek_closing_=false;
      self->EnsureActive(); self->Notify("Link preview could not start"); if (after) after(false);
    }
  },weak_from_this(),id),15000);
  return true;
}
CefRefPtr<CefDictionaryValue> WorkspaceController::DescribeLinkPeek() const {
  const auto state=state_.lock();
  if (!HasLinkPeek() || !state || !state->embedded || state->embedded->IsPageFullscreen()) return nullptr;
  auto result=CefDictionaryValue::Create();
  const auto browser=LiveBrowser(link_peek_id_); const auto card=state->embedded->LinkPeekCard();
  const auto area=state->embedded->SplitDropArea(); const double scale=state->window->GetDisplay()->GetDeviceScaleFactor();
  const auto bounds=[scale](const CefRect& rect) {
    auto value=CefDictionaryValue::Create(); value->SetDouble("x",rect.x/scale); value->SetDouble("y",rect.y/scale);
    value->SetDouble("width",rect.width/scale); value->SetDouble("height",rect.height/scale); return value;
  };
  result->SetString("id",link_peek_id_); result->SetString("url",(browser ? browser->GetMainFrame()->GetURL().ToString() : link_peek_url_).substr(0,4096));
  const auto title=page_titles_.find(link_peek_id_); result->SetString("title",title==page_titles_.end() ? "Link preview" : title->second.text);
  result->SetDictionary("bounds",bounds(card)); result->SetDictionary("area",bounds(area));
  result->SetBool("loading",!browser || browser->IsLoading()); result->SetBool("closing",link_peek_closing_ || link_peek_promoting_);
  result->SetBool("composed",state->embedded->LinkPeekComposed()); result->SetBool("opening",state->embedded->LinkPeekOpening());
  result->SetBool("depthReady",state->embedded->LinkPeekDepthReady());
  result->SetBool("canSplit",CanSplitLinkPeek()); result->SetBool("returning",state->embedded->LinkPeekReturnDelay()>0);
  result->SetBool("crashed",browser && crashed_.contains(browser->GetIdentifier()));
  result->SetBool("canGoBack",browser && browser->CanGoBack()); result->SetBool("canGoForward",browser && browser->CanGoForward());
  return result;
}
void WorkspaceController::DismissLinkPeek(std::function<void(bool)> after) {
  if (!HasLinkPeek()) { if (after) after(true); return; }
  if (link_peek_closing_ || link_peek_promoting_) { if (after) after(false); return; }
  link_peek_closing_=true; link_peek_after_=std::move(after); Publish();
  if (!LiveBrowser(link_peek_id_) && creating_.contains(link_peek_id_)) {
    const auto id=link_peek_id_; creating_.erase(id); link_initiators_.erase(id);
    const auto state=state_.lock();
    const int delay=state && state->embedded ? state->embedded->ReturnPendingLinkPeek() : 0;
    CefPostDelayedTask(TID_UI,base::BindOnce([](std::weak_ptr<WorkspaceController> weak,std::string id) {
      if (auto self=weak.lock(); self && self->link_peek_id_==id) self->FinishLinkPeekClose();
    },weak_from_this(),id),delay);
    return;
  }
  if (const auto state=state_.lock(); state && state->embedded && LiveBrowser(link_peek_id_)) {
    const auto id=link_peek_id_;
    state->embedded->PrepareLinkPeekClose([weak=weak_from_this(),id] {
      if (const auto self=weak.lock(); self && self->link_peek_id_==id && self->link_peek_closing_) if (const auto browser=self->LiveBrowser(id)) CloseBrowserWithDialogs(self->state_, browser, false);
    });
  }
}
void WorkspaceController::FinishLinkPeekClose() {
  page_titles_.erase(link_peek_id_); link_peek_id_.clear(); link_peek_opener_.clear(); link_peek_url_.clear(); link_peek_closing_=false;
  auto after=std::move(link_peek_after_);
  if (const auto state=state_.lock(); state && state->embedded) state->embedded->ClearLinkPeek();
  EnsureActive(); Publish(); if (const auto state=state_.lock(); state && !shutting_down_) FocusBrowser(state->content);
  if (after) after(true);
}
bool WorkspaceController::HandleLinkPeek(CefRefPtr<CefDictionaryValue> request,Reply done) {
  if (request->GetString("method")!="linkPeek.control") return false;
  const auto state=state_.lock();
  if (request->GetSize()!=3 || request->GetType("id")!=VTYPE_STRING || request->GetType("action")!=VTYPE_STRING ||
      !HasLinkPeek() || request->GetString("id")!=link_peek_id_ || !state || !state->embedded || shutting_down_) {
    done(400,"Invalid link preview command",nullptr); return true;
  }
  const auto action=request->GetString("action").ToString();
  if (action=="hoverClose" || action=="hoverSplit" || action=="hoverPromote" || action=="hoverNone") {
    state->embedded->HoverLinkPeek(action=="hoverClose" ? 0 : action=="hoverPromote" ? 1 : action=="hoverSplit" ? 2 : -1);
    done(0,"",Snapshot()); return true;
  }
  if (action=="close") {
    DismissLinkPeek([weak=weak_from_this(),done](bool closed) { if (auto self=weak.lock()) done(closed ? 0 : 409,closed ? "" : "Close cancelled; preview kept",self->Snapshot()); }); return true;
  }
  const auto browser=LiveBrowser(link_peek_id_);
  if (!browser || link_peek_closing_ || link_peek_promoting_) { done(409,"Link preview is busy",nullptr); return true; }
  if (action=="promote" || action=="split") {
    if (action=="split" && !CanSplitLinkPeek()) { done(409,"Source is already in a split or unavailable",nullptr); return true; }
    const auto url=browser->GetMainFrame()->GetURL().ToString(); if (Site(url).empty()) { done(400,"Preview address cannot become a tab",nullptr); return true; }
    TreeCommand command{TreeAction::CreateTab}; command.id=link_peek_id_; command.url=url; command.value=true;
    if (action=="split") {
      command.split_with=link_peek_opener_;
      const auto source=std::find_if(tree_.nodes.begin(),tree_.nodes.end(),[&](const auto& node) { return node.id==link_peek_opener_; });
      if (source!=tree_.nodes.end() && source->pinned) {
        const auto source_browser=LiveBrowser(source->id);
        command.split_source_url=source_browser ? source_browser->GetMainFrame()->GetURL().ToString() : source->url;
        const auto page=page_titles_.find(source->id);
        command.split_source_title=source->custom_title || page==page_titles_.end() || page->second.text.empty() ? source->title : page->second.text;
      }
    }
    const auto title=page_titles_.find(link_peek_id_); command.title=title==page_titles_.end() || title->second.text.empty() ? url : title->second.text;
    link_peek_promoting_=true;
    Dispatch(command,[weak=weak_from_this(),done](int code,const std::string& error,CefRefPtr<CefDictionaryValue>) {
      if (auto self=weak.lock()) {
        self->link_peek_promoting_=false;
        if (!code) {
          self->link_peek_id_.clear(); self->link_peek_opener_.clear(); self->link_peek_url_.clear();
          if (const auto state=self->state_.lock(); state && state->embedded) {
            state->embedded->ClearLinkPeek(true); state->embedded->SetSplitPages(-1,-1,false,5000,false);
            if (const auto browser=self->LiveBrowser(self->tree_.active_id)) state->embedded->Activate(browser);
          }
          self->EnsureActive(); self->Publish();
        }
        done(code,error,self->Snapshot());
      }
    },true); return true;
  }
  if (action=="back") NavigateHistory(false);
  else if (action=="forward") NavigateHistory(true);
  else if (action=="reload") Reload();
  else if (action=="stop") browser->StopLoad();
  else { done(400,"Unknown link preview action",nullptr); return true; }
  FocusBrowser(browser); Publish(); done(0,"",Snapshot()); return true;
}
}  // namespace red
