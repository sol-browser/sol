#include "workspace/controller.h"
#include "app/session.h"
#include <algorithm>

namespace red {
// Close without archiving first, so beforeunload cancellation keeps the whole
// saved folder. A final transaction archives all tabs and removes the folders.
// Reserve the scope in every workspace window to prevent lazy reopening.
struct WorkspaceController::FolderRemoval : std::enable_shared_from_this<FolderRemoval> {
  std::weak_ptr<WorkspaceController> owner;
  std::vector<std::weak_ptr<WorkspaceController>> windows;
  TreeCommand command;
  Reply done;
  bool finished=false;

  void Finish(int code,const std::string& error,CefRefPtr<CefDictionaryValue> snapshot=nullptr) {
    if (finished) return;
    const auto keep_alive=shared_from_this();
    finished=true;
    std::vector<std::shared_ptr<WorkspaceController>> released;
    for (const auto& weak:windows) if (const auto window=weak.lock()) {
      window->removing_folder_nodes_.clear(); window->folder_removal_.reset();
      released.push_back(window);
    }
    // Release every reservation before any window attempts lazy restoration.
    for (const auto& window:released)
      if (!window->shutting_down_ && !window->stopped_) { window->EnsureActive(); window->Publish(); }
    if (done) { auto reply=std::move(done); reply(code,error,snapshot); }
  }
  void Next() {
    if (finished) return;
    const auto self=owner.lock();
    if (!self || self->stopped_ || self->shutting_down_) { Finish(409,"Folder removal cancelled; window is closing"); return; }
    if (FolderNodeIds(self->tree_,command.id)!=command.confirmed_nodes) { Finish(409,"Folder contents changed; review removal again"); return; }
    for (const auto& weak:windows) if (const auto window=weak.lock()) {
      if (window->shutting_down_ || window->stopped_) { Finish(409,"Folder removal cancelled; window is closing"); return; }
      for (const auto& id:command.confirmed_nodes) {
        if (window->creating_.contains(id)) { Finish(409,"A folder page is still opening; try again"); return; }
        if (!window->sessions_.contains(id)) continue;
        window->Close(id,false,[operation=shared_from_this()](int code,const std::string& error,CefRefPtr<CefDictionaryValue>) {
          if (code) operation->Finish(code,error); else operation->Next();
        });
        return;
      }
    }
    self->Dispatch(command,[operation=shared_from_this()](int code,const std::string& error,CefRefPtr<CefDictionaryValue> snapshot) {
      operation->Finish(code,error,snapshot);
    },true);
  }
};

void WorkspaceController::RemoveFolder(TreeCommand command,Reply done) {
  auto operation=std::make_shared<FolderRemoval>();
  operation->owner=weak_from_this(); operation->command=std::move(command); operation->done=std::move(done);
  operation->windows.push_back(weak_from_this());
  if (session_) for (const auto& state:session_->Windows())
    if (const auto& other=state->workspace; other && other.get()!=this && other->data_==data_) operation->windows.push_back(other);
  for (const auto& weak:operation->windows) if (const auto window=weak.lock()) {
    if (window->folder_removal_ || window->shutting_down_ || !window->closing_.empty() || window->closing_commits_) {
      if (operation->done) operation->done(409,"Finish the current tab operation first",nullptr);
      return;
    }
  }
  for (const auto& weak:operation->windows) if (const auto window=weak.lock()) {
    window->folder_removal_=operation;
    window->removing_folder_nodes_=std::set<std::string>(operation->command.confirmed_nodes.begin(),operation->command.confirmed_nodes.end());
  }
  operation->Next();
}
void WorkspaceController::CancelFolderRemoval() {
  if (const auto operation=folder_removal_) operation->Finish(409,"Folder removal cancelled; window is closing");
}
}  // namespace red
