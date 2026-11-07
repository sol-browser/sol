#include "windows/utility_window.h"
#include "windows/passwords_window.h"
#include "trust_policy.h"
#include "app/browsing_profiles.h"
#include "app/session.h"
#include "windows/branding.h"
#include "windows/window_close.h"
#include "workspace/controller.h"
#include "include/cef_client.h"
#include "include/views/cef_browser_view.h"
#include "include/views/cef_fill_layout.h"

namespace red {
namespace {
constexpr char kPasswords[] = "chrome://password-manager/passwords";
constexpr char kSettings[] = "chrome://password-manager/settings";
class Client final : public UtilityWindowClient, public CefLifeSpanHandler, public CefRequestHandler, public CefDisplayHandler {
 public:
  explicit Client(std::weak_ptr<PasswordsWindow> owner) : owner_(std::move(owner)) {}
  CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return this; }
  CefRefPtr<CefRequestHandler> GetRequestHandler() override { return this; }
  CefRefPtr<CefDisplayHandler> GetDisplayHandler() override { return this; }
  void OnTitleChange(CefRefPtr<CefBrowser>, const CefString&) override { if(auto owner=owner_.lock())owner->UpdateTitle(owner->title()); }
  void OnAfterCreated(CefRefPtr<CefBrowser> browser) override {
    if (auto owner = owner_.lock()) owner->BrowserCreated(browser);
    else browser->GetHost()->CloseBrowser(true);
  }
  void OnBeforeClose(CefRefPtr<CefBrowser> browser) override {
    if (auto owner = owner_.lock()) owner->BrowserClosed(browser->GetIdentifier());
  }
  bool OnBeforeBrowse(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame> frame, CefRefPtr<CefRequest> request, bool, bool) override {
    return frame->IsMain() && !IsPasswordManagerUrl(request->GetURL().ToString());
  }
  bool OnBeforePopup(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, int, const CefString&, const CefString&,
      cef_window_open_disposition_t, bool, const CefPopupFeatures&, CefWindowInfo&, CefRefPtr<CefClient>&,
      CefBrowserSettings&, CefRefPtr<CefDictionaryValue>&, bool*) override { return true; }
 private:
  std::weak_ptr<PasswordsWindow> owner_;
  IMPLEMENT_REFCOUNTING(Client);
};
class View final : public CefBrowserViewDelegate {
 public: cef_runtime_style_t GetBrowserRuntimeStyle() override { return CEF_RUNTIME_STYLE_CHROME; }
 private: IMPLEMENT_REFCOUNTING(View);
};
class Window final : public UtilityWindowDelegate {
 public:
  Window(std::weak_ptr<State> state, std::weak_ptr<PasswordsWindow> owner, CefRefPtr<CefBrowserView> view)
      : UtilityWindowDelegate(std::move(state), CefSize(1000, 760)), owner_(std::move(owner)), view_(view) {}
  cef_runtime_style_t GetWindowRuntimeStyle() override { return CEF_RUNTIME_STYLE_CHROME; }
  CefSize GetMinimumSize(CefRefPtr<CefView>) override { return CefSize(740, 520); }
  void OnWindowCreated(CefRefPtr<CefWindow> window) override {
    UtilityWindowDelegate::OnWindowCreated(window);
    if (auto owner = owner_.lock()) window->SetTitle(owner->title());
    const auto icon = RedWindowIcon(); window->SetWindowIcon(icon); window->SetWindowAppIcon(icon);
    window->SetToFillLayout(); window->AddChildView(view_);
    if (auto owner = owner_.lock()) owner->Created(window);
    else window->Close();
  }
  bool CanClose(CefRefPtr<CefWindow>) override {
    const auto browser = view_ ? view_->GetBrowser() : nullptr;
    return !browser || browser->GetHost()->TryCloseBrowser();
  }
  void OnWindowDestroyed(CefRefPtr<CefWindow>) override {
    view_ = nullptr; if (auto owner = owner_.lock()) owner->Destroyed();
  }
 private:
  std::weak_ptr<PasswordsWindow> owner_;
  CefRefPtr<CefBrowserView> view_;
  IMPLEMENT_REFCOUNTING(Window);
};
}
bool ShowPasswords(const std::shared_ptr<State>& state, const std::string& profile, bool settings) {
  if (!state || state->private_window || state->standalone_window || !state->session || !state->session->profiles ||
      !state->workspace || state->workspace->WindowClosing()) return false;
  const auto snapshot = state->workspace->Snapshot();
  const auto profiles = snapshot->GetList("profiles");
  if (!profiles) return false;
  std::string name;
  bool found = false;
  for (size_t i = 0; i < profiles->GetSize(); ++i) {
    const auto item = profiles->GetDictionary(i);
    if (item->GetString("id") == profile) { name = item->GetString("name"); found = true; break; }
  }
  if (!found) return false;
  const auto identity = state->session->profiles->Get(state, profile);
  if (!identity || !identity->context) return false;
  auto& manager = state->password_windows[profile];
  if (!manager) manager = std::make_shared<PasswordsWindow>(state, identity->context, "Passwords — " + name + " — Sol");
  manager->UpdateTitle("Passwords — " + name + " — Sol");
  identity->WhenReady([weak=std::weak_ptr<PasswordsWindow>(manager), settings] { if(auto owner=weak.lock())owner->Show(settings); });
  return true;
}
void PasswordsWindow::Show(bool settings) {
  const auto state = state_.lock();
  if (closing_ || !state || !state->window || !state->workspace || state->workspace->WindowClosing()) return;
  const auto url = settings ? kSettings : kPasswords;
  if (window_) {
    if (browser_) browser_->GetMainFrame()->LoadURL(url);
    PresentUtilityWindow(window_, state->window); return;
  }
  if (creating_) return;
  creating_ = true;
  auto view = CefBrowserView::CreateBrowserView(new Client(weak_from_this()), url, CefBrowserSettings(), nullptr, context_, new View());
  if(!view) { creating_=false;state->workspace->Notify("Could not open the password manager.");return; }
  CefWindow::CreateTopLevelWindow(new Window(state_, weak_from_this(), view));
  state->Record("password_manager_opened");  // No URLs, accounts, secrets or list metadata.
}
void PasswordsWindow::UpdateTitle(const std::string& title) { title_ = title; if(window_)window_->SetTitle(title_); }
void PasswordsWindow::Created(CefRefPtr<CefWindow> window) {
  window_ = window; creating_ = false;
  if (closing_) window_->Close();
  else { const auto state=state_.lock(); PresentUtilityWindow(window_, state ? state->window : nullptr); }
}
void PasswordsWindow::Destroyed() { window_ = nullptr; creating_ = false; closing_ = false; }
void PasswordsWindow::BrowserCreated(CefRefPtr<CefBrowser> browser) {
  browser_ = browser;
  if (auto state = state_.lock()) state->browsers[browser->GetIdentifier()] = browser;
  if (closing_) browser->GetHost()->CloseBrowser(false);
}
void PasswordsWindow::BrowserClosed(int id) {
  browser_ = nullptr;
  if (auto state = state_.lock()) {
    state->browsers.erase(id);
    if (state->window_close) state->window_close->OnClosed(id);
  }
}
void PasswordsWindow::Close() { closing_ = creating_ || window_; if (window_) window_->Close(); }
}  // namespace red
