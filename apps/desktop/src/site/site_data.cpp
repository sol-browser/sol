#include "site/site_data.h"
#include "include/base/cef_callback.h"
#include "include/cef_cookie.h"
#include "include/cef_parser.h"
#include "include/wrapper/cef_closure_task.h"
#include "site/site_settings.h"
#include "windows/embedded_content.h"

namespace red {
namespace {
std::string Host(CefRefPtr<CefBrowser> browser) {
  CefURLParts parts;
  if (!browser || SiteOrigin(browser).empty() || !CefParseURL(browser->GetMainFrame()->GetURL(), parts)) return "";
  return CefString(&parts.host).ToString();
}
void Done(const std::shared_ptr<State>& state, int browser_id, const std::wstring& title) {
  // Only if the same page is still visible.
  if (!state->content || state->content->GetIdentifier() != browser_id) return;
  state->content->ReloadIgnoreCache();
  if (state->embedded) state->embedded->Notify("clear", title);
}
// Deletes while visiting; reports once Chromium released the visitor.
class SiteCookies final : public CefCookieVisitor {
 public:
  SiteCookies(std::weak_ptr<State> state, int browser_id, std::string host)
      : state_(std::move(state)), browser_id_(browser_id), host_(std::move(host)) {}
  ~SiteCookies() override {
    CefPostTask(TID_UI, base::BindOnce([](std::weak_ptr<State> weak, int id, std::wstring host, int count) {
      auto state = weak.lock(); if (!state) return;
      auto data = CefDictionaryValue::Create(); data->SetInt("deleted", count); state->Record("site_cookies_cleared", data);
      Done(state, id, L"Cookies cleared");
    }, state_, browser_id_, CefString(host_).ToWString(), deleted_));
  }
  bool Visit(const CefCookie& cookie, int, int, bool& remove) override {
    if (CookieDomainMatches(CefString(&cookie.domain).ToString(), host_)) { remove = true; ++deleted_; }
    return true;
  }
 private:
  std::weak_ptr<State> state_;
  int browser_id_;
  std::string host_;
  int deleted_ = 0;
  IMPLEMENT_REFCOUNTING(SiteCookies);
};
}  // namespace

void ClearSiteCookies(const std::shared_ptr<State>& state) {
  const auto host = Host(state->content);
  if (host.empty()) return;
  auto manager = state->content->GetHost()->GetRequestContext()->GetCookieManager(nullptr);
  if (!manager || !manager->VisitAllCookies(new SiteCookies(state, state->content->GetIdentifier(), host)))
    if (state->embedded) state->embedded->Notify("clear", L"Could not clear cookies");
}
void ClearSiteCache(const std::shared_ptr<State>& state) {
  auto origin = SiteOrigin(state->content);
  if (origin.empty()) return;
  origin.pop_back();  // DevTools expects the origin without the trailing slash.
  auto params = CefDictionaryValue::Create();
  params->SetString("origin", origin);
  params->SetString("storageTypes", "cache_storage,service_workers,shader_cache");
  state->content->GetHost()->ExecuteDevToolsMethod(0, "Storage.clearDataForOrigin", params);
  state->Record("site_cache_cleared");
  Done(state, state->content->GetIdentifier(), L"Cache cleared");
}
}  // namespace red
