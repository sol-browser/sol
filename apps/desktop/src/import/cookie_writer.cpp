#include "import/cookie_writer.h"
#include "include/cef_parser.h"
#include "include/cef_request_context.h"
#include "include/base/cef_callback.h"
#include "include/wrapper/cef_closure_task.h"
#include <set>
#include <tuple>

namespace red {
namespace {
using CookieKey = std::tuple<std::string, std::string, std::string>;
CookieKey Key(const CefCookie& c, const std::string& host = "") {
  return {host.empty() ? CefString(&c.domain).ToString() : host, CefString(&c.path).ToString(), CefString(&c.name).ToString()};
}
class Writer : public std::enable_shared_from_this<Writer> {
 public:
  Writer(std::vector<ZenCookie> cookies, std::function<void(CookieWriteResult)> done) : cookies_(std::move(cookies)), done_(std::move(done)) {}
  void Start();
  void Existing(std::set<CookieKey> keys, bool success) {
    if (!success) { result_.failed = static_cast<int>(cookies_.size()); Finish(); return; }
    existing_ = std::move(keys); Next();
  }
  void Next();
  void Finish() { cookies_.clear(); if (auto done = std::move(done_)) done(result_); }
  CookieWriteResult result_;
 private:
  std::vector<ZenCookie> cookies_;
  std::function<void(CookieWriteResult)> done_;
  CefRefPtr<CefCookieManager> manager_;
  std::set<CookieKey> existing_;
  size_t next_ = 0;
};
class ExistingCookies final : public CefCookieVisitor {
 public:
  explicit ExistingCookies(std::shared_ptr<Writer> job) : job_(std::move(job)) {}
  ~ExistingCookies() override {
    CefPostTask(TID_UI, base::BindOnce([](std::shared_ptr<Writer> job, std::set<CookieKey> keys, bool started) { job->Existing(std::move(keys), started); },
                                       std::move(job_), std::move(keys_), started));
  }
  bool Visit(const CefCookie& c, int, int, bool&) override { keys_.insert(Key(c)); return true; }
  bool started = false;
 private:
  std::shared_ptr<Writer> job_; std::set<CookieKey> keys_;
  IMPLEMENT_REFCOUNTING(ExistingCookies);
};
class CookieSet final : public CefSetCookieCallback {
 public:
  explicit CookieSet(std::function<void(bool)> done) : done_(std::move(done)) {}
  void OnComplete(bool success) override { CefPostTask(TID_UI, base::BindOnce([](std::function<void(bool)> done, bool success) { done(success); }, std::move(done_), success)); }
 private:
  std::function<void(bool)> done_; IMPLEMENT_REFCOUNTING(CookieSet);
};
class CookieFlush final : public CefCompletionCallback {
 public:
  explicit CookieFlush(std::function<void()> done) : done_(std::move(done)) {}
  void OnComplete() override { CefPostTask(TID_UI, base::BindOnce([](std::function<void()> done) { done(); }, std::move(done_))); }
 private:
  std::function<void()> done_; IMPLEMENT_REFCOUNTING(CookieFlush);
};
void Writer::Start() {
  manager_ = CefRequestContext::GetGlobalContext()->GetCookieManager(nullptr);
  if (!manager_) { result_.failed = static_cast<int>(cookies_.size()); Finish(); return; }
  CefRefPtr<ExistingCookies> visitor = new ExistingCookies(shared_from_this());
  visitor->started = manager_->VisitAllCookies(visitor);
}
void Writer::Next() {
  while (next_ < cookies_.size()) {
    auto& c = cookies_[next_++]; std::string host = CefString(&c.cookie.domain).ToString();
    if (host.empty()) { CefURLParts p; CefParseURL(c.url, p); host = CefString(&p.host).ToString(); }
    const auto key = Key(c.cookie, host);
    if (existing_.contains(key)) { ++result_.kept; continue; }
    existing_.insert(key);
    if (!manager_->SetCookie(c.url, c.cookie, new CookieSet([self = shared_from_this()](bool ok) { if (ok) ++self->result_.written; else ++self->result_.failed; self->Next(); }))) {
      ++result_.failed;
      CefPostTask(TID_UI, base::BindOnce([](std::shared_ptr<Writer> self) { self->Next(); }, shared_from_this()));
    }
    return;
  }
  if (!manager_->FlushStore(new CookieFlush([self = shared_from_this()] { self->result_.flushed = true; self->Finish(); }))) Finish();
}
}  // namespace

void WriteCookies(std::vector<ZenCookie> cookies, std::function<void(CookieWriteResult)> done) {
  if (cookies.empty()) { CookieWriteResult none; none.flushed = true; done(none); return; }
  std::make_shared<Writer>(std::move(cookies), std::move(done))->Start();
}
}  // namespace red
