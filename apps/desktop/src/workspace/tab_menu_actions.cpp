#include "workspace/controller.h"
#include "windows/clipboard.h"
#include "windows/embedded_content.h"
#include "include/base/cef_callback.h"
#include "include/wrapper/cef_closure_task.h"
#include <algorithm>
#include <memory>

// Actions of the sidebar tab menus that are not plain tree commands
// (context_menu_model.h): copy a tab's link, edit a pinned page's saved
// address/title, archive other Today tabs or the ones below, and move a tab
// into a new space.
namespace red {
namespace {
bool WebAddress(const std::string& url) {
  return url.size() <= 4096 && (url.starts_with("https://") || url.starts_with("http://") || url.starts_with("file:///"));
}
}  // namespace

int WorkspaceController::CopyTabLink(const std::string& id, std::string& error) {
  const auto node = std::find_if(tree_.nodes.begin(), tree_.nodes.end(), [&](const auto& n) { return n.id == id; });
  if (node == tree_.nodes.end() || node->folder || node->archived) { error = "Tab is unavailable"; return 404; }
  const auto browser = LiveBrowser(id);
  const auto url = browser ? browser->GetMainFrame()->GetURL().ToString() : node->url;
  if (url.empty()) { error = "No page address to copy"; return 409; }
  const auto state = state_.lock();
  if (const int code = CopyLinkText(state && state->window ? state->window->GetWindowHandle() : nullptr, CefString(std::string(ShownAddress(url))).ToWString())) {
    error = code == 409 ? "The clipboard is busy. Try again." : "Could not copy the page address"; return code;
  }
  if (state && state->embedded) state->embedded->Notify("copy", L"Copied current URL");
  if (state) state->Record("tab_link_copied");
  return 0;
}

int WorkspaceController::SetPinnedPage(const std::string& id, std::string url, const std::string& title, std::string& error) {
  const auto node = std::find_if(tree_.nodes.begin(), tree_.nodes.end(), [&](const auto& n) { return n.id == id; });
  if (node == tree_.nodes.end() || node->folder || node->archived || (!node->pinned && !node->favorite)) { error = "Not a pinned page"; return 404; }
  if (url.empty()) {  // "Replace Pinned URL with Current".
    const auto browser = LiveBrowser(id);
    if (!browser) { error = "Open the page first"; return 409; }
    url = browser->GetMainFrame()->GetURL().ToString();
  }
  if (!WebAddress(url) || title.size() > 240) { error = "Invalid address"; return 400; }
  TreeCommand command{TreeAction::SetUrl}; command.id = id; command.url = url; command.section = "home";
  Dispatch(command, {}, true);
  if (!title.empty() && title != node->title) {
    TreeCommand rename{TreeAction::Rename}; rename.id = id; rename.title = title; Dispatch(rename, {}, true);
  }
  return 0;
}

void WorkspaceController::SetXBadgeHidden(const std::string& id, bool hidden, Reply done) {
  if (!ready_ || stopped_ || shutting_down_ || !worker_ || closing_.contains(id)) {
    done(409, "Tab is unavailable", nullptr); return;
  }
  TreeCommand command{TreeAction::SetXBadgeHidden};
  command.id = id;
  command.value = hidden;
  Dispatch(std::move(command), std::move(done), true);
}

int WorkspaceController::ArchiveTodayTabs(const std::string& id, bool below, std::string& error, bool all) {
  const auto node = std::find_if(tree_.nodes.begin(), tree_.nodes.end(), [&](const auto& n) { return n.id == id; });
  if (node == tree_.nodes.end() || node->folder || node->archived || node->pinned || node->favorite || !node->parent.empty()) {
    error = "Not a Today tab"; return 404;
  }
  std::vector<const TreeNode*> today;
  for (const auto& other : tree_.nodes)
    if (!other.folder && !other.archived && !other.pinned && !other.favorite && other.parent.empty() && other.space == node->space) today.push_back(&other);
  std::sort(today.begin(), today.end(), [](const auto* a, const auto* b) { return a->position < b->position; });
  auto ids = std::make_shared<std::vector<std::string>>();
  for (const auto* other : today)
    if (all || (other->id != id && (!below || other->position > node->position))) ids->push_back(other->id);
  if (ids->empty()) { error = below ? "No tabs below" : "No other tabs"; return 409; }
  // One at a time (each may ask to confirm unsaved changes); stops if one is kept.
  auto next = std::make_shared<std::function<void()>>();
  *next = [weak = weak_from_this(), ids, next] {
    const auto self = weak.lock(); if (!self || ids->empty()) return;
    const auto current = ids->front(); ids->erase(ids->begin());
    self->Close(current, true, [weak, next](int code, const std::string&, CefRefPtr<CefDictionaryValue>) {
      if (code) return;
      if (const auto self = weak.lock()) CefPostTask(TID_UI, base::BindOnce([](std::shared_ptr<std::function<void()>> next) { (*next)(); }, next));
    });
  };
  (*next)();
  return 0;
}

int WorkspaceController::MoveToNewSpace(const std::string& id, std::string& error) {
  const auto node = std::find_if(tree_.nodes.begin(), tree_.nodes.end(), [&](const auto& n) { return n.id == id; });
  if (node == tree_.nodes.end() || node->archived || node->favorite || !node->parent.empty()) { error = "Only top-level Pinned or Today entries move between spaces"; return 400; }
  if (tree_.spaces.size() >= 24) { error = "Too many spaces"; return 409; }
  TreeCommand create{TreeAction::CreateSpace}; create.title = "New space";
  Dispatch(create, [weak = weak_from_this(), id](int code, const std::string&, CefRefPtr<CefDictionaryValue>) {
    const auto self = weak.lock(); if (!self || code) return;
    // The new space is now the one on screen; the tab follows it there.
    TreeCommand move{TreeAction::MoveToSpace}; move.id = id; move.space = self->tree_.active_space;
    self->Dispatch(move, {}, true);
  }, true);
  return 0;
}
}  // namespace red
