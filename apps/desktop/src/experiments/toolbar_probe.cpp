#include "experiments/toolbar_probe.h"
#include <windows.h>
#include "include/base/cef_callback.h"
#include "include/views/cef_box_layout.h"
#include "include/wrapper/cef_closure_task.h"

namespace red {
namespace {
class ToolbarDelegate final : public CefBrowserViewDelegate {
 public:
  ToolbarDelegate(ToolbarProbe* probe, cef_chrome_toolbar_type_t type) : probe_(probe), type_(type) {}
  cef_runtime_style_t GetBrowserRuntimeStyle() override { return CEF_RUNTIME_STYLE_CHROME; }
  ChromeToolbarType GetChromeToolbarType(CefRefPtr<CefBrowserView>) override { return type_; }
  CefSize GetPreferredSize(CefRefPtr<CefView>) override { return CefSize(1200, 640); }
  // The toolbar exists only after the view joined a window (cef_browser_view.h).
  void OnWindowChanged(CefRefPtr<CefView> view, bool added) override {
    if (added && view->AsBrowserView()) probe_->OnToolbarAvailable(view->AsBrowserView());
  }
  void OnBrowserDestroyed(CefRefPtr<CefBrowserView>, CefRefPtr<CefBrowser> browser) override {
    probe_->OnViewBrowserDestroyed(browser->GetIdentifier());
  }
 private:
  ToolbarProbe* probe_;  // The probe outlives every view it creates.
  cef_chrome_toolbar_type_t type_;
  IMPLEMENT_REFCOUNTING(ToolbarDelegate);
};
}  // namespace

ToolbarProbe::ToolbarProbe(std::weak_ptr<State> state, cef_chrome_toolbar_type_t type, bool manual,
                           std::function<CefRefPtr<CefClient>()> client_factory)
    : state_(std::move(state)), type_(type), manual_(manual), client_factory_(std::move(client_factory)) {}

CefRefPtr<CefBrowserView> ToolbarProbe::CreateView(const std::string& url) {
  return CefBrowserView::CreateBrowserView(client_factory_(), url, CefBrowserSettings(), nullptr,
                                           CefRequestContext::GetGlobalContext(), new ToolbarDelegate(this, type_));
}

void ToolbarProbe::OnToolbarAvailable(CefRefPtr<CefBrowserView> view) {
  auto state = state_.lock();
  if (!state || !window_ && !state->window) return;
  auto toolbar = view->GetChromeToolbar();
  auto details = CefDictionaryValue::Create();
  details->SetBool("first_view", view == first_ || !first_);
  details->SetBool("toolbar", toolbar != nullptr);
  state->Record("chrome_toolbar_available", details);
  if (!toolbar || (toolbar_ && toolbar->IsSame(toolbar_))) return;
  auto window = window_ ? window_ : state->window;
  if (toolbar_) window->RemoveChildView(toolbar_);
  toolbar_ = toolbar;
  // Directly above its page; the shell is inserted at index 0 later or already.
  int index = 0;
  for (; index < static_cast<int>(window->GetChildViewCount()); ++index)
    if (window->GetChildViewAt(index)->IsSame(view)) break;
  window->AddChildViewAt(toolbar_, index);
  window->Layout();
}

void ToolbarProbe::OnViewBrowserDestroyed(int browser_id) {
  if (auto state = state_.lock()) {
    auto details = CefDictionaryValue::Create(); details->SetInt("browser_id", browser_id);
    state->Record("chrome_toolbar_view_browser_destroyed", details);
  }
}

void ToolbarProbe::Describe(const char* phase) {
  auto state = state_.lock();
  if (!state || !window_) return;
  auto details = CefDictionaryValue::Create();
  details->SetString("phase", phase);
  details->SetInt("children", static_cast<int>(window_->GetChildViewCount()));
  if (toolbar_) {
    const auto bounds = toolbar_->GetBounds();
    details->SetInt("toolbar_y", bounds.y); details->SetInt("toolbar_height", bounds.height); details->SetInt("toolbar_width", bounds.width);
  }
  for (auto [name, view] : {std::pair{"first", first_}, std::pair{"second", second_}}) {
    if (!view) continue;
    auto row = CefDictionaryValue::Create();
    row->SetBool("in_window", view->GetWindow() != nullptr);
    auto browser = view->GetBrowser();
    row->SetInt("browser_id", browser ? browser->GetIdentifier() : -1);
    row->SetInt("height", view->GetBounds().height);
    if (browser) row->SetString("url", browser->GetMainFrame() ? browser->GetMainFrame()->GetURL().ToString().substr(0, 200) : "");
    details->SetDictionary(name, row);
  }
  state->Record("chrome_toolbar_layout", details);
}

void ToolbarProbe::Swap(CefRefPtr<CefBrowserView> out, CefRefPtr<CefBrowserView> in, const char* phase) {
  if (!window_) return;
  window_->RemoveChildView(out);
  if (toolbar_) { window_->RemoveChildView(toolbar_); toolbar_ = nullptr; }
  window_->AddChildView(in);
  if (auto layout = window_->GetLayout(); layout && layout->AsBoxLayout()) layout->AsBoxLayout()->SetFlexForView(in, 1);
  // A view already holding a browser gets no OnWindowChanged-created toolbar
  // of its own here unless Chrome supplies one again; ask explicitly.
  OnToolbarAvailable(in);
  window_->Layout();
  Describe(phase);
}

void ToolbarProbe::Start(CefRefPtr<CefWindow> window, CefRefPtr<CefBrowserView> first, const std::string& second_url) {
  window_ = window; first_ = first;
  Describe("initial");
  if (manual_) return;  // A person clicks toolbar buttons and closes the window.
  CefPostDelayedTask(TID_UI, base::BindOnce([](ToolbarProbe* self, std::string url) {
    self->second_ = self->CreateView(url);
    self->Swap(self->first_, self->second_, "swapped_to_second");
  }, this, second_url), 9000);
  CefPostDelayedTask(TID_UI, base::BindOnce([](ToolbarProbe* self) {
    self->Swap(self->second_, self->first_, "swapped_back_to_first");
  }, this), 16000);
  CefPostDelayedTask(TID_UI, base::BindOnce([](ToolbarProbe* self) {
    self->Describe("final");
    self->finished_ = true;
    if (auto state = self->state_.lock(); state && state->window) state->window->Close();
  }, this), 24000);
}
}  // namespace red
