#include "windows/browser_dialogs.h"
#include "web_url.h"
#include "workspace/controller.h"
#include "app/session.h"
#include "app/auto_archive.h"
#include "windows/embedded_content.h"
#include "integrations/providers.h"
namespace red {
void WorkspaceController::TouchAudibleActivity(int browser) {
  const auto found=browser_nodes_.find(browser);
  if (found!=browser_nodes_.end() && session_ && session_->auto_archive) session_->auto_archive->Activity(found->second);
}
bool WorkspaceController::AutoArchiveBusy() const {
  const auto state=state_.lock();
  if (!state || !state->window) return false;
  return !ready_ || stopped_ || shutting_down_ || HasLinkPeek() || pending_commands_ || closing_commits_ || !closing_.empty() ||
    profile_changing_ || !creating_.empty() || !moving_unloads_.empty() || split_resizing_ || state->tab_dragging || state->ui_editing;
}
bool WorkspaceController::AutoArchiveProtected(const std::string& id) const {
  const auto state=state_.lock(); if (!state || !state->window) return false;
  if (preloading_.contains(id)) return true;  // ExpirePreload unloads it on its own schedule.
  const auto browser=LiveBrowser(id);
  if (!state->window->IsMinimized() && IsWindowVisible(state->window->GetWindowHandle())) {
    if (tree_.active_id==id) return true;
    if (browser && state->embedded && state->embedded->IsSplitPage(browser->GetIdentifier()) && IsWindowVisible(browser->GetHost()->GetWindowHandle())) return true;
  }
  if (browser) {
    const auto bid=browser->GetIdentifier();
    if (auto_archive_media_.contains(bid) || state->audible_frames.contains(bid) || browser->GetHost()->IsAudioMuted()) return true;
    if (const auto media=state->media_access.find(bid); media!=state->media_access.end() && (media->second.first || media->second.second)) return true;
  }
  return false;
}
void WorkspaceController::AutoArchiveUnload(const std::string& id, std::function<void(bool)> done) {
  if (AutoArchiveBusy() || AutoArchiveProtected(id) || auto_closing_.contains(id)) { done(false); return; }
  const auto browser=LiveBrowser(id); if (!browser) { done(true); return; }
#if RED_CEF_NAVIGATION_STATE
  // Kept like Chrome's discarded tabs; a beforeunload veto keeps the page and
  // leaves an entry that the next unload or reopen replaces.
  std::erase_if(discarded_, [&](const auto& entry) {
    return std::none_of(tree_.nodes.begin(), tree_.nodes.end(), [&](const auto& n) { return n.id == entry.first && !n.archived; });
  });
  // Keyed by the page's address: Today tabs save it as theirs when the page closes;
  // Pinned/Favorites unload only while on their saved address (UnloadKeepsPlace).
  if (auto saved = browser->GetHost()->GetNavigationState()) discarded_[id] = {browser->GetMainFrame()->GetURL().ToString(), saved};
  else discarded_.erase(id);
#endif
  auto_closing_[id]=std::move(done); Publish(); CloseBrowserWithDialogs(state_, browser, false);
}
void WorkspaceController::UnloadPreload(const std::string& id, std::function<void(bool)> done) {
  if (!ready_ || stopped_ || shutting_down_ || pending_commands_ || closing_commits_ || !closing_.empty() || profile_changing_ ||
      !moving_unloads_.empty() || auto_closing_.contains(id)) { done(false); return; }
  const auto browser=LiveBrowser(id); if (!browser) { done(true); return; }
  auto_closing_[id]=std::move(done); Publish(); CloseBrowserWithDialogs(state_, browser, false);
}
bool WorkspaceController::UnloadKeepsPlace(const std::string& id) const {
  const auto node=std::find_if(tree_.nodes.begin(),tree_.nodes.end(),[&](const auto& n) { return n.id==id; });
  const auto browser=LiveBrowser(id);
  if (node==tree_.nodes.end() || node->folder || node->archived || !browser || crashed_.contains(browser->GetIdentifier())) return false;
  // Never the selected page or a selected split's panes, even while minimized.
  if (tree_.active_id==id) return false;
  if (const auto* split=SelectedSplit(); split && split->Contains(id)) return false;
  if (!node->pinned && !node->favorite) {  // Its current web address is saved on unload.
    const auto url=browser->GetMainFrame()->GetURL().ToString();
    return HasWebScheme(url);
  }
  return browser->GetMainFrame()->GetURL().ToString()==node->url;
}
bool WorkspaceController::AutoArchiveClosing(int browser) const {
  const auto node=browser_nodes_.find(browser); return node!=browser_nodes_.end() && auto_closing_.contains(node->second);
}
}  // namespace red
