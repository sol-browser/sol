#include "storage/tree_store.h"
#include <windows.h>
#include <algorithm>
#include <cstdio>

namespace {
int failures=0;
void Check(bool value,const char* name) { if (!value) { ++failures; std::fprintf(stderr,"FAIL: %s\n",name); } }
const red::TreeNode* Find(const red::TreeSnapshot& tree,const std::string& id) {
  const auto found=std::find_if(tree.nodes.begin(),tree.nodes.end(),[&](const auto& n) { return n.id==id; });
  return found==tree.nodes.end()?nullptr:&*found;
}
}
int wmain() {
  const auto path=std::filesystem::temp_directory_path()/(L"red-folder-removal-"+std::to_wstring(GetCurrentProcessId())+L".sqlite3");
  std::string parent,nested,first,second,extra,outside;
  {
    red::TreeStore store; Check(static_cast<bool>(store.Open(path)),"store opens");
    auto apply=[&](red::TreeCommand command) { command.expected_revision=store.Read().snapshot.revision; return store.Apply(command); };
    auto folder=[&](const std::string& owner) { red::TreeCommand command{red::TreeAction::CreateFolder}; command.parent=owner; command.title="Folder"; return apply(command).created_id; };
    auto tab=[&](const std::string& owner) { red::TreeCommand command{red::TreeAction::CreateTab}; command.parent=owner; command.title="Page"; command.url="https://example.org/"; return apply(command).created_id; };
    parent=folder(""); nested=folder(parent); first=tab(parent); second=tab(nested); outside=tab("");
    Check(red::FolderNodeIds(store.Read().snapshot,first).empty(),"tab cannot be a removal root");
    Check(red::FolderNodeIds(store.Read().snapshot,"missing").empty(),"unknown folder cannot be removed");
    red::TreeCommand command{red::TreeAction::RemoveLeaf}; command.id=parent;
    Check(apply(command).code==409,"nonempty folder requires explicit confirmation");
    command={red::TreeAction::RemoveFolder}; command.id=parent;
    Check(apply(command).code==409,"missing confirmed scope rejected");
    command.confirmed_nodes=red::FolderNodeIds(store.Read().snapshot,parent);
    extra=tab(nested);
    const auto before=store.Read().snapshot;
    Check(apply(command).code==409,"new descendant invalidates earlier confirmation");
    Check(store.Read().snapshot.nodes==before.nodes && store.Read().snapshot.revision==before.revision,"rejected removal changes nothing");
    command.confirmed_nodes=red::FolderNodeIds(before,parent);
    auto stale=command; stale.expected_revision=before.revision-1;
    Check(store.Apply(stale).code==409,"stale structural revision rejected");
    const auto approved=command.confirmed_nodes;
    command={red::TreeAction::RemoveLeaf}; command.id=extra; apply(command);
    extra=tab(nested);
    command={red::TreeAction::RemoveFolder}; command.id=parent; command.confirmed_nodes=approved;
    Check(apply(command).code==409,"replacing a descendant with the same tab count still invalidates confirmation");
    command={red::TreeAction::CreateSplit}; command.id=first; command.parent=second; Check(static_cast<bool>(apply(command)),"nested tabs can share a split");
    command={red::TreeAction::RemoveFolder}; command.id=parent; command.confirmed_nodes=red::FolderNodeIds(store.Read().snapshot,parent);
    auto result=apply(command);
    Check(static_cast<bool>(result),"confirmed subtree removal commits");
    Check(!Find(result.snapshot,parent) && !Find(result.snapshot,nested),"root and nested folders are removed");
    for (const auto& id:{first,second,extra}) {
      const auto* page=Find(result.snapshot,id);
      Check(page && page->archived && !page->pinned && !page->favorite && page->parent.empty() && page->archived_at>0,"every nested tab is recoverable from Archive");
    }
    Check(Find(result.snapshot,outside) && !Find(result.snapshot,outside)->archived,"unrelated tab survives");
    Check(result.snapshot.splits.empty() && result.snapshot.archived_splits.size()==1,"both split members retain archived layout");
    Check(red::TreeStore::ValidateSnapshot(result.snapshot),"result preserves all tree invariants");
  }
  {
    red::TreeStore store; const auto reopened=store.Open(path);
    Check(reopened && !Find(reopened.snapshot,parent) && Find(reopened.snapshot,first)->archived,"folder removal survives restart");
    red::TreeCommand command{red::TreeAction::Restore}; command.id=first; command.expected_revision=reopened.snapshot.revision;
    const auto result=store.Apply(command);
    Check(result && !Find(result.snapshot,first)->archived && !Find(result.snapshot,second)->archived && result.snapshot.splits.size()==1,"archive restores split tabs after folder deletion");
  }
  for (const auto& suffix:{L"",L"-wal",L"-shm"}) { std::error_code error; std::filesystem::remove(path.wstring()+suffix,error); }
  return failures?1:0;
}
