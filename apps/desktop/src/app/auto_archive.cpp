#include "app/auto_archive.h"
#include "app/session.h"
#include "workspace/controller.h"
#include "windows/embedded_content.h"
#include "include/cef_command_line.h"
#include "include/base/cef_callback.h"
#include "include/wrapper/cef_closure_task.h"
#include <algorithm>
#include <chrono>
#include <utility>
#include <fstream>
#include <charconv>
namespace red {
namespace {
int64_t NativeNow() { return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count(); }
bool Today(const TreeNode& n) { return !n.folder && !n.archived && !n.pinned && !n.favorite && n.parent.empty(); }
}
std::shared_ptr<AutoArchive> AutoArchive::Start(const std::shared_ptr<AppSession>& session) {
  auto self=std::shared_ptr<AutoArchive>(new AutoArchive(session));
  if (CefCommandLine::GetGlobalCommandLine()->GetSwitchValue("auto-archive-test-clock")=="manual-fixture")
    for (const auto& state:session->Windows()) if (state->profile.filename()==L"profile" &&
        state->profile.parent_path().filename().wstring().starts_with(L"manual-input-") && state->profile.parent_path().parent_path().filename()==L"artifacts") {
      self->test_clock_file_=state->profile/"BrowserData"/"auto-archive-test-clock-ms"; break;
    }
  for (const auto& state:session->Windows()) if (state->probe && CefCommandLine::GetGlobalCommandLine()->HasSwitch("probe-unload")) self->unload_test_ms_=3000;
  self->Schedule(self->unload_test_ms_ ? 2000 : 30000); return self;
}
int64_t AutoArchive::Now() const {
  int64_t offset=0;
  if (!test_clock_file_.empty()) {
    std::ifstream stream(test_clock_file_); char value[32]{}; stream.getline(value,sizeof(value));
    const auto end=value+std::char_traits<char>::length(value); const auto parsed=std::from_chars(value,end,offset);
    if (!stream || parsed.ec!=std::errc{} || parsed.ptr!=end || offset<0 || offset>7LL*86400000) offset=0;
  }
  return NativeNow()+offset;
}
void AutoArchive::Activity(const std::string& id) {
  if (stopped_ || id.empty()) return;
  pending_.insert(id); recent_[id]=Now(); blocked_.erase(id);
}
bool AutoArchive::Reserved(const std::string& id) const { return std::find(ids_.begin(),ids_.end(),id)!=ids_.end(); }
void AutoArchive::Schedule(int milliseconds) {
  if (stopped_) return;
  if (!test_clock_file_.empty()) milliseconds=std::min(milliseconds,5000);
  if (unload_test_ms_) milliseconds=std::min(milliseconds,2000);
  CefPostDelayedTask(TID_UI,base::BindOnce([](std::weak_ptr<AutoArchive> weak) { if (auto self=weak.lock()) self->Tick(); },weak_from_this()),milliseconds);
}
void AutoArchive::Tick() {
  const auto session=session_.lock();
  if (stopped_ || !session) return;
  const auto data=session->workspace;
  if (!data->ready || !data->worker) { Schedule(60000); return; }
  auto touched=std::exchange(pending_,{});
  for (const auto& node:data->tree.nodes) {
    if (node.archived || node.folder) continue;
    bool protect=node.last_active_at<=0 || node.last_active_at>Now();
    for (const auto& state:session->Windows()) if (state->workspace_data==data && state->workspace && state->workspace->AutoArchiveProtected(node.id)) protect=true;
    if (protect) { touched.insert(node.id); recent_[node.id]=Now(); }
  }
  std::map<std::string,int64_t> activity;
  for (const auto& id:touched) { const auto at=recent_.find(id); activity[id]=at==recent_.end() ? Now() : at->second; }
  data->worker->GetTaskRunner()->PostTask(CefCreateClosureTask(base::BindOnce([](std::shared_ptr<AutoArchive> self,std::shared_ptr<WorkspaceData> data,std::map<std::string,int64_t> activity) {
    auto result=data->store->TouchTabs(activity);
    CefPostTask(TID_UI,base::BindOnce([](std::shared_ptr<AutoArchive> self,std::shared_ptr<WorkspaceData> data,TreeResult result) {
      const auto session=self->session_.lock(); if (self->stopped_ || !session) return;
      if (!result) { self->Schedule(60000); return; }
      session->PublishTree(data,result.snapshot,nullptr); self->budget_=8; self->unload_budget_=8; self->archived_=0; self->Next();
    },self,data,std::move(result)));
  },shared_from_this(),data,std::move(activity))));
}
bool AutoArchive::Safe() const {
  const auto session=session_.lock();
  if (stopped_ || !session || !session->preferences || !session->preferences->Get().auto_archive_hours) return false;
  const auto& tree=session->workspace->tree;
  const auto cutoff=std::min(cutoff_,Now()-static_cast<int64_t>(session->preferences->Get().auto_archive_hours)*3600000);
  for (const auto& id:ids_) {
    const auto node=std::find_if(tree.nodes.begin(),tree.nodes.end(),[&](const auto& n) { return n.id==id; });
    if (node==tree.nodes.end() || !Today(*node) || node->last_active_at<=0 || node->last_active_at>cutoff || blocked_.contains(id)) return false;
    if (const auto activity=recent_.find(id); activity!=recent_.end() && activity->second>cutoff) return false;
  }
  // A newly-created/changed split must not be partly archived by a stale job.
  for (const auto& split:tree.splits) if (std::find(ids_.begin(),ids_.end(),split.anchor)!=ids_.end() || std::find(ids_.begin(),ids_.end(),split.other)!=ids_.end())
    if (ids_.size()!=2 || std::find(ids_.begin(),ids_.end(),split.anchor)==ids_.end() || std::find(ids_.begin(),ids_.end(),split.other)==ids_.end()) return false;
  for (const auto& state:session->Windows()) if (state->workspace_data==session->workspace && state->workspace) {
    if (state->workspace->AutoArchiveBusy()) return false;
    for (const auto& id:ids_) if (state->workspace->AutoArchiveProtected(id)) return false;
  }
  return true;
}
void AutoArchive::Next() {
  const auto session=session_.lock(); if (stopped_ || !session) return;
  const int hours=session->preferences ? session->preferences->Get().auto_archive_hours : 0;
  cutoff_=Now()-static_cast<int64_t>(hours)*3600000;
  if (hours && budget_>0) for (const auto& node:session->workspace->tree.nodes) {
    if (!Today(node) || node.last_active_at<=0 || node.last_active_at>cutoff_ || blocked_.contains(node.id)) continue;
    ids_={node.id};
    for (const auto& split:session->workspace->tree.splits) if (split.anchor==node.id || split.other==node.id) { ids_={split.anchor,split.other}; break; }
    if (!Safe()) continue;
    --budget_; close_index_=0; CloseNext(); return;
  }
  ids_.clear();
  if (UnloadNext()) return;
  if (archived_) for (const auto& state:session->Windows()) if (!state->standalone_window && state->embedded && state->window && !state->window->IsMinimized()) {
    state->embedded->Notify("archive",L"Auto-archived "+std::to_wstring(archived_)+L" tabs"); break;
  }
  Schedule(60000);
}
void AutoArchive::CloseNext() {
  const auto session=session_.lock(); if (!Safe() || !session) { Finish(false); return; }
  const auto windows=session->Windows();
  for (const auto& state:windows) for (const auto& id:ids_) {
    if (state->workspace_data!=session->workspace || !state->workspace || !state->workspace->LiveBrowser(id)) continue;
    state->workspace->AutoArchiveUnload(id,[weak=weak_from_this()](bool closed) { if (auto self=weak.lock()) { if (closed) self->CloseNext(); else self->Finish(false); } }); return;
  }
  // Metadata commits only after every live instance has accepted closing.
  const auto data=session->workspace; const auto ids=ids_; const auto cutoff=cutoff_;
  data->worker->GetTaskRunner()->PostTask(CefCreateClosureTask(base::BindOnce([](std::shared_ptr<AutoArchive> self,std::shared_ptr<WorkspaceData> data,std::vector<std::string> ids,int64_t cutoff) {
    auto current=data->store->Read(); TreeResult result=current;
    if (current) { TreeCommand command{TreeAction::AutoArchive}; command.id=ids.front(); command.not_after=cutoff; command.expected_revision=current.snapshot.revision; result=data->store->Apply(command); }
    CefPostTask(TID_UI,base::BindOnce([](std::shared_ptr<AutoArchive> self,std::shared_ptr<WorkspaceData> data,TreeResult result) {
      if (self->stopped_) return;
      if (const auto session=self->session_.lock(); session && result) session->PublishTree(data,result.snapshot,nullptr);
      self->Finish(static_cast<bool>(result));
    },self,data,std::move(result)));
  },shared_from_this(),data,ids,cutoff)));
}
bool AutoArchive::UnloadNext() {
  const auto session=session_.lock();
  const int minutes=session && session->preferences ? session->preferences->Get().unload_minutes : 0;
  if (stopped_ || !session || (!minutes && !unload_test_ms_) || unload_budget_<=0) return false;
  const auto cutoff=Now()-(unload_test_ms_ ? unload_test_ms_ : static_cast<int64_t>(minutes)*60000);
  for (const auto& node:session->workspace->tree.nodes) {
    if (node.folder || node.archived || node.last_active_at<=0 || node.last_active_at>cutoff || blocked_.contains(node.id)) continue;
    if (const auto activity=recent_.find(node.id); activity!=recent_.end() && activity->second>cutoff) continue;
    std::shared_ptr<State> window; bool held=false;
    for (const auto& state:session->Windows()) if (state->workspace_data==session->workspace && state->workspace) {
      if (state->workspace->AutoArchiveBusy() || state->workspace->AutoArchiveProtected(node.id)) { held=true; break; }
      if (state->workspace->LiveBrowser(node.id)) { if (!state->workspace->UnloadKeepsPlace(node.id)) { held=true; break; } window=state; }
    }
    if (held || !window) continue;
    --unload_budget_;
    window->workspace->AutoArchiveUnload(node.id,[weak=weak_from_this(),id=node.id](bool closed) { if (auto self=weak.lock()) self->Unloaded(id,closed); });
    return true;
  }
  return false;
}
void AutoArchive::Unloaded(const std::string& id, bool closed) {
  if (stopped_) return;
  if (!closed) blocked_.insert(id);
  if (const auto session=session_.lock()) { auto data=CefDictionaryValue::Create(); data->SetBool("closed",closed);
    for (const auto& state:session->Windows()) if (state->probe) { state->Record("tab_unloaded",data); break; } }
  // Another window may still show it: unload there too, then continue.
  if (closed) if (const auto session=session_.lock()) for (const auto& state:session->Windows())
    if (state->workspace_data==session->workspace && state->workspace && state->workspace->LiveBrowser(id)) {
      state->workspace->AutoArchiveUnload(id,[weak=weak_from_this(),id](bool again) { if (auto self=weak.lock()) self->Unloaded(id,again); }); return;
    }
  Next();
}
void AutoArchive::Finish(bool success) {
  if (stopped_) return;
  if (success) archived_+=static_cast<int>(ids_.size());
  else for (const auto& id:ids_) blocked_.insert(id);
  ids_.clear();
  if (!success) if (const auto session=session_.lock()) for (const auto& state:session->Windows())
    if (state->window && state->workspace && state->workspace_data==session->workspace) state->workspace->AttachContent();
  Next();
}
}  // namespace red
