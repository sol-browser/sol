#include "experiments/profiles_probe.h"
#include "windows/window_close.h"
#include "windows/embedded_content.h"
#include "include/cef_client.h"
#include "include/cef_request_context_handler.h"
#include "include/base/cef_callback.h"
#include "include/wrapper/cef_closure_task.h"

namespace red {
namespace {
struct Run {
  std::weak_ptr<State> state;
  std::vector<CefRefPtr<CefRequestContext>> contexts;
  int loaded = 0, closed = 0;
};
class Page final : public CefClient, public CefLifeSpanHandler, public CefLoadHandler {
 public:
  Page(std::shared_ptr<Run> run, int index) : run_(std::move(run)), index_(index) {}
  CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return this; }
  CefRefPtr<CefLoadHandler> GetLoadHandler() override { return this; }
  void OnAfterCreated(CefRefPtr<CefBrowser> browser) override {
    if (const auto state = run_->state.lock()) {
      state->browsers[browser->GetIdentifier()] = browser;
      auto data = CefDictionaryValue::Create();
      data->SetInt("index", index_);
      data->SetBool("same_window", GetParent(browser->GetHost()->GetWindowHandle()) == state->window->GetWindowHandle());
      data->SetBool("isolated", !browser->GetHost()->GetRequestContext()->IsSharingWith(CefRequestContext::GetGlobalContext()) &&
          !run_->contexts[0]->IsSharingWith(run_->contexts[1]));
      state->Record("profiles_probe_created", data);
    }
  }
  void OnLoadEnd(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, int status) override {
    if (!frame->IsMain() || loaded_) return;
    loaded_ = true;
    if (const auto state = run_->state.lock()) {
      auto data = CefDictionaryValue::Create(); data->SetInt("index", index_); data->SetInt("status", status);
      state->Record("profiles_probe_loaded", data);
    }
    ++run_->loaded;
    CefPostDelayedTask(TID_UI, base::BindOnce([](CefRefPtr<CefBrowser> page) { page->GetHost()->CloseBrowser(false); }, browser), 1200);
  }
  void OnBeforeClose(CefRefPtr<CefBrowser> browser) override {
    if (const auto state = run_->state.lock()) {
      state->browsers.erase(browser->GetIdentifier());
      if (state->window_close) state->window_close->OnClosed(browser->GetIdentifier());
      if (++run_->closed == 2) {
        auto data = CefDictionaryValue::Create(); data->SetInt("loaded", run_->loaded); data->SetInt("closed", run_->closed);
        state->Record("profiles_probe_finished", data);
        state->link_headers_done = true;
        if (state->embedded) state->embedded->QueueRaise();
      }
    }
  }
 private:
  std::shared_ptr<Run> run_;
  int index_;
  bool loaded_ = false;
  IMPLEMENT_REFCOUNTING(Page);
};
}
void StartProfilesProbe(const std::shared_ptr<State>& state, const std::string& url) {
  auto run = std::make_shared<Run>(); run->state = state;
  for (const char* name : {"Profile-Probe-A", "Profile-Probe-B"}) {
    CefRequestContextSettings settings;
    CefString(&settings.cache_path) = (state->profile / name).wstring();
    run->contexts.push_back(CefRequestContext::CreateContext(settings, nullptr));
  }
  const auto origin = url.substr(0, url.find('/', url.find("://") + 3));
  for (int i = 0; i < 2; ++i) {
    CefWindowInfo info;
    info.SetAsChild(state->window->GetWindowHandle(), CefRect(350 + i * 330, 90, 320, 420));
    info.runtime_style = CEF_RUNTIME_STYLE_CHROME;
    CefBrowserSettings settings;
    if (!CefBrowserHost::CreateBrowser(info, new Page(run, i), origin + "/profile?name=" + (i == 0 ? "A" : "B"),
        settings, nullptr, run->contexts[i])) state->Record("profiles_probe_create_failed");
  }
}
}
