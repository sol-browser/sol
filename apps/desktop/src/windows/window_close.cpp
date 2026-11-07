#include "windows/browser_dialogs.h"
#include "windows/window_close.h"
#include "windows/tab_drag_preview.h"
#include <windows.h>
#include "workspace/controller.h"
#include "windows/command_bar.h"
#include "windows/panel_popup.h"
#include "windows/menu_popup.h"
#include "windows/extension_popup.h"
#include "windows/extensions_window.h"
#include "windows/passwords_window.h"
#include "windows/sidebar_peek.h"
#include "windows/sidebar_menu.h"
#include "include/base/cef_callback.h"
#include "include/wrapper/cef_closure_task.h"
namespace red {
bool WindowClose::CanClose() {
  auto state = state_.lock();
  if (!state) return true;
  auto debug = CefDictionaryValue::Create(); debug->SetInt("phase", static_cast<int>(sequence_.phase()));
  debug->SetInt("shell_id", state->shell_id); state->Record("window_close_requested", debug);
  if (sequence_.phase() == CloseSequence::Phase::Shell) {
    return std::all_of(state->browsers.begin(), state->browsers.end(), [&](const auto& item) { return item.first == state->shell_id; });
  }
  // CEF's close request for a docked DevTools frontend that closed itself
  // (its renderer crashed) arrives right after that frontend's OnBeforeClose;
  // it is not the user closing the window. docs/devtools.md.
  if (sequence_.phase() == CloseSequence::Phase::Idle && !queued_ && state->docked_devtools_closed_ms &&
      GetTickCount64() - state->docked_devtools_closed_ms < 1000) {
    state->Record("window_close_ignored");
    return false;
  }
  if (sequence_.phase() == CloseSequence::Phase::Idle && !queued_) {
    queued_ = true;
    CefPostTask(TID_UI, base::BindOnce([](std::weak_ptr<WindowClose> weak) {
      if (auto self = weak.lock()) { self->queued_ = false; self->Begin(); }
    }, weak_from_this()));
  }
  return false;
}
void WindowClose::Begin() {
  auto state = state_.lock();
  if (!state || !state->window || !state->workspace || !state->popup_pending.empty()) return;
  HideTabDragPreview(state); state->tab_dragging = false;
  std::vector<int> pages;
  if (state->sidebar_menu) state->sidebar_menu->Cancel();
  if (state->command_bar) state->command_bar->Hide(false);
  if (state->panel_popup) state->panel_popup->Hide(false);
  if (state->menu_popup) state->menu_popup->Close(false);
  if (state->extension_popup) state->extension_popup->Close();
  if (state->extensions_window) state->extensions_window->Close();
  for (const auto& [id, manager] : state->password_windows) manager->Close();
  if (state->sidebar_peek) state->sidebar_peek->SetEnabled(false);
  for (const auto& [id, browser] : state->browsers)
    if (id != state->shell_id && id != state->command_id && id != state->panel_id && id != state->peek_id &&
        id != state->menu_id[0] && id != state->menu_id[1] && id != state->menu_id[2] && !state->devtools_targets.contains(id)) pages.push_back(id);
  if (!sequence_.Begin(std::move(pages), state->shell_id)) return;
  if (!state->workspace->PrepareShutdown([weak = weak_from_this()](bool ok) {
    if (auto self = weak.lock()) self->Prepared(ok);
  })) sequence_.Cancel();
}
void WindowClose::Prepared(bool success) {
  auto state = state_.lock();
  if (!state || !state->window) return;
  if (!success) { sequence_.Cancel(); state->workspace->CancelShutdown(); return; }
  sequence_.Prepared();
  Advance();
}
void WindowClose::Advance() {
  auto state = state_.lock();
  if (!state || !state->window) return;
  const auto id = sequence_.Next();
  if (id < 0) return;
  const auto browser = state->browsers.find(id);
  if (browser == state->browsers.end()) { sequence_.Closed(id); QueueAdvance(); return; }
  auto details = CefDictionaryValue::Create(); details->SetInt("browser_id", id);
  details->SetBool("shell", id == state->shell_id); state->Record("window_close_started", details);
  // No force-close: CEF dispatches its real unload lifecycle; the page's
  // beforeunload dialog for this close is accepted without asking.
  // The trusted UI has no unload handler or website state. Destroy its Views
  // hierarchy only after every untrusted browser's OnBeforeClose completed.
  // Explicitly closing the Alloy BrowserView first can leave its host waiting
  // for the same parent teardown it is supposed to authorize.
  if (id == state->shell_id) {
    if (state->command_bar) state->command_bar->Shutdown();
    if (state->panel_popup) state->panel_popup->Shutdown();
    if (state->menu_popup) state->menu_popup->Shutdown();
    if (state->sidebar_peek) state->sidebar_peek->Shutdown();
    state->window->Close();
  }
  else CloseBrowserWithDialogs(state_, browser->second, false);
}
void WindowClose::QueueAdvance() {
  if (queued_) return;
  queued_ = true;
  CefPostTask(TID_UI, base::BindOnce([](std::weak_ptr<WindowClose> weak) {
    if (auto self = weak.lock()) { self->queued_ = false; self->Advance(); }
  }, weak_from_this()));
}
void WindowClose::OnClosed(int browser) {
  if (sequence_.phase() == CloseSequence::Phase::Idle) return;
  sequence_.Closed(browser);
  if (sequence_.phase() == CloseSequence::Phase::Shell) {
    if (auto state = state_.lock(); state && state->window) state->window->Close();
  } else if (sequence_.phase() == CloseSequence::Phase::Pages) QueueAdvance();
}
void WindowClose::Cancel(int browser) {
  if (sequence_.phase() != CloseSequence::Phase::Pages || sequence_.waiting() != browser) return;
  sequence_.Cancel();
  if (auto state = state_.lock()) { ++state->window_close_cancelled; state->workspace->CancelShutdown(); state->Record("window_close_cancelled"); }
}
}  // namespace red
