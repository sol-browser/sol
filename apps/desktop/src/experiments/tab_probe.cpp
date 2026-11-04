#include "experiments/tab_probe.h"
#include "include/base/cef_callback.h"
#include "include/cef_id_mappers.h"
#include "include/cef_parser.h"
#include "include/wrapper/cef_closure_task.h"
#include "include/views/cef_browser_view.h"
#include "trust_policy.h"
#include "windows/embedded_content.h"

namespace red {
TabProbe::TabProbe(std::weak_ptr<State> state, std::string first_url)
    : state_(std::move(state)), second_url_(std::move(first_url) + "?second") {}

void TabProbe::Record(const std::string& event, CefRefPtr<CefDictionaryValue> details) {
  if (auto state = state_.lock()) state->Record(event, details);
}
void TabProbe::Later(int delay_ms, void (TabProbe::*method)()) {
  CefPostDelayedTask(TID_UI, base::BindOnce([](std::shared_ptr<TabProbe> self, void (TabProbe::*method)()) {
    if (self->stage_ != Stage::Failed) (self.get()->*method)();
  }, shared_from_this(), method), delay_ms);
}
void TabProbe::Fail(const std::string& reason) {
  stage_ = Stage::Failed;
  auto data = CefDictionaryValue::Create(); data->SetString("reason", reason);
  Record("tabs_failure", data);
}
bool TabProbe::Command(CefRefPtr<CefBrowser> browser, const char* name) {
  int id = cef_id_for_command_id_name(name);
  bool enabled = id >= 0 && browser && browser->GetHost()->CanExecuteChromeCommand(id);
  auto data = CefDictionaryValue::Create();
  data->SetString("name", name); data->SetInt("id", id); data->SetBool("enabled", enabled);
  Record("tabs_command", data);
  if (!enabled) { Fail(std::string("Chrome command unavailable: ") + name); return false; }
  browser->GetHost()->ExecuteChromeCommand(id, CEF_WOD_CURRENT_TAB);
  return true;
}
void TabProbe::OnCreated(CefRefPtr<CefBrowser> browser) {
  if (!first_) { first_ = browser; first_id_ = browser->GetIdentifier(); return; }
  if (stage_ != Stage::SecondCreate || second_) return;
  second_ = browser; second_id_ = browser->GetIdentifier();
  stage_ = Stage::SecondLoad;
  // OnAfterCreated precedes insertion into the tab model. Defer navigation and
  // window queries to the next task rather than assuming insertion is finished.
  CefPostTask(TID_UI, base::BindOnce([](std::shared_ptr<TabProbe> self) {
    if (!self->second_ || !self->first_) return;
    const HWND first_handle = self->first_->GetHost()->GetWindowHandle();
    const HWND second_handle = self->second_->GetHost()->GetWindowHandle();
    const bool valid_handles = IsWindow(first_handle) && IsWindow(second_handle);
    self->same_window_ = valid_handles && GetAncestor(first_handle, GA_ROOT) ==
        GetAncestor(second_handle, GA_ROOT);
    auto data = CefDictionaryValue::Create();
    data->SetInt("first_browser_id", self->first_id_);
    data->SetInt("second_browser_id", self->second_id_);
    data->SetBool("same_window", self->same_window_);
    data->SetBool("valid_window_handles", valid_handles);
    data->SetBool("second_has_cef_view", CefBrowserView::GetForBrowser(self->second_) != nullptr);
    self->Record("tabs_second_created", data);
    self->second_->GetMainFrame()->LoadURL(self->second_url_);
  }, shared_from_this()));
}
void TabProbe::OnLoad(CefRefPtr<CefBrowser> browser, int status) {
  if (status < 200 || status >= 400) return;
  const int id = browser->GetIdentifier();
  if (id == first_id_) {
    ++first_loads_;
    if (stage_ == Stage::FirstLoad) {
      if (auto state = state_.lock()) shell_id_ = state->shell_id.load();
      stage_ = Stage::FirstMarker;
      WriteMarker(browser, "first");
    }
  } else if (id == second_id_) {
    if (browser->GetMainFrame()->GetURL().ToString() != second_url_) return;
    ++second_loads_;
    if (stage_ == Stage::SecondLoad && browser->GetMainFrame()->GetURL().ToString() == second_url_) {
      stage_ = Stage::SecondMarker;
      WriteMarker(browser, "second");
    }
  }
}
void TabProbe::WriteMarker(CefRefPtr<CefBrowser> browser, const char* phase) {
  const std::string script = std::string("window.__redTabMarker=crypto.randomUUID();") +
      "document.querySelector('input').value='unsaved-" + phase + "';" +
      "console.info('RED_TABS:" + phase + "_written:'+JSON.stringify({nonce:window.__redTabMarker," +
      "value:document.querySelector('input').value}));";
  browser->GetMainFrame()->ExecuteJavaScript(script, browser->GetMainFrame()->GetURL(), 0);
}
void TabProbe::ReadMarker(CefRefPtr<CefBrowser> browser, const char* phase) {
  const std::string script = std::string("console.info('RED_TABS:") + phase +
      "_read:'+JSON.stringify({nonce:window.__redTabMarker,value:document.querySelector('input').value}));";
  browser->GetMainFrame()->ExecuteJavaScript(script, browser->GetMainFrame()->GetURL(), 0);
}
void TabProbe::OnConsole(CefRefPtr<CefBrowser> browser, const std::string& message) {
  constexpr std::string_view prefix = "RED_TABS:";
  if (!message.starts_with(prefix)) return;
  auto separator = message.find(':', prefix.size());
  if (separator == std::string::npos) return;
  auto phase = message.substr(prefix.size(), separator - prefix.size());
  auto json = CefParseJSON(message.substr(separator + 1), JSON_PARSER_RFC);
  auto data = json && json->GetType() == VTYPE_DICTIONARY ? json->GetDictionary() : nullptr;
  if (!data) return;
  auto details = data->Copy(false); details->SetString("phase", phase);
  details->SetInt("browser_id", browser->GetIdentifier());
  Record("tabs_marker", details);
  if (browser->GetIdentifier() == first_id_ && phase == "first_written" && stage_ == Stage::FirstMarker) {
    first_marker_ = data->GetString("nonce");
    Later(300, &TabProbe::CreateSecond);
  } else if (browser->GetIdentifier() == second_id_ && phase == "second_written" && stage_ == Stage::SecondMarker) {
    second_marker_ = data->GetString("nonce");
    Later(300, &TabProbe::ReturnFirst);
  } else if (browser->GetIdentifier() == first_id_ && phase == "first_read" && stage_ == Stage::FirstReturn) {
    first_preserved_ = !first_marker_.empty() && data->GetString("nonce") == first_marker_ &&
        data->GetString("value") == "unsaved-first" && first_loads_ == 1;
    Later(300, &TabProbe::ReturnSecond);
  } else if (browser->GetIdentifier() == second_id_ && phase == "second_read" && stage_ == Stage::SecondReturn) {
    second_preserved_ = !second_marker_.empty() && data->GetString("nonce") == second_marker_ &&
        data->GetString("value") == "unsaved-second" && second_loads_ == 1;
    Later(300, &TabProbe::CloseSecond);
  } else if (browser->GetIdentifier() == shell_id_ && phase == "shell" && stage_ == Stage::ShellCheck) {
    if (auto state = state_.lock()) shell_retained_ = state->shell_id == shell_id_ && data->GetBool("connected");
    stage_ = Stage::Done;
    Summarize();
  }
}
void TabProbe::CreateSecond() {
  stage_ = Stage::SecondCreate;
  if (auto state = state_.lock(); state && state->embedded) {
    if (!state->embedded->Create("about:blank")) Fail("Embedded browser creation rejected");
    return;
  }
  Command(first_, "IDC_NEW_TAB");
}
void TabProbe::ReturnFirst() {
  stage_ = Stage::FirstReturn;
  first_focused_ = false;
  if (auto state = state_.lock(); state && state->embedded) state->embedded->Activate(first_);
  else first_->GetHost()->SetFocus(true);
  CefPostDelayedTask(TID_UI, base::BindOnce([](std::shared_ptr<TabProbe> self) {
    if (self->first_) self->ReadMarker(self->first_, "first");
  }, shared_from_this()), 300);
}
void TabProbe::ReturnSecond() {
  stage_ = Stage::SecondReturn;
  second_focused_ = false;
  if (auto state = state_.lock(); state && state->embedded) state->embedded->Activate(second_);
  else second_->GetHost()->SetFocus(true);
  CefPostDelayedTask(TID_UI, base::BindOnce([](std::shared_ptr<TabProbe> self) {
    if (self->second_) self->ReadMarker(self->second_, "second");
  }, shared_from_this()), 300);
}
void TabProbe::CloseSecond() {
  stage_ = Stage::ClosingSecond;
  second_->GetHost()->CloseBrowser(false);
}
void TabProbe::OnClosed(int id) {
  if (id == second_id_) {
    second_ = nullptr;
    second_closed_ = true;
    if (stage_ == Stage::ClosingSecond) Later(300, &TabProbe::CheckShell);
  }
  if (id == first_id_) {
    first_ = nullptr;
    if (stage_ != Stage::Done && stage_ != Stage::Failed) Fail("Primary tab closed before probe completed");
  }
}
void TabProbe::OnFocus(int id) {
  if (id == first_id_ && stage_ == Stage::FirstReturn) first_focused_ = true;
  if (id == second_id_ && stage_ == Stage::SecondReturn) second_focused_ = true;
  auto data = CefDictionaryValue::Create(); data->SetInt("browser_id", id);
  Record("tabs_focus", data);
}
void TabProbe::CheckShell() {
  auto state = state_.lock();
  if (!state) return;
  const auto shell = state->browsers.find(shell_id_);
  if (shell == state->browsers.end()) { Fail("Original React shell disappeared"); return; }
  stage_ = Stage::ShellCheck;
  shell->second->GetMainFrame()->ExecuteJavaScript(
      "window.cefQuery({request:JSON.stringify({method:'browser.getSnapshot'}),persistent:false,"
      "onSuccess:()=>console.info('RED_TABS:shell:{\"connected\":true}'),"
      "onFailure:()=>console.info('RED_TABS:shell:{\"connected\":false}')})", std::string(kUiUrl), 0);
}
bool TabProbe::passed() const {
  return stage_ == Stage::Done && same_window_ && first_preserved_ && second_preserved_ &&
      first_focused_ && second_focused_ && second_closed_ && shell_retained_;
}
void TabProbe::Summarize() {
  auto data = CefDictionaryValue::Create();
  data->SetBool("pass", passed()); data->SetInt("stage", static_cast<int>(stage_));
  data->SetInt("first_loads", first_loads_); data->SetInt("second_loads", second_loads_);
  data->SetBool("same_window", same_window_); data->SetBool("first_preserved", first_preserved_);
  data->SetBool("second_preserved", second_preserved_); data->SetBool("first_focused", first_focused_);
  data->SetBool("second_focused", second_focused_); data->SetBool("second_closed", second_closed_);
  data->SetBool("shell_retained", shell_retained_);
  Record("tabs_result", data);
}
}  // namespace red
