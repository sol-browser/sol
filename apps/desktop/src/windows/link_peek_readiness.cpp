#include "windows/embedded_content.h"
#include "workspace/controller.h"
#include "include/cef_parser.h"
namespace red {
void EmbeddedContent::ObserveLinkPeekReadiness(CefRefPtr<CefBrowser> browser) {
  browser->GetHost()->ExecuteDevToolsMethod(0,"Page.enable",nullptr);
  auto params=CefDictionaryValue::Create(); params->SetBool("enabled",true);
  browser->GetHost()->ExecuteDevToolsMethod(0,"Page.setLifecycleEventsEnabled",params);
  link_peek_frame_query_=browser->GetHost()->ExecuteDevToolsMethod(0,"Page.getFrameTree",nullptr);
  // Read existing paint timing once in case FCP preceded observer setup. No
  // page mutation, injected handler, network call or navigation readiness loop.
  auto timing=CefDictionaryValue::Create(); timing->SetBool("returnByValue",true);
  timing->SetString("expression","performance.getEntriesByType('paint').some(p=>p.name==='first-contentful-paint')");
  link_peek_paint_query_=browser->GetHost()->ExecuteDevToolsMethod(0,"Runtime.evaluate",timing);
}
void EmbeddedContent::LinkPeekPageReady(CefRefPtr<CefBrowser> browser) {
  if (!browser->IsValid() || !IsLinkPeek(browser->GetIdentifier()) || link_peek_page_ready_ || link_peek_return_delay_) return;
  link_peek_page_ready_=true; LayoutLinkPeek();
  if (const auto state=state_.lock(); state && state->workspace) state->workspace->Refresh();
}
bool EmbeddedContent::OnLinkPeekReadiness(CefRefPtr<CefBrowser> browser,int message,bool success,const void* data,size_t size) {
  if (!IsLinkPeek(browser->GetIdentifier()) || !message || (message!=link_peek_frame_query_ && message!=link_peek_paint_query_)) return false;
  const bool frame=message==link_peek_frame_query_;
  if (frame) link_peek_frame_query_=0; else link_peek_paint_query_=0;
  if (!success || size>65536 || !browser->IsValid()) return true;
  const auto value=CefParseJSON(static_cast<const char*>(data),size,JSON_PARSER_RFC);
  if (!value || value->GetType()!=VTYPE_DICTIONARY) return true;
  if (frame) {
    const auto tree=value->GetDictionary()->GetDictionary("frameTree"); const auto main=tree ? tree->GetDictionary("frame") : nullptr;
    if (main) { link_peek_frame_=main->GetString("id"); link_peek_loader_=main->GetString("loaderId"); }
  } else {
    const auto result=value->GetDictionary()->GetDictionary("result");
    if (result && result->GetBool("value")) LinkPeekPageReady(browser);
  }
  return true;
}
void EmbeddedContent::OnLinkPeekEvent(CefRefPtr<CefBrowser> browser,const std::string& method,const std::string& json) {
  if (!IsLinkPeek(browser->GetIdentifier()) || !browser->IsValid() || link_peek_page_ready_ || link_peek_return_delay_) return;
  const auto value=CefParseJSON(json,JSON_PARSER_RFC); if (!value || value->GetType()!=VTYPE_DICTIONARY) return;
  const auto params=value->GetDictionary();
  if (method=="Page.frameNavigated") {
    const auto frame=params->GetDictionary("frame");
    if (frame && !frame->HasKey("parentId")) { link_peek_frame_=frame->GetString("id"); link_peek_loader_=frame->GetString("loaderId"); }
  } else if (!link_peek_frame_.empty() && params->GetString("frameId")==link_peek_frame_ && params->GetString("loaderId")==link_peek_loader_ &&
      params->GetString("name")=="firstContentfulPaint") LinkPeekPageReady(browser);
}
}  // namespace red
