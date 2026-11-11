#include "windows/browser_dialogs.h"
#include "workspace/controller.h"
#include "app/session.h"
#include <deque>
#include "include/base/cef_callback.h"
#include "include/wrapper/cef_closure_task.h"

namespace red {
void WorkspaceController::ChangeProfile(TreeCommand command, Reply done) {
  if (command.expected_revision!=tree_.revision) { done(409,"Workspace changed; retry",nullptr); return; }
  std::set<std::string> spaces(command.spaces.begin(),command.spaces.end());
  if (command.action==TreeAction::SetSpaceProfile) {
    const auto space=std::find_if(tree_.spaces.begin(),tree_.spaces.end(),[&](const auto& s){return s.id==command.id;});
    if (space==tree_.spaces.end() || std::none_of(tree_.profiles.begin(),tree_.profiles.end(),[&](const auto& p){return p.id==command.profile;})) {
      done(400,"Unknown profile or Space",nullptr); return;
    }
    if (space->profile==command.profile) { done(0,"",Snapshot()); return; }
    spaces.insert(space->id);
  }
  if (command.action==TreeAction::ExportSpace) {
    const auto source=std::find_if(tree_.spaces.begin(),tree_.spaces.end(),[&](const auto& s){return s.id==command.id;});
    const auto target=std::find_if(tree_.spaces.begin(),tree_.spaces.end(),[&](const auto& s){return s.id==command.space;});
    if (source==tree_.spaces.end() || target==tree_.spaces.end() || source==target) { done(400,"Choose another Space",nullptr); return; }
    if (source->profile!=target->profile) spaces.insert(source->id);
  }
  if (spaces.size()!=command.spaces.size() && command.action==TreeAction::CreateProfile) { done(400,"Duplicate Space",nullptr); return; }
  if (command.action==TreeAction::CreateProfile && (tree_.profiles.size()>=24 || command.title.empty() || command.title.size()>40 ||
      command.title.find_first_not_of(" \t\r\n")==std::string::npos)) { done(400,"Invalid profile name or profile limit reached",nullptr); return; }
  for (const auto& id : spaces) if (std::none_of(tree_.spaces.begin(),tree_.spaces.end(),[&](const auto& s){return s.id==id;})) {
    done(400,"Unknown Space",nullptr); return;
  }
  if (spaces.empty()) { Dispatch(std::move(command),std::move(done)); return; }
  UnloadThenDispatch([spaces](WorkspaceController& workspace,const TreeNode& node) {
    const TreeNode* root=&node;
    for (int depth=0;!root->parent.empty() && depth<64;++depth) {
      const auto parent=std::find_if(workspace.tree_.nodes.begin(),workspace.tree_.nodes.end(),[&](const auto& n){return n.id==root->parent;});
      if (parent==workspace.tree_.nodes.end()) break; root=&*parent;
    }
    return spaces.contains(root->space);
  },std::move(command),std::move(done),"Close Link Peek in the selected Spaces before changing profiles","Profile change cancelled; remaining tabs were kept");
}
// Unloads the live pages |selected| picks in every window of this workspace,
// each through its own close dialogs, then dispatches |command|. Refuses while
// another tab operation or a Link Peek is in progress; stops if a page is kept.
void WorkspaceController::UnloadThenDispatch(std::function<bool(WorkspaceController&,const TreeNode&)> selected,TreeCommand command,
                                             Reply done,const std::string& peek_error,const std::string& cancelled_error) {
  struct Operation {
    std::vector<std::shared_ptr<WorkspaceController>> windows;
    std::deque<std::pair<std::shared_ptr<WorkspaceController>,std::string>> pages;
    std::weak_ptr<WorkspaceController> owner;
    TreeCommand command;
    Reply done;
    std::string cancelled;  // Error when the user keeps one of the pages.
    std::function<void()> next;
  };
  auto operation=std::make_shared<Operation>(); operation->owner=weak_from_this(); operation->command=std::move(command); operation->done=std::move(done); operation->cancelled=cancelled_error;
  for (const auto& window : session_->Windows()) if (window->workspace_data==data_ && window->workspace) {
    const auto workspace=window->workspace;
    if (workspace->profile_changing_ || !workspace->closing_.empty() || workspace->folder_removal_ || !workspace->creating_.empty() ||
        !workspace->auto_closing_.empty() || workspace->HasLinkPeek()) {
      operation->done(409,"Finish the current tab operation and close Link Peek first",nullptr); return;
    }
    operation->windows.push_back(workspace);
    for (const auto& node : workspace->tree_.nodes) {
      if (!selected(*workspace,node)) continue;
      if (workspace->parked_peeks_.contains(node.id)) { operation->done(409,peek_error,nullptr); return; }
      if (workspace->LiveBrowser(node.id)) operation->pages.emplace_back(workspace,node.id);
    }
  }
  if (operation->pages.empty()) { Dispatch(operation->command,std::move(operation->done)); return; }
  for (const auto& workspace : operation->windows) workspace->profile_changing_=true;
  const auto finish=[operation](int code,const std::string& error,CefRefPtr<CefDictionaryValue> snapshot) {
    for (const auto& workspace : operation->windows) { workspace->profile_changing_=false; workspace->EnsureActive(); workspace->Publish(); }
    operation->next={}; if (operation->done) operation->done(code,error,snapshot);
  };
  operation->next=[weak=std::weak_ptr<Operation>(operation),finish] {
    const auto op=weak.lock(); if (!op) return;
    const auto owner=op->owner.lock(); if (!owner || owner->shutting_down_) { finish(409,"Window is closing",nullptr); return; }
    if (op->pages.empty()) {
      owner->Dispatch(op->command,finish,true); return;
    }
    const auto [workspace,id]=op->pages.front(); op->pages.pop_front();
    const auto browser=workspace->LiveBrowser(id);
    if (!browser) { CefPostTask(TID_UI,base::BindOnce([](std::shared_ptr<Operation> pending){if(pending->next)pending->next();},op)); return; }
    workspace->profile_closing_[id]=[op,finish](bool closed) {
      if (!closed) { finish(409,op->cancelled,nullptr); return; }
      CefPostTask(TID_UI,base::BindOnce([](std::shared_ptr<Operation> pending){if(pending->next)pending->next();},op));
    };
    CloseBrowserWithDialogs(workspace->state_, browser, false);
  };
  operation->next();
}
}  // namespace red
