#include "experiments/protocols_probe.h"
#include <windows.h>
#include <string>
#include <algorithm>
#include <vector>
#include "include/cef_parser.h"
#include "include/base/cef_callback.h"
#include "include/wrapper/cef_closure_task.h"
#include "workspace/controller.h"
namespace red {
namespace {
constexpr char kPdfViewer[] = "chrome-extension://mhjfbmdgcfjbbpaeojofohoefgiehjai/";

void Evaluate(CefRefPtr<CefBrowser> page, const std::string& script, bool gesture) {
  auto evaluate = CefDictionaryValue::Create();
  evaluate->SetString("expression", script); evaluate->SetBool("userGesture", gesture);
  page->GetHost()->ExecuteDevToolsMethod(0, "Runtime.evaluate", evaluate);
}
// Visible top-level windows of this process other than |except|.
std::vector<HWND> OtherWindows(HWND except) {
  struct Search { DWORD pid; HWND except; std::vector<HWND> found; } search{GetCurrentProcessId(), except, {}};
  EnumWindows([](HWND window, LPARAM param) -> BOOL {
    auto* s = reinterpret_cast<Search*>(param); DWORD pid = 0; GetWindowThreadProcessId(window, &pid);
    if (pid == s->pid && window != s->except && IsWindowVisible(window)) s->found.push_back(window);
    return TRUE;
  }, reinterpret_cast<LPARAM>(&search));
  return search.found;
}
void Later(const std::shared_ptr<State>& state, int delay, base::OnceCallback<void(const std::shared_ptr<State>&)> step) {
  CefPostDelayedTask(TID_UI, base::BindOnce([](std::weak_ptr<State> weak, base::OnceCallback<void(const std::shared_ptr<State>&)> step) {
    if (auto state = weak.lock()) std::move(step).Run(state);
  }, std::weak_ptr<State>(state), std::move(step)), delay);
}
void Finish(const std::shared_ptr<State>& state) {
  if (!state->content) return;
  const HWND root = state->window ? state->window->GetWindowHandle() : nullptr;
  auto windows = CefListValue::Create();
  for (const HWND window : OtherWindows(root)) {
    wchar_t title[256] = {}; GetWindowTextW(window, title, 256);
    windows->SetString(windows->GetSize(), CefString(title));
    PostMessageW(window, WM_CLOSE, 0, 0);  // Dismiss; never accept a launch.
  }
  auto data = CefDictionaryValue::Create();
  data->SetString("url", state->content->GetMainFrame()->GetURL().ToString().substr(0, 300));
  data->SetList("windows", windows);
  state->Record("protocols_after_external", data);
  // 5. Drop the local PDF on the page (DevTools drag events with a file, like
  // a drop from Explorer): it must open in a new tab with the PDF viewer.
  state->protocols_phase = 5;
  auto path = CefURIDecode(state->protocols_probe.substr(8), true,
      static_cast<cef_uri_unescape_rule_t>(UU_SPACES | UU_PATH_SEPARATORS | UU_URL_SPECIAL_CHARS_EXCEPT_PATH_SEPARATORS)).ToString();
  std::replace(path.begin(), path.end(), '/', '\\');
  for (const char* type : {"dragEnter", "dragOver", "drop"}) {
    auto drag = CefDictionaryValue::Create(); drag->SetString("type", type); drag->SetDouble("x", 200); drag->SetDouble("y", 200);
    auto files = CefListValue::Create(); files->SetString(0, path);
    auto items = CefListValue::Create();
    auto payload = CefDictionaryValue::Create(); payload->SetList("items", items); payload->SetList("files", files);
    payload->SetInt("dragOperationsMask", 1);
    drag->SetDictionary("data", payload);
    state->content->GetHost()->ExecuteDevToolsMethod(0, "Input.dispatchDragEvent", drag);
  }
}
}  // namespace

void OnProtocolsProbeFrame(const std::shared_ptr<State>& state, CefRefPtr<CefBrowser> page, const std::string& url, bool main) {
  if (state->protocols_probe.empty() || !state->workspace) return;
  if (!main && url.starts_with(kPdfViewer) && state->protocols_phase == 5) {
    state->Record("protocols_dropped_pdf_viewer");
    // 6. The same file dropped on the sidebar (shell document). DevTools drag
    // events may bypass CEF's OS-level OnDragEnter; the result is recorded
    // either way and the probe finishes after a short wait.
    state->protocols_phase = 6;
    const auto shell = state->browsers.find(state->shell_id);
    if (shell != state->browsers.end()) {
      auto path = CefURIDecode(state->protocols_probe.substr(8), true,
          static_cast<cef_uri_unescape_rule_t>(UU_SPACES | UU_PATH_SEPARATORS | UU_URL_SPECIAL_CHARS_EXCEPT_PATH_SEPARATORS)).ToString();
      std::replace(path.begin(), path.end(), '/', '\\');
      for (const char* type : {"dragEnter", "dragOver", "drop"}) {
        auto drag = CefDictionaryValue::Create(); drag->SetString("type", type); drag->SetDouble("x", 110); drag->SetDouble("y", 320);
        auto files = CefListValue::Create(); files->SetString(0, path);
        auto payload = CefDictionaryValue::Create(); payload->SetList("items", CefListValue::Create()); payload->SetList("files", files);
        payload->SetInt("dragOperationsMask", 1); drag->SetDictionary("data", payload);
        shell->second->GetHost()->ExecuteDevToolsMethod(0, "Input.dispatchDragEvent", drag);
      }
    }
    Later(state, 2000, base::BindOnce([](const std::shared_ptr<State>& s) { s->protocols_done = true; }));
    return;
  }
  if (!main && url.starts_with(kPdfViewer) && (state->protocols_phase == 1 || state->protocols_phase == 2)) {
    auto data = CefDictionaryValue::Create(); data->SetInt("phase", state->protocols_phase);
    state->Record("protocols_pdf_viewer", data);
    if (state->protocols_phase == 1) {
      state->protocols_phase = 2;
      // Like a typed path: the trusted navigation path accepts local files.
      Later(state, 500, base::BindOnce([](const std::shared_ptr<State>& s) {
        s->workspace->Navigate(s->protocols_probe, [weak = std::weak_ptr<State>(s)](int code, const std::string&, CefRefPtr<CefDictionaryValue>) {
          auto s = weak.lock(); if (!s) return;
          auto data = CefDictionaryValue::Create(); data->SetInt("code", code); s->Record("protocols_file_navigate", data);
        });
      }));
    } else {
      state->protocols_phase = 3;
      Later(state, 500, base::BindOnce([](const std::shared_ptr<State>& s) {
        s->content->GetMainFrame()->LoadURL(s->protocols_start);
      }));
    }
    return;
  }
  if (!main || !url.starts_with("http")) return;
  if (state->protocols_phase == 0 && url.ends_with("/start")) {
    state->protocols_phase = 1; state->protocols_start = url;
    // Same-document navigation (pushState, like YouTube links): record whether
    // the snapshot's back state follows without a load event.
    Evaluate(page, "history.pushState({}, '', '/start#spa')", true);
    Later(state, 600, base::BindOnce([](const std::shared_ptr<State>& s) {
      auto data = CefDictionaryValue::Create();
      data->SetBool("native_can_go_back", s->content->CanGoBack());
      s->Record("protocols_spa_back", data);
      s->content->GetMainFrame()->LoadURL(s->protocols_start.substr(0, s->protocols_start.rfind('/')) + "/doc.pdf");
    }));
  } else if (state->protocols_phase == 3 && url.ends_with("/start")) {
    state->protocols_phase = 4;
    // A website cannot open local files, even with a gesture.
    Evaluate(page, "location.href='file:///C:/Windows/win.ini'", true);
    Later(state, 1200, base::BindOnce([](const std::shared_ptr<State>& s) {
      auto data = CefDictionaryValue::Create();
      data->SetString("url", s->content->GetMainFrame()->GetURL().ToString().substr(0, 300));
      s->Record("protocols_after_file_attempt", data);
      // An app link the person clicked (unregistered scheme: nothing to launch).
      Evaluate(s->content, "location.href='red-probe-app:hello'", true);
      Later(s, 2000, base::BindOnce(&Finish));
    }));
  }
}
}  // namespace red
