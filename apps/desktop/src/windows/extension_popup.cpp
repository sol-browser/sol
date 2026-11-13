#include "windows/extension_popup.h"
#include "web_url.h"
#include <dwmapi.h>
#include <algorithm>
#include "experiments/capture_mute_probe.h"
#include "experiments/extension_popup_probe.h"
#include "extensions/extension_manifest.h"
#include "extensions/extension_catalog.h"
#include "include/cef_parser.h"
#include "include/cef_request_context.h"
#include "include/views/cef_display.h"
#include "include/views/cef_fill_layout.h"
#include "workspace/controller.h"

namespace red {
namespace {
// Chrome's extension popup limits and Sol's initial window size.
const CefSize kMinimum(25, 25), kMaximum(800, 600), kInitialSize(320, 120);

std::string Origin(const std::string& id) { return "chrome-extension://" + id + "/"; }
bool IsWebLink(const std::string& url) { return HasWebScheme(url); }

class PopupClient final : public CefClient, public CefLifeSpanHandler, public CefRequestHandler, public CefDisplayHandler,
    public CefKeyboardHandler, public CefContextMenuHandler, public CefLoadHandler {
 public:
  PopupClient(std::weak_ptr<ExtensionPopup> owner, std::weak_ptr<State> state, std::string id)
      : owner_(std::move(owner)), state_(std::move(state)), id_(std::move(id)) {}
  CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return this; }
  CefRefPtr<CefRequestHandler> GetRequestHandler() override { return this; }
  CefRefPtr<CefDisplayHandler> GetDisplayHandler() override { return this; }
  CefRefPtr<CefKeyboardHandler> GetKeyboardHandler() override { return this; }
  CefRefPtr<CefContextMenuHandler> GetContextMenuHandler() override { return this; }
  CefRefPtr<CefLoadHandler> GetLoadHandler() override { return this; }
  void OnAfterCreated(CefRefPtr<CefBrowser> browser) override {
    if (auto owner = owner_.lock()) owner->SetBrowser(browser->GetIdentifier());
    browser->GetHost()->SetAutoResizeEnabled(true, kMinimum, kMaximum);
  }
  // Links and window.open from the popup open as workspace tabs, like Chrome
  // opens them in the window's tab strip; the popup then closes.
  bool OnBeforePopup(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, int, const CefString& url, const CefString&,
      cef_window_open_disposition_t, bool, const CefPopupFeatures&, CefWindowInfo&, CefRefPtr<CefClient>&,
      CefBrowserSettings&, CefRefPtr<CefDictionaryValue>&, bool*) override {
    OpenAsTab(url.ToString()); return true;
  }
  // The popup document stays inside its extension. Navigating elsewhere opens
  // a tab instead (a user's link click); everything else is cancelled.
  bool OnBeforeBrowse(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame> frame, CefRefPtr<CefRequest> request, bool gesture, bool) override {
    const auto url = request->GetURL().ToString();
    if (!frame->IsMain() || url.starts_with(Origin(id_))) return false;
    if (gesture) OpenAsTab(url);
    return true;
  }
  bool OnAutoResize(CefRefPtr<CefBrowser> browser, const CefSize& size) override {
    const auto owner = owner_.lock();
    if (!owner || !owner->Owns(browser->GetIdentifier()) || auto_resize_disabled_) return true;
    // A fluid extension page can keep getting narrower while its height is
    // already capped. Recover when it hits the width floor after requesting
    // a larger width at that height. A stable, deliberately narrow popup still
    // uses its requested dimensions.
    if (size.height == kMaximum.height && size.width == kMinimum.width &&
        previous_size_.height == size.height && previous_size_.width > size.width &&
        previous_size_.width < kInitialSize.width) {
      auto_resize_disabled_ = true;
      // Lay out this popup in a fixed viewport. Intrinsic sizing is what made
      // its fluid page collapse, so keep it off for this popup's lifetime.
      browser->GetHost()->SetAutoResizeEnabled(false, CefSize(), CefSize());
      owner->Resize(CefSize(kInitialSize.width, size.height));
      return true;
    }
    previous_size_ = size;
    owner->Resize(size);
    return true;
  }
  bool OnPreKeyEvent(CefRefPtr<CefBrowser>, const CefKeyEvent& event, CefEventHandle, bool*) override {
    if (event.type != KEYEVENT_RAWKEYDOWN || event.windows_key_code != VK_ESCAPE) return false;
    if (auto owner = owner_.lock()) owner->Close(); return true;
  }
  // No Back/Reload/View source on an extension popup; editing commands stay.
  void OnBeforeContextMenu(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, CefRefPtr<CefContextMenuParams> params,
                           CefRefPtr<CefMenuModel> model) override {
    if (!(params->GetTypeFlags() & (CM_TYPEFLAG_EDITABLE | CM_TYPEFLAG_SELECTION))) model->Clear();
  }
  void OnLoadEnd(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, int status) override {
    auto state = state_.lock(); if (!state || !state->probe || !frame->IsMain()) return;
    auto data = CefDictionaryValue::Create(); data->SetInt("status", status);
    data->SetBool("extension_origin", frame->GetURL().ToString().starts_with(Origin(id_)));
    data->SetInt("style", browser->GetHost()->GetRuntimeStyle());
    state->Record("ext_popup_loaded", data);
  }
  bool OnConsoleMessage(CefRefPtr<CefBrowser>, cef_log_severity_t, const CefString& message, const CefString&, int) override {
    // Probe evidence from the owned fixture extension only.
    auto state = state_.lock(); const auto text = message.ToString();
    if (state && state->probe && text.starts_with("RED_EXT_POPUP:")) {
      auto data = CefDictionaryValue::Create(); data->SetString("result", text.substr(14, 4000));
      state->Record("ext_popup_query", data);
    }
    if (state) ObserveCaptureMuteProbe(state, text);
    return false;
  }
 private:
  void OpenAsTab(const std::string& url) {
    auto state = state_.lock();
    if (!state || !state->workspace || !(IsWebLink(url) || url.starts_with(Origin(id_)))) return;
    // Same-extension pages are the extension's own request, like tabs.create.
    if (IsWebLink(url)) state->workspace->OpenLink(url, true); else state->workspace->AdoptExtensionPage(url);
    if (auto owner = owner_.lock()) owner->Close();
  }
  std::weak_ptr<ExtensionPopup> owner_;
  std::weak_ptr<State> state_;
  std::string id_;
  CefSize previous_size_;
  bool auto_resize_disabled_ = false;
  IMPLEMENT_REFCOUNTING(PopupClient);
};
class PopupView final : public CefBrowserViewDelegate {
 public: cef_runtime_style_t GetBrowserRuntimeStyle() override { return CEF_RUNTIME_STYLE_ALLOY; }
 private: IMPLEMENT_REFCOUNTING(PopupView);
};
class PopupWindow final : public CefWindowDelegate {
 public:
  PopupWindow(std::weak_ptr<ExtensionPopup> owner, std::weak_ptr<State> state, CefRefPtr<CefBrowserView> view)
      : owner_(std::move(owner)), state_(std::move(state)), view_(view) {}
  cef_runtime_style_t GetWindowRuntimeStyle() override { return CEF_RUNTIME_STYLE_ALLOY; }
  bool IsFrameless(CefRefPtr<CefWindow>) override { return true; }
  bool CanResize(CefRefPtr<CefWindow>) override { return false; }
  bool CanMaximize(CefRefPtr<CefWindow>) override { return false; }
  bool CanMinimize(CefRefPtr<CefWindow>) override { return false; }
  CefRefPtr<CefWindow> GetParentWindow(CefRefPtr<CefWindow>, bool* modal, bool*) override {
    *modal = false; auto state = state_.lock(); return state ? state->window : nullptr;
  }
  CefSize GetPreferredSize(CefRefPtr<CefView>) override { return kInitialSize; }
  void OnWindowCreated(CefRefPtr<CefWindow> window) override {
    window->SetTitle("Extension");
    const DWM_WINDOW_CORNER_PREFERENCE corners = DWMWCP_ROUND;
    DwmSetWindowAttribute(window->GetWindowHandle(), DWMWA_WINDOW_CORNER_PREFERENCE, &corners, sizeof(corners));
    window->SetToFillLayout(); window->AddChildView(view_);
    if (auto owner = owner_.lock()) owner->Created(window);
    window->Show();
    view_->RequestFocus();
  }
  // Like Chrome's popups: clicking anywhere else closes it.
  void OnWindowActivationChanged(CefRefPtr<CefWindow> window, bool active) override {
    if (!active) if (auto owner = owner_.lock()) owner->Close(window);
  }
  bool CanClose(CefRefPtr<CefWindow>) override {
    const auto browser = view_ ? view_->GetBrowser() : nullptr;
    return !browser || browser->GetHost()->TryCloseBrowser();
  }
  void OnWindowDestroyed(CefRefPtr<CefWindow> window) override {
    view_ = nullptr; if (auto owner = owner_.lock()) owner->Destroyed(window);
  }
 private:
  std::weak_ptr<ExtensionPopup> owner_;
  std::weak_ptr<State> state_;
  CefRefPtr<CefBrowserView> view_;
  IMPLEMENT_REFCOUNTING(PopupWindow);
};
}  // namespace

void ExtensionPopup::Open(const std::string& id, const std::string& path, const CefPoint& anchor) {
  auto state = state_.lock();
  if (!state || !state->window || !IsExtensionId(id) || !ExtensionRelativePath(path, true)) return;
  if (window_) {
    const bool same = id_ == id;
    Close();
    if (same) return;  // A second click on the same extension closes it.
  }
  id_ = id; anchor_ = anchor;
  size_ = kInitialSize;
  auto view = CefBrowserView::CreateBrowserView(new PopupClient(weak_from_this(), state_, id), Origin(id) + path,
      CefBrowserSettings(), nullptr, state->WebContext(), new PopupView());
  CefWindow::CreateTopLevelWindow(new PopupWindow(weak_from_this(), state_, view));
  auto data = CefDictionaryValue::Create(); data->SetString("id", id.substr(0, 8));
  state->Record("ext_popup_opened", data);
}
void ExtensionPopup::Created(CefRefPtr<CefWindow> window) { window_ = window; Place(); }
void ExtensionPopup::Close(CefRefPtr<CefWindow> which) {
  if (!window_ || (which && !which->IsSame(window_))) return;
  auto window = window_; window_ = nullptr;
  browser_id_ = -1;
  window->Close();
}
void ExtensionPopup::Resize(const CefSize& size) {
  size_ = CefSize(std::clamp(size.width, kMinimum.width, kMaximum.width), std::clamp(size.height, kMinimum.height, kMaximum.height));
  Place();
  if (auto state = state_.lock(); state && state->probe) {
    auto data = CefDictionaryValue::Create(); data->SetInt("width", size_.width); data->SetInt("height", size_.height);
    state->Record("ext_popup_resized", data);
  }
}
void ExtensionPopup::Place() {
  if (!window_) return;
  // Right-aligned to the anchor, kept inside the work area of its display.
  const auto area = CefDisplay::GetDisplayNearestPoint(anchor_, false)->GetWorkArea();
  const int x = std::clamp(anchor_.x - size_.width, area.x, std::max(area.x, area.x + area.width - size_.width));
  const int y = std::clamp(anchor_.y, area.y, std::max(area.y, area.y + area.height - size_.height));
  window_->SetBounds(CefRect(x, y, size_.width, size_.height));
}
void ExtensionPopup::Destroyed(CefRefPtr<CefWindow> window) {
  if (window_ && window_->IsSame(window)) { window_ = nullptr; browser_id_ = -1; }
  if (auto state = state_.lock()) {
    state->Record("ext_popup_closed");
    OnExtensionPopupProbeClosed(state);
  }
}
void RunExtension(const std::shared_ptr<State>& s, const std::string& id, const CefPoint& at) {
  const auto* item = s->extensions ? s->extensions->Find(id) : nullptr;
  if (!item) return;
#if CEF_API_ADDED(CEF_EXPERIMENTAL)
  if (item->action && s->content) {
    CefString popup_url;
    const int ran = s->content->GetHost()->RunExtensionAction(id, popup_url);
    const std::string origin = "chrome-extension://" + id + "/", url = popup_url.ToString();
    if (ran == 2) return;
    if (ran == 1 && url.starts_with(origin) && s->extension_popup) { s->extension_popup->Open(id, url.substr(origin.size()), at); return; }
  }
#endif
  if (!item->popup.empty() && s->extension_popup) s->extension_popup->Open(id, item->popup, at);
  else if (!item->options.empty() && s->workspace) s->workspace->AdoptExtensionPage("chrome-extension://" + id + "/" + item->options);
}
}  // namespace red
