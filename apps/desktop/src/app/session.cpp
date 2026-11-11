#include "app/session.h"
#include "app/auto_archive.h"
#include "app/browsing_profiles.h"
#include "workspace/controller.h"
#include "windows/command_bar.h"
#include "windows/auto_pip.h"
#include "include/cef_app.h"
#include "include/base/cef_callback.h"
#include "include/wrapper/cef_closure_task.h"
#include <algorithm>
namespace red {
namespace {
void StopData(const std::shared_ptr<WorkspaceData>& data) {
  if (!data->worker) return;
  data->worker->GetTaskRunner()->PostTask(CefCreateClosureTask(base::BindOnce(
      [](std::shared_ptr<WorkspaceData> scope) { scope->store.reset(); }, data)));
  data->worker->Stop(); data->worker = nullptr;
}
}
void AppSession::Register(const std::shared_ptr<State>& window) {
  std::lock_guard lock(mutex_); windows_.push_back(window);
}
bool BrowserIntegrationEnabled(const std::shared_ptr<State>& state, std::string_view id) {
  if (state && state->session && state->session->preferences) return IntegrationEnabled(state->session->preferences->Get().integrations, id);
  return FindIntegrationSetting(id) != nullptr;
}
void SaveIntegrationPreference(const std::shared_ptr<State>& state, std::string id, bool enabled, BrowserPreferences::Reply done) {
  const auto session = state ? state->session : nullptr;
  const auto* provider = FindIntegrationSetting(id);
  if (!session || !session->preferences || !provider || !provider->listed) { done("Integration is unavailable"); return; }
  auto next = session->preferences->Get();
  next.integrations[id] = enabled;
  session->preferences->Save(std::move(next), [weak = std::weak_ptr<AppSession>(session), id = std::move(id), done = std::move(done)](const std::string& error) {
    if (error.empty()) if (const auto owner = weak.lock()) for (const auto& window : owner->Windows()) {
      if (window->workspace) window->workspace->Refresh();
      if (window->command_bar) window->command_bar->Refresh();
      if (id == "googleMeet") RefreshPipIntegrations(window);
    }
    done(error);
  });
}
std::vector<std::shared_ptr<State>> AppSession::Windows() const {
  std::lock_guard lock(mutex_);
  std::vector<std::shared_ptr<State>> result;
  for (const auto& weak : windows_) if (auto state = weak.lock()) result.push_back(std::move(state));
  return result;
}
bool AppSession::HasOpenWindows() const {
  for (const auto& state : Windows()) if (state->window || !state->browsers.empty()) return true;
  return false;
}
void AppSession::RegisterUi(int browser, const std::shared_ptr<State>& state) {
  std::lock_guard lock(mutex_); ui_owners_[browser] = state;
}
void AppSession::UnregisterUi(int browser) { std::lock_guard lock(mutex_); ui_owners_.erase(browser); }
std::shared_ptr<State> AppSession::UiOwner(int browser) const {
  std::lock_guard lock(mutex_);
  const auto found = ui_owners_.find(browser);
  return found == ui_owners_.end() ? nullptr : found->second.lock();
}
void AppSession::PublishTree(const std::shared_ptr<WorkspaceData>& data, const TreeSnapshot& incoming, WorkspaceController* origin) {
  if (data->ready && incoming.revision < data->tree.revision) return;
  data->tree = incoming; data->ready = true;
  for (const auto& state : Windows())
    if (state->workspace_data == data && state->workspace && state->workspace.get() != origin) state->workspace->ReceiveSharedTree(data->tree);
}
void AppSession::AddWorkspace(const std::shared_ptr<WorkspaceData>& data) { data_scopes_.push_back(data); }
void AppSession::StopWorkspaces() {
  if (auto_archive) auto_archive->Stop();
  if (preferences) preferences->Stop();
  if (profiles) profiles->Stop();
  for (const auto& data : data_scopes_) StopData(data);
}
void AppSession::ReleaseUnusedWorkspaces() {
  const auto windows = Windows();
  std::erase_if(data_scopes_, [&](const auto& data) {
    if (data == workspace) return false;
    for (const auto& state : windows)
      if (state->workspace_data == data && (state->window || !state->browsers.empty())) return false;
    StopData(data); return true;
  });
}
void QuitIfLastWindow(const std::shared_ptr<State>& state) {
  if (state->session) state->session->ReleaseUnusedWorkspaces();
  if (!state->session || !state->session->HasOpenWindows()) CefQuitMessageLoop();
}
void RegisterSessionUi(const std::shared_ptr<State>& state, int browser) {
  if (state->session) state->session->RegisterUi(browser, state);
}
void UnregisterSessionUi(const std::shared_ptr<State>& state, int browser) {
  if (state->session) state->session->UnregisterUi(browser);
}
}
