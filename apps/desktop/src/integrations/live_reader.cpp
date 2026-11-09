#include "integrations/live_reader.h"
#include "integrations/live_script.h"
#include "include/cef_devtools_message_observer.h"
#include "include/cef_parser.h"
#include "include/base/cef_callback.h"
#include "include/wrapper/cef_closure_task.h"
#include <set>
namespace red {
namespace {
class ReadObserver final : public CefDevToolsMessageObserver {
 public:
  explicit ReadObserver(std::weak_ptr<LiveRead> owner) : owner_(std::move(owner)) {}
  void OnDevToolsMethodResult(CefRefPtr<CefBrowser> browser, int query, bool success, const void* bytes, size_t size) override {
    std::string json;
    if (success && size && size <= 32768) json.assign(static_cast<const char*>(bytes), size);
    CefPostTask(TID_UI, base::BindOnce([](std::weak_ptr<LiveRead> weak, int browser, int query, std::string json) {
      if (const auto self = weak.lock()) self->Result(browser, query, json);
    }, owner_, browser->GetIdentifier(), query, std::move(json)));
  }
 private:
  std::weak_ptr<LiveRead> owner_;
  IMPLEMENT_REFCOUNTING(ReadObserver);
};
}
std::shared_ptr<LiveRead> LiveRead::Start(CefRefPtr<CefBrowser> browser, const LiveProvider& provider,
    std::string source, Reply reply, bool outlook_shape) {
  return Evaluate(browser, provider, std::move(source), {}, {}, std::move(reply), outlook_shape);
}
void LiveRead::Click(CefRefPtr<CefBrowser> browser, const LiveProvider& provider, std::string source,
                     Action action, std::string token, std::function<void(bool)> reply) {
  const bool compose = action == Action::Compose;
  if (compose ? (!provider.IsMail() || !token.empty()) :
      (provider.id == "gmail" || !IntegrationRowToken(provider, token))) { reply(false); return; }
  auto operation = Evaluate(browser, provider, std::move(source), compose ? "compose" : "open", std::move(token),
    [reply=std::move(reply)](CefRefPtr<CefDictionaryValue> result) { reply(result && result->GetBool("clicked")); });
  CefPostDelayedTask(TID_UI, base::BindOnce([](std::shared_ptr<LiveRead>){}, std::move(operation)), 2700);
}
std::shared_ptr<LiveRead> LiveRead::Evaluate(CefRefPtr<CefBrowser> browser, const LiveProvider& provider,
    std::string source, std::string action, std::string token, Reply reply, bool outlook_shape) {
  auto self = std::make_shared<LiveRead>(); self->browser_ = browser; self->provider_ = provider;
  self->source_ = std::move(source); self->reply_ = std::move(reply);
  const auto allowed = LiveIntegration(self->source_);
  const auto frame = browser && browser->IsValid() ? browser->GetMainFrame() : nullptr;
  if (!allowed || allowed->id != provider.id || !frame || frame->GetURL() != self->source_) {
    CefPostTask(TID_UI, base::BindOnce([](std::shared_ptr<LiveRead> op) { op->Finish(); }, self));
    return self;
  }
  self->action_ = !action.empty();
  self->outlook_shape_ = outlook_shape && provider.id == "outlookMail" && !self->action_;
  self->observer_ = browser->GetHost()->AddDevToolsMessageObserver(new ReadObserver(self));
  auto expected = CefDictionaryValue::Create(); expected->SetString("source", self->source_); expected->SetString("service", provider.id);
  expected->SetString("action", action); expected->SetString("token", token);
  expected->SetBool("outlookShape", self->outlook_shape_);
  auto value = CefValue::Create(); value->SetDictionary(expected);
  auto params = CefDictionaryValue::Create(); params->SetBool("returnByValue", true); params->SetInt("timeout", 800);
  params->SetString("expression", "(()=>{const expected=" + CefWriteJSON(value, JSON_WRITER_DEFAULT).ToString() + ";" + std::string(kLiveReadScript) + "})()");
  self->query_ = browser->GetHost()->ExecuteDevToolsMethod(0, "Runtime.evaluate", params);
  CefPostDelayedTask(TID_UI, base::BindOnce([](std::weak_ptr<LiveRead> weak) { if (const auto self = weak.lock()) self->Finish(); }, std::weak_ptr<LiveRead>(self)), 2500);
  return self;
}
void LiveRead::Cancel() { done_ = true; reply_ = {}; observer_ = nullptr; browser_ = nullptr; }
void LiveRead::Finish(CefRefPtr<CefDictionaryValue> value) {
  if (done_) return;
  auto reply = std::move(reply_); Cancel(); if (reply) reply(value);
}
void LiveRead::Result(int browser, int query, const std::string& json) {
  if (done_ || !browser_ || browser_->GetIdentifier() != browser || query_ != query) return;
  const auto frame = browser_->IsValid() ? browser_->GetMainFrame() : nullptr;
  if (!frame || (!action_ && frame->GetURL() != source_) || json.empty()) { Finish(); return; }
  const auto parsed = CefParseJSON(json, JSON_PARSER_RFC);
  const auto outer = parsed && parsed->GetType() == VTYPE_DICTIONARY ? parsed->GetDictionary()->GetDictionary("result") : nullptr;
  if (action_) {
    auto ack = CefDictionaryValue::Create();
    ack->SetBool("clicked", outer && outer->GetType("value") == VTYPE_BOOL && outer->GetBool("value"));
    Finish(ack); return;
  }
  const auto value = outer ? outer->GetDictionary("value") : nullptr;
  if (!value || value->GetType("status") != VTYPE_STRING || value->GetType("items") != VTYPE_LIST || value->GetList("items")->GetSize() > 8) { Finish(); return; }
  const auto status = value->GetString("status");
  if (status != "ready" && status != "unsupported" && status != "stale") { Finish(); return; }
  auto items = CefListValue::Create();
  std::set<std::string> tokens;
  const auto input = value->GetList("items");
  for (size_t i = 0; i < input->GetSize(); ++i) {
    if (input->GetType(i) != VTYPE_DICTIONARY) { Finish(); return; }
    const auto item = input->GetDictionary(i);
    if (item->GetType("title") != VTYPE_STRING || item->GetString("title").empty() || item->GetString("title").length() > 400 ||
        item->GetType("token") != VTYPE_STRING) { Finish(); return; }
    const auto token = item->GetString("token").ToString();
    if (!IntegrationRowToken(provider_, token)) { Finish(); return; }
    if (!tokens.insert(token).second) { Finish(); return; }
    auto row = CefDictionaryValue::Create(); row->SetString("title", item->GetString("title")); row->SetString("token", token);
    if (item->HasKey("detail")) {
      if (item->GetType("detail") != VTYPE_STRING || item->GetString("detail").length() > 400) { Finish(); return; }
      row->SetString("detail", item->GetString("detail"));
    }
    if (item->HasKey("displayTime")) {
      if (!provider_.IsMail() || item->GetType("displayTime") != VTYPE_STRING ||
          item->GetString("displayTime").empty() || item->GetString("displayTime").length() > 24) { Finish(); return; }
      row->SetString("displayTime", item->GetString("displayTime"));
    }
    if (item->HasKey("startMinute") || item->HasKey("endMinute")) {
      const int start = item->GetInt("startMinute"), end = item->GetInt("endMinute");
      if (provider_.id != "googleCalendar" || !IsCalendarDayView(source_) || item->GetType("startMinute") != VTYPE_INT ||
          start < 0 || start >= 1440 || (item->HasKey("endMinute") &&
          (item->GetType("endMinute") != VTYPE_INT || end <= start || end >= 1440))) { Finish(); return; }
      row->SetInt("startMinute", start);
      if (item->HasKey("endMinute")) row->SetInt("endMinute", end);
    }
    if (item->HasKey("join")) {
      if (provider_.id != "googleCalendar" || item->GetType("join") != VTYPE_STRING || !IntegrationJoinUrl(item->GetString("join").ToString())) { Finish(); return; }
      row->SetString("join", item->GetString("join"));
    }
    items->SetDictionary(items->GetSize(), row);
  }
  auto result = CefDictionaryValue::Create(); result->SetString("status", status); result->SetList("items", items);
  result->SetBool("composeAvailable", provider_.IsMail() && value->GetType("composeAvailable") == VTYPE_BOOL && value->GetBool("composeAvailable"));
  if (provider_.IsMail() && status == "ready") {
    const auto view = value->GetString("mailView");
    const auto target = MailboxTarget(source_).value_or("");
    if (view != "recent" && view != "unread" &&
        !((view == "unreadAll" || view == "recentAll") && IsGmailInboxSearch(target,source_,view=="unreadAll"))) {
      Finish(); return;
    }
    result->SetString("mailView", view);
  }
  if (outlook_shape_ && value->GetType("outlookShape") == VTYPE_LIST) {
    const auto shape = value->GetList("outlookShape");
    auto checked = CefListValue::Create();
    if (shape->GetSize() <= 36) for (size_t i=0; i<shape->GetSize(); ++i) {
      if (shape->GetType(i) != VTYPE_LIST) continue;
      const auto fields = shape->GetList(i);
      if (fields->GetSize() != 12) continue;
      bool valid = true;
      for (size_t field=0; field<fields->GetSize(); ++field) {
        if (fields->GetType(field) != VTYPE_INT ||
            fields->GetInt(field)<-1 || fields->GetInt(field)>1073741823) valid=false;
      }
      if (valid) checked->SetList(checked->GetSize(),fields->Copy());
    }
    result->SetList("outlookShape",checked);
  }
  Finish(result);
}
}  // namespace red
