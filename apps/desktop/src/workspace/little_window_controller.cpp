// Little Sol (docs/little-sol.md): a compact window that holds exactly one page.
// The page can move to a Space; every other page it would open goes to a workspace window.
#include "workspace/controller.h"
#include "app/session.h"
#include "web_url.h"
#include "windows/command_bar.h"
#include "windows/focus.h"
#include "include/base/cef_callback.h"
#include "include/wrapper/cef_closure_task.h"
#include <algorithm>
namespace red {
namespace {
void BringToFront(const std::shared_ptr<State>& state) {
  if (!state || !state->window) return;
  if (state->window->IsMinimized()) state->window->Restore();
  state->window->Show();
  state->window->Activate();
}
const Space* FindSessionSpace(const std::shared_ptr<AppSession>& session, const std::string& id) {
  const auto& spaces = session->workspace->tree.spaces;
  const auto found = std::find_if(spaces.begin(), spaces.end(), [&](const auto& space) { return space.id == id; });
  return found == spaces.end() ? nullptr : &*found;
}
}  // namespace

bool WorkspaceController::OpensInLittleSol(const AppSession& session, const std::string& url) {
  return (!session.preferences || session.preferences->Get().little_sol_links) && HasWebScheme(url) && url.size() <= 8192;
}

void WorkspaceController::QueueLittleSolLinks(const std::vector<std::string>& urls) {
  for (const auto& url : urls) {
    if (little_pending_.size() >= 16) break;
    if (HasWebScheme(url) && url.size() <= 8192) little_pending_.push_back(url);
  }
  DrainLittleSolLinks();
}

void WorkspaceController::DrainLittleSolLinks() {
  const auto state = state_.lock();
  if (little_pending_.empty() || !CanOpen() || !state || !state->embedded || !state->window) return;
  auto urls = std::move(little_pending_);
  little_pending_.clear();
  std::vector<std::string> tabs;
  for (const auto& url : urls)
    if (!session_ || !session_->new_little_window || !session_->new_little_window(state, url)) tabs.push_back(url);
  if (!tabs.empty()) QueueExternalUrls(tabs);
}

void WorkspaceController::StartLittleSolLauncher(int attempts) {
  const auto state = state_.lock();
  if (!state || !state->little_launcher || shutting_down_) return;
  if (CanOpen() && state->window && state->command_bar && state->browsers.contains(state->shell_id)) {
    state->command_bar->OpenDetached();
    if (state->command_bar->is_open()) return;
  }
  if (attempts <= 0) {
    state->Record("little_sol_launcher_timeout");
    state->little_launcher = false;
    if (state->window) state->window->Close();
    return;
  }
  CefPostDelayedTask(TID_UI, base::BindOnce([](std::weak_ptr<WorkspaceController> weak, int attempts) {
    if (const auto self = weak.lock()) self->StartLittleSolLauncher(attempts - 1);
  }, weak_from_this(), attempts), 50);
}

void WorkspaceController::FinishLittleSolLauncher(bool opened) {
  const auto state = state_.lock();
  if (!state || !state->little_launcher) return;
  state->little_launcher = false;
  state->Record(opened ? "little_sol_launcher_opened" : "little_sol_launcher_cancelled");
  if (!opened || !state->window) { if (state->window) state->window->Close(); return; }
  // The bar that asked for the address was a window of its own; Ctrl+L now belongs to this window.
  if (state->command_bar) state->command_bar->Shutdown();
  state->command_bar = std::make_shared<CommandBar>(state);
  BringToFront(state);
  if (state->content) FocusBrowser(state->content);
}

bool WorkspaceController::LittleWindowHasPage() const {
  const auto state = state_.lock();
  return state && state->little_window &&
      std::any_of(tree_.nodes.begin(), tree_.nodes.end(), [](const auto& node) { return !node.folder && !node.archived; });
}

bool WorkspaceController::LittleWindowRejects(const TreeCommand& command) const {
  const auto state = state_.lock();
  if (!state || !state->little_window) return false;
  switch (command.action) {
    // Nothing in Little Sol shows folders, Spaces, splits or closed tabs.
    case TreeAction::CreateFolder: case TreeAction::CreateSpace: case TreeAction::CreateSplit: case TreeAction::AddSplitPane:
    case TreeAction::Restore: case TreeAction::CreateProfile: case TreeAction::MoveToSpace: case TreeAction::SetSpaceProfile:
    case TreeAction::ExportSpace:
      return true;
    case TreeAction::CreateTab: return LittleWindowHasPage();
    default: return false;
  }
}

std::shared_ptr<State> WorkspaceController::LittleTargetWindow(const std::string& space_id) {
  const auto source = state_.lock();
  if (!source || !session_) return nullptr;
  const auto find = [&] {
    std::shared_ptr<State> target;
    for (const auto& window : session_->Windows()) {
      if (window->standalone_window || !window->window || !window->workspace || !window->workspace->CanOpen()) continue;
      if (!target || window->workspace->ActiveSpaceId() == space_id) target = window;
    }
    return target;
  };
  if (auto target = find()) return target;
  if (session_->new_window && session_->new_window(source, "")) return find();
  return nullptr;
}

std::string WorkspaceController::LittleLinkSpace() const {
  const auto state = state_.lock();
  if (!state || !session_) return {};
  const auto& spaces = session_->workspace->tree.spaces;
  // The Space the window came from, else any Space that shares the page's cookies.
  if (const auto* preferred = FindSessionSpace(session_, state->initial_space); preferred && preferred->profile == state->active_profile)
    return preferred->id;
  const auto same = std::find_if(spaces.begin(), spaces.end(), [&](const auto& space) { return space.profile == state->active_profile; });
  return same == spaces.end() ? std::string{} : same->id;
}

void WorkspaceController::OpenFromLittleWindow(const std::string& url, const std::string& title, bool foreground, Reply done,
                                               const std::string& initiator) {
  const auto space_id = LittleLinkSpace();
  const auto* space = session_ ? FindSessionSpace(session_, space_id) : nullptr;
  if (!space) { if (done) done(409, "No Space uses this page's profile", nullptr); return; }
  const auto target = LittleTargetWindow(space_id);
  if (!target) { if (done) done(409, "Cannot open a workspace window", nullptr); return; }
  target->workspace->OpenPage(url, title, foreground,
      [weak = weak_from_this(), weak_target = std::weak_ptr<State>(target), foreground, name = space->name, done = std::move(done)]
      (int code, const std::string& error, CefRefPtr<CefDictionaryValue> snapshot) {
    const auto self = weak.lock();
    if (!code) {
      if (foreground) BringToFront(weak_target.lock());
      else if (self) self->Notify("Opened in " + name);
    }
    if (done) done(code, error, code ? nullptr : self ? self->Snapshot() : snapshot);
  }, initiator, "", space_id);
}

void WorkspaceController::SendLittlePage(const std::string& space_id, Reply done) {
  const auto source = state_.lock();
  if (!source || !source->little_window || !CanOpen() || sending_little_page_ || !session_) {
    done(409, "This page cannot move right now", nullptr);
    return;
  }
  const auto source_id = tree_.active_id;
  const auto seed = DetachedSeed(source_id);
  if (!seed || !HasWebScheme(seed->url) || !CanTransfer(source_id)) {
    done(409, "Wait for the page to open before moving it", nullptr);
    return;
  }
  const auto* space = FindSessionSpace(session_, space_id);
  if (!space) {
    done(404, "This Space is no longer available", nullptr);
    Publish();
    return;
  }
  const bool same_profile = space->profile == source->active_profile;
  const auto target = LittleTargetWindow(space_id);
  if (!target) {
    done(409, "Cannot open a workspace window", nullptr);
    return;
  }
  sending_little_page_ = true;
  TreeCommand create{TreeAction::CreateTab};
  create.space = space_id;
  create.url = seed->url;
  create.title = seed->title;
  auto finish = [weak = weak_from_this(), destination = std::weak_ptr<State>(target), done = std::move(done)]
      (int code, const std::string& error) {
    const auto self = weak.lock();
    if (!self) return;
    self->sending_little_page_ = false;
    if (code) { done(code, error, nullptr); return; }
    done(0, "", self->Snapshot());
    BringToFront(destination.lock());
    if (const auto source = self->state_.lock(); source && source->window) source->window->Close();
  };
  target->workspace->Dispatch(std::move(create),
      [weak_source = std::weak_ptr<State>(source), weak_target = std::weak_ptr<State>(target), source_id, same_profile,
       finish = std::move(finish)](int code, const std::string& error, CefRefPtr<CefDictionaryValue> snapshot) mutable {
    if (code) { finish(code, error); return; }
    const auto source = weak_source.lock();
    const auto target = weak_target.lock();
    if (!target || !target->workspace || !snapshot) { finish(409, "The workspace window closed"); return; }
    const auto id = snapshot->GetString("createdId").ToString();
    if (same_profile) {
      if (source && source->window && source->session->transfer_page &&
          source->session->transfer_page(source, target, source_id, id, false)) {
        finish(0, "");
      } else {
        target->workspace->RemoveDetachedToday(id);
        finish(409, "The page could not move. Try again when it finishes loading");
      }
      return;
    }
    // Another profile must create its own page. A live browser keeps its original context.
    TreeCommand activate{TreeAction::Activate};
    activate.id = id;
    target->workspace->Dispatch(std::move(activate),
        [weak_target, id, finish = std::move(finish)](int code, const std::string& error, CefRefPtr<CefDictionaryValue>) {
      // A failed switch must not leave a stray copy of the page in that Space.
      if (code) if (const auto target = weak_target.lock(); target && target->workspace) target->workspace->RemoveDetachedToday(id);
      finish(code, error);
    }, true);
  }, true);
}
}  // namespace red
