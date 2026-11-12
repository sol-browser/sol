#include "windows/utility_window.h"
#include "web_url.h"
#include "windows/extensions_window.h"
#include "include/cef_request_context.h"
#include "include/views/cef_browser_view.h"
#include "include/views/cef_fill_layout.h"
#include "trust_policy.h"
#include "windows/branding.h"
#include "workspace/controller.h"

namespace red {
namespace {
constexpr char kExtensionsUrl[] = "chrome://extensions/";

class ExtensionsClient final : public UtilityWindowClient, public CefLifeSpanHandler, public CefRequestHandler {
 public:
  explicit ExtensionsClient(std::weak_ptr<State> state) : state_(std::move(state)) {}
  CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return this; }
  CefRefPtr<CefRequestHandler> GetRequestHandler() override { return this; }
  bool OnBeforePopup(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, int, const CefString& url, const CefString&,
      cef_window_open_disposition_t, bool, const CefPopupFeatures&, CefWindowInfo&, CefRefPtr<CefClient>&,
      CefBrowserSettings&, CefRefPtr<CefDictionaryValue>&, bool*) override {
    Open(url.ToString()); return true;
  }
  bool OnBeforeBrowse(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame> frame, CefRefPtr<CefRequest> request, bool, bool) override {
    const auto url = request->GetURL().ToString();
    if (!frame->IsMain() || url.starts_with(kExtensionsUrl) || url == "chrome://extensions") return false;
    Open(url); return true;
  }
 private:
  void Open(const std::string& url) {
    auto state = state_.lock(); if (!state || !state->workspace) return;
    if (HasWebScheme(url)) state->workspace->OpenLink(url, true);
    else if (IsExtensionPageUrl(url)) state->workspace->AdoptExtensionPage(url);
  }
  std::weak_ptr<State> state_;
  IMPLEMENT_REFCOUNTING(ExtensionsClient);
};
class ChromeView final : public CefBrowserViewDelegate {
 public: cef_runtime_style_t GetBrowserRuntimeStyle() override { return CEF_RUNTIME_STYLE_CHROME; }
 private: IMPLEMENT_REFCOUNTING(ChromeView);
};
class ExtensionsWindowDelegate final : public UtilityWindowDelegate {
 public:
  ExtensionsWindowDelegate(std::weak_ptr<State> state, std::weak_ptr<ExtensionsWindow> owner, CefRefPtr<CefBrowserView> view)
      : UtilityWindowDelegate(std::move(state), CefSize(980, 720)), owner_(std::move(owner)), view_(view) {}
  cef_runtime_style_t GetWindowRuntimeStyle() override { return CEF_RUNTIME_STYLE_CHROME; }
  void OnWindowCreated(CefRefPtr<CefWindow> window) override {
    UtilityWindowDelegate::OnWindowCreated(window);
    window->SetTitle("Extensions — Sol");
    const auto icon = RedWindowIcon(); window->SetWindowIcon(icon); window->SetWindowAppIcon(icon);
    window->SetToFillLayout(); window->AddChildView(view_);
    if (auto owner = owner_.lock()) owner->Created(window);
    PresentUtilityWindow(window, parent()); view_->RequestFocus();
  }
  bool CanClose(CefRefPtr<CefWindow>) override {
    const auto browser = view_ ? view_->GetBrowser() : nullptr;
    return !browser || browser->GetHost()->TryCloseBrowser();
  }
  void OnWindowDestroyed(CefRefPtr<CefWindow> window) override { view_ = nullptr; if (auto owner = owner_.lock()) owner->Destroyed(window); }
 private:
  std::weak_ptr<ExtensionsWindow> owner_;
  CefRefPtr<CefBrowserView> view_;
  IMPLEMENT_REFCOUNTING(ExtensionsWindowDelegate);
};
}  // namespace

void ExtensionsWindow::Show() {
  auto state = state_.lock(); if (!state || !state->window) return;
  if (window_) { PresentUtilityWindow(window_, state->window); return; }
  auto view = CefBrowserView::CreateBrowserView(new ExtensionsClient(state_), kExtensionsUrl, CefBrowserSettings(), nullptr,
      state->WebContext(), new ChromeView());
  CefWindow::CreateTopLevelWindow(new ExtensionsWindowDelegate(state_, weak_from_this(), view));
  state->Record("extensions_window_opened");
}
void ExtensionsWindow::Close() { if (auto window = window_) window->Close(); }
}  // namespace red
