#include "experiments/window_transfer.h"
#include <algorithm>
#include <array>
#include "include/base/cef_callback.h"
#include "include/cef_app.h"
#include "include/cef_parser.h"
#include "include/views/cef_window.h"
#include "include/views/cef_window_delegate.h"
#include "include/wrapper/cef_closure_task.h"
namespace red {
namespace {
class Transfer final : public CefClient, public CefLifeSpanHandler,
    public CefLoadHandler, public CefDisplayHandler, public CefRequestHandler, public CefWindowDelegate {
 public:
  explicit Transfer(std::shared_ptr<State> state) : state_(std::move(state)) {}
  ~Transfer() override { for (auto root : roots_) if (IsWindow(root)) DestroyWindow(root); }
  bool Start(const std::string& url) {
    CefURLParts parts;
    if (!CefParseURL(url, parts) || CefString(&parts.scheme) != "http" || CefString(&parts.host) != "127.0.0.1") return false;
    url_ = url;
    alloy_ = CefCommandLine::GetGlobalCommandLine()->GetSwitchValue("probe-window-transfer").ToString().starts_with("alloy");
    WNDCLASSW wc{}; wc.lpfnWndProc = Proc; wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"RedWindowTransferExperiment"; wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    if (!RegisterClassW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;
    for (int i = 0; i < 2; ++i) {
      if (alloy_) {
        windows_[i] = CefWindow::CreateTopLevelWindow(this);
        roots_[i] = windows_[i]->GetWindowHandle();
        windows_[i]->SetTitle(i ? "Sol transfer Alloy — B" : "Sol transfer Alloy — A");
        windows_[i]->SetBounds(CefRect(80 + i * 560, 100, 550, 680));
        windows_[i]->Show();
        continue;
      }
      roots_[i] = CreateWindowExW(0, wc.lpszClassName, i ? L"Sol transfer — B" : L"Sol transfer — A",
          WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, 80 + i * 560, 100, 550, 680,
          nullptr, nullptr, wc.hInstance, this);
      if (!roots_[i]) return false;
      ShowWindow(roots_[i], SW_SHOWNORMAL);
    }
    CefWindowInfo info; info.SetAsChild(roots_[0], Bounds()); info.runtime_style = CEF_RUNTIME_STYLE_CHROME;
    return CefBrowserHost::CreateBrowser(info, this, url_, CefBrowserSettings(), nullptr, CefRequestContext::GetGlobalContext());
  }
  cef_runtime_style_t GetWindowRuntimeStyle() override { return CEF_RUNTIME_STYLE_ALLOY; }
  bool CanClose(CefRefPtr<CefWindow> window) override {
    if (closing_roots_ || (browser_ && GetParent(browser_->GetHost()->GetWindowHandle()) != window->GetWindowHandle())) return true;
    Close(); return false;
  }
  void OnWindowBoundsChanged(CefRefPtr<CefWindow> window, const CefRect&) override {
    if (window->GetWindowHandle() == roots_[owner_]) Layout();
  }
  void OnWindowDestroyed(CefRefPtr<CefWindow> window) override {
    state_->Record("window_transfer_cef_root_destroyed");
    for (int i = 0; i < 2; ++i) if (windows_[i] && windows_[i]->IsSame(window)) { windows_[i] = nullptr; roots_[i] = nullptr; }
    if (closing_roots_ && !windows_[0] && !windows_[1]) Finish();
  }
  CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return this; }
  CefRefPtr<CefLoadHandler> GetLoadHandler() override { return this; }
  CefRefPtr<CefDisplayHandler> GetDisplayHandler() override { return this; }
  CefRefPtr<CefRequestHandler> GetRequestHandler() override { return this; }
  bool OnBeforeBrowse(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame> frame, CefRefPtr<CefRequest> request, bool, bool) override {
    const auto url = request->GetURL().ToString();
    return !frame->IsMain() || (url != url_ && !url.starts_with(url_ + "#"));
  }
  bool OnBeforePopup(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, int, const CefString&, const CefString&,
      cef_window_open_disposition_t, bool, const CefPopupFeatures&, CefWindowInfo&, CefRefPtr<CefClient>&,
      CefBrowserSettings&, CefRefPtr<CefDictionaryValue>&, bool*) override { return true; }
  void OnAfterCreated(CefRefPtr<CefBrowser> browser) override {
    browser_ = browser; id_ = browser->GetIdentifier(); Layout();
    if (closing_) browser_->GetHost()->CloseBrowser(false);
  }
  void OnBeforeClose(CefRefPtr<CefBrowser>) override {
    state_->Record("window_transfer_before_close");
    if (!closing_) { state_->Record("window_transfer_unexpected_close"); state_->exit_code = 1; }
    browser_ = nullptr;
    // Leave Chromium's close callback before destroying the native ancestor.
    CefPostTask(TID_UI, base::BindOnce([](CefRefPtr<Transfer> self) {
      if (self->alloy_) {
        self->closing_roots_ = true;
        auto windows = self->windows_;
        for (auto& window : windows) if (window) window->Close();
      } else {
        for (auto& root : self->roots_) if (root) { DestroyWindow(root); root = nullptr; }
        self->Finish();
      }
    }, CefRefPtr<Transfer>(this)));
  }
  void OnRenderProcessTerminated(CefRefPtr<CefBrowser>, TerminationStatus, int, const CefString&) override { Fail("renderer terminated"); }
  void OnLoadEnd(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, int status) override {
    if (!frame->IsMain() || status != 200) return;
    if (++loads_ != 1) { Fail("document reloaded"); return; }
    browser->GetMainFrame()->ExecuteJavaScript(R"JS(
      window.transferToken = crypto.randomUUID();
      document.querySelector('input').value = 'unsaved-' + transferToken;
      history.pushState({token:transferToken}, '', '#kept'); scrollTo(0, 240);
      setTimeout(() => { window.transferExpected = JSON.stringify({token:transferToken,
        input:document.querySelector('input').value, scroll:scrollY, history:history.length});
        console.log('RED_TRANSFER_READY'); }, 200);
    )JS", url_, 0);
  }
  bool OnConsoleMessage(CefRefPtr<CefBrowser>, cef_log_severity_t, const CefString& message, const CefString&, int) override {
    const auto text = message.ToString();
    if (text == "RED_TRANSFER_READY" && stage_ == 0) { stage_ = 1; Schedule(); }
    else if (text == "RED_TRANSFER_OK" && stage_ > 0 && stage_ <= 5) {
      auto data = CefDictionaryValue::Create(); data->SetInt("stage", stage_);
      data->SetInt("browser_id", id_); data->SetInt("loads", loads_);
      state_->Record("window_transfer_preserved", data);
      if (stage_ == 5) {
        state_->Record("window_transfer_pass");
        SetWindowTextW(roots_[owner_], L"Sol transfer — PASS; source closed, verify typing/clicks");
        if (CefCommandLine::GetGlobalCommandLine()->GetSwitchValue("probe-window-transfer").ToString().ends_with("auto"))
          CefPostDelayedTask(TID_UI, base::BindOnce([](CefRefPtr<Transfer> self) { self->Close(); }, CefRefPtr<Transfer>(this)), 1000);
      } else { ++stage_; Schedule(); }
    } else if (text == "RED_TRANSFER_FAIL") Fail("live document state changed");
    return false;
  }
 private:
  CefRect Bounds() const {
    RECT r{}; GetClientRect(roots_[owner_], &r);
    return {0, 0, static_cast<int>(std::max(1L, r.right)), static_cast<int>(std::max(1L, r.bottom))};
  }
  void Layout() {
    if (!browser_) return;
    const auto r = Bounds();
    SetWindowPos(browser_->GetHost()->GetWindowHandle(), HWND_TOP, 0, 0, r.width, r.height, SWP_NOACTIVATE | SWP_SHOWWINDOW);
  }
  bool Move(int target) {
    if (!browser_ || !roots_[target] || closing_) return false;
    HWND child = browser_->GetHost()->GetWindowHandle(); browser_->GetHost()->SetFocus(false);
    // Pinned cefclient ShowPopup uses SetParent + SetWindowPos. Test only;
    // production ownership, IPC and EmbeddedContent are deliberately untouched.
    SetLastError(ERROR_SUCCESS);
    const auto previous = SetParent(child, roots_[target]);
    if ((!previous && GetLastError() != ERROR_SUCCESS) || GetParent(child) != roots_[target]) { Fail("SetParent failed"); return false; }
    owner_ = target; Layout(); browser_->GetHost()->NotifyMoveOrResizeStarted();
    SetForegroundWindow(roots_[owner_]); SetFocus(child); browser_->GetHost()->SetFocus(true);
    return true;
  }
  void Schedule() {
    CefPostDelayedTask(TID_UI, base::BindOnce([](CefRefPtr<Transfer> self) {
      if (!self->browser_ || self->closing_) return;
      if (!self->Move(self->stage_ <= 4 ? self->stage_ % 2 : 1)) return;
      if (self->stage_ == 5) {
        if (self->alloy_) self->windows_[0]->Close();
        else { DestroyWindow(self->roots_[0]); self->roots_[0] = nullptr; }
      }
      CefPostDelayedTask(TID_UI, base::BindOnce([](CefRefPtr<Transfer> self) {
        if (!self->browser_ || self->closing_) return;
        if (self->browser_->GetIdentifier() != self->id_) { self->Fail("browser identity changed"); return; }
        self->browser_->GetMainFrame()->ExecuteJavaScript(R"JS(
          requestAnimationFrame(() => console.log(JSON.stringify({token:window.transferToken,
            input:document.querySelector('input').value,scroll:scrollY,history:history.length}) ===
            window.transferExpected ? 'RED_TRANSFER_OK' : 'RED_TRANSFER_FAIL'));
        )JS", self->url_, 0);
      }, self), 300);
    }, CefRefPtr<Transfer>(this)), 300);
  }
  void Fail(const std::string& reason) {
    auto data = CefDictionaryValue::Create(); data->SetString("reason", reason);
    state_->Record("window_transfer_failure", data); state_->exit_code = 1; Close();
  }
  void Finish() { state_->Record("window_transfer_roots_closed"); CefQuitMessageLoop(); }
  void Close() {
    if (!closing_) {
      closing_ = true; state_->Record("window_transfer_close_requested");
      if (browser_) browser_->GetHost()->CloseBrowser(false);
    }
  }
  static LRESULT CALLBACK Proc(HWND hwnd, UINT message, WPARAM w, LPARAM l) {
    auto self = reinterpret_cast<Transfer*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
      self = static_cast<Transfer*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);
      SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    if (self) {
      if (message == WM_SIZE && hwnd == self->roots_[self->owner_]) self->Layout();
      if (message == WM_CLOSE) { self->Close(); return 0; }
    }
    return DefWindowProcW(hwnd, message, w, l);
  }
  std::shared_ptr<State> state_; CefRefPtr<CefBrowser> browser_;
  HWND roots_[2]{}; std::string url_;
  std::array<CefRefPtr<CefWindow>, 2> windows_{};
  int id_ = -1, owner_ = 0, stage_ = 0, loads_ = 0; bool closing_ = false;
  bool alloy_ = false, closing_roots_ = false;
  IMPLEMENT_REFCOUNTING(Transfer);
};
}
void StartWindowTransfer(const std::shared_ptr<State>& state, const std::string& url) {
  CefRefPtr<Transfer> experiment = new Transfer(state);
  if (!experiment->Start(url)) { state->exit_code = 1; CefQuitMessageLoop(); }
}
}  // namespace red
