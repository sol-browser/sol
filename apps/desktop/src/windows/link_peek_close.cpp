#include "windows/embedded_content.h"
#include "workspace/controller.h"
#include "include/cef_parser.h"
#include "include/base/cef_callback.h"
#include "include/wrapper/cef_closure_task.h"
#include <vector>
namespace red {
void EmbeddedContent::CaptureLinkPeekSources() {
  link_peek_source_captures_.clear();
  for (const auto& [id,browser]:browsers_) if (id!=link_peek_id_ && (id==link_peek_base_ || IsSplitPage(id)) && IsWindowVisible(browser->GetHost()->GetWindowHandle())) {
    auto params=CefDictionaryValue::Create(); params->SetString("format","png"); params->SetBool("captureBeyondViewport",false); params->SetBool("optimizeForSpeed",true);
    const int message=browser->GetHost()->ExecuteDevToolsMethod(0,"Page.captureScreenshot",params);
    if (message) link_peek_source_captures_[id]=message;
  }
  link_peek_sources_requested_=GetTickCount64(); link_peek_source_frames_.clear();
  link_peek_waiting_=!link_peek_source_captures_.empty();
  const auto generation=link_peek_generation_;
  CefPostDelayedTask(TID_UI,base::BindOnce([](std::weak_ptr<State> weak,unsigned long long generation) {
    if (const auto state=weak.lock(); state && state->embedded && state->embedded->link_peek_generation_==generation) {
      if (const auto missed=state->embedded->link_peek_source_captures_.size()) {
        auto data=CefDictionaryValue::Create(); data->SetInt("missed",static_cast<int>(missed)); state->Record("link_peek_source_capture_timeout",data);
      }
      state->embedded->link_peek_source_captures_.clear();
      if (state->embedded->link_peek_waiting_) state->embedded->StartLinkPeekScene();
    }
  },state_,generation),400);
}
void EmbeddedContent::DiscardLinkPeekClose() {
  ++link_peek_capture_generation_; link_peek_close_capture_=0; link_peek_capture_ready_={}; corners_.DiscardLinkPeekFrame();
}
void EmbeddedContent::PrepareLinkPeekClose(std::function<void()> ready) {
  DiscardLinkPeekClose();
  const auto browser=browsers_.find(link_peek_id_);
  // Not revealed yet: the neutral card returns, no page frame is needed.
  if (!LinkPeekComposed() || !corners_.LinkPeekPreviewVisible() || browser==browsers_.end()) { ready(); return; }
  link_peek_capture_ready_=std::move(ready);
  auto params=CefDictionaryValue::Create(); params->SetString("format","png"); params->SetBool("captureBeyondViewport",false); params->SetBool("optimizeForSpeed",true);
  link_peek_close_capture_=browser->second->GetHost()->ExecuteDevToolsMethod(0,"Page.captureScreenshot",params);
  link_peek_close_requested_=GetTickCount64();
  if (!link_peek_close_capture_) { auto done=std::move(link_peek_capture_ready_); done(); return; }
  const auto generation=link_peek_capture_generation_;
  CefPostDelayedTask(TID_UI,base::BindOnce([](std::weak_ptr<State> weak,unsigned long long generation) {
    if (const auto state=weak.lock(); state && state->embedded && state->embedded->link_peek_capture_generation_==generation) {
      auto& self=*state->embedded; self.link_peek_close_capture_=0; state->Record("link_peek_close_capture_timeout");
      auto ready=std::move(self.link_peek_capture_ready_); if (ready) ready();
    }
  },state_,generation),200);
}
bool EmbeddedContent::OnLinkPeekCapture(CefRefPtr<CefBrowser> browser,int message,bool success,const void* data,size_t size) {
  if (const auto pending=link_peek_source_captures_.find(browser->GetIdentifier()); pending!=link_peek_source_captures_.end() && pending->second==message) {
    link_peek_source_captures_.erase(pending);
    if (const auto state=state_.lock()) {
      auto timing=CefDictionaryValue::Create(); timing->SetInt("ms",static_cast<int>(GetTickCount64()-link_peek_sources_requested_));
      timing->SetInt("bytes",static_cast<int>(size)); state->Record("link_peek_source_capture",timing);
    }
    if (success && size<=24*1024*1024 && browser->IsValid() && HasLinkPeek()) {
      const auto parsed=CefParseJSON(static_cast<const char*>(data),size,JSON_PARSER_RFC);
      if (parsed && parsed->GetType()==VTYPE_DICTIONARY) if (const auto png=CefBase64Decode(parsed->GetDictionary()->GetString("data")); png && png->GetSize() && png->GetSize()<=16*1024*1024) {
        std::vector<char> bytes(png->GetSize()); png->GetData(bytes.data(),bytes.size(),0);
        if (link_peek_waiting_) link_peek_source_frames_.emplace_back(browser->GetHost()->GetWindowHandle(),std::move(bytes));
        else {  // Late (after the budget): the scene is already running.
          corners_.HoldLinkPeekSource(browser->GetHost()->GetWindowHandle(),bytes.data(),bytes.size()); LayoutLinkPeek();
          if (const auto state=state_.lock(); state && state->workspace) state->workspace->Refresh();
          // Masks the live source pages once the picture is on screen.
          CefPostDelayedTask(TID_UI,base::BindOnce([](std::weak_ptr<State> weak,unsigned long long generation) {
            if (const auto state=weak.lock(); state && state->embedded && state->embedded->HasLinkPeek() && state->embedded->link_peek_generation_==generation) {
              state->embedded->LayoutLinkPeek(); if (state->workspace) state->workspace->Refresh();
            }
          },state_,link_peek_generation_),static_cast<int64_t>(LinkPeekScene::kMaskDelayMs+10));
        }
      }
    }
    if (link_peek_waiting_ && link_peek_source_captures_.empty()) StartLinkPeekScene();
    return true;
  }
  if (!message || message!=link_peek_close_capture_ || !IsLinkPeek(browser->GetIdentifier())) return false;
  link_peek_close_capture_=0; ++link_peek_capture_generation_;
  if (const auto state=state_.lock()) {
    auto timing=CefDictionaryValue::Create(); timing->SetInt("ms",static_cast<int>(GetTickCount64()-link_peek_close_requested_));
    timing->SetInt("bytes",static_cast<int>(size)); state->Record("link_peek_close_capture",timing);
  }
  if (success && size<=24*1024*1024) {
    const auto parsed=CefParseJSON(static_cast<const char*>(data),size,JSON_PARSER_RFC);
    if (parsed && parsed->GetType()==VTYPE_DICTIONARY) if (const auto png=CefBase64Decode(parsed->GetDictionary()->GetString("data")); png && png->GetSize() && png->GetSize()<=16*1024*1024) {
      std::vector<char> bytes(png->GetSize()); png->GetData(bytes.data(),bytes.size(),0);
      const auto card=LinkPeekCard(); corners_.HoldLinkPeekFrame(bytes.data(),bytes.size(),card.width,card.height);
    }
  }
  auto ready=std::move(link_peek_capture_ready_); if (ready) ready(); return true;
}
}  // namespace red
