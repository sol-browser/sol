#include "windows/tab_thumbnails.h"
#include <algorithm>
#include "include/base/cef_callback.h"
#include "include/cef_devtools_message_observer.h"
#include "include/cef_parser.h"
#include "include/wrapper/cef_closure_task.h"
#include "windows/image_scale.h"

namespace red {
namespace {
constexpr size_t kMaxThumbnails = 24;
constexpr size_t kMaxReply = 24u * 1024 * 1024;  // The page-sized JPEG, as base64 in JSON.
constexpr unsigned kWidth = 440;                  // Pixels of the stored picture (a 220 DIP card at 2x).
}  // namespace

struct TabThumbnails::Store {
  std::map<std::string, std::string> images;
  std::list<std::string> order;  // Oldest first.
  void Put(const std::string& id, std::string image) {
    order.remove(id);
    order.push_back(id);
    images[id] = std::move(image);
    while (order.size() > kMaxThumbnails) { images.erase(order.front()); order.pop_front(); }
  }
};

namespace {
// One picture of what the page shows. Taken at the page's own size, never with a clip or scale: in the
// pinned Chromium those resize the page's render widget until the reply, and the page flashed shrunk into
// the window's top-left corner on every Ctrl+Tab (user report, 2026-11-12; docs/page-transitions.md).
// The reply is scaled down off the UI thread.
class Shot final : public CefDevToolsMessageObserver {
 public:
  Shot(std::weak_ptr<TabThumbnails::Store> store, CefRefPtr<CefBrowser> page, std::string id)
      : store_(std::move(store)), page_(page), id_(std::move(id)) {}
  void Start(CefRefPtr<CefRegistration> registration) {
    registration_ = registration;
    auto params = CefDictionaryValue::Create();
    params->SetString("format", "jpeg"); params->SetInt("quality", 80);
    params->SetBool("fromSurface", true); params->SetBool("captureBeyondViewport", false); params->SetBool("optimizeForSpeed", true);
    shot_id_ = page_->GetHost()->ExecuteDevToolsMethod(0, "Page.captureScreenshot", params);
  }
  void OnDevToolsMethodResult(CefRefPtr<CefBrowser>, int id, bool success, const void* data, size_t size) override {
    std::string json;
    if (success && size <= kMaxReply) json.assign(static_cast<const char*>(data), size);
    CefPostTask(TID_UI, base::BindOnce(&Shot::Result, CefRefPtr<Shot>(this), id, std::move(json)));
  }

 private:
  void Result(int id, std::string json) {
    if (done_ || id != shot_id_) return;
    done_ = true;
    registration_ = nullptr;
    if (json.empty()) return;
    CefPostTask(TID_FILE_USER_BLOCKING, base::BindOnce([](std::weak_ptr<TabThumbnails::Store> store, std::string id, std::string json) {
      const auto value = CefParseJSON(json, JSON_PARSER_RFC);
      if (!value || value->GetType() != VTYPE_DICTIONARY) return;
      const auto binary = CefBase64Decode(value->GetDictionary()->GetString("data"));
      if (!binary || !binary->GetSize()) return;
      std::string jpeg(binary->GetSize(), '\0');
      binary->GetData(jpeg.data(), jpeg.size(), 0);
      const auto small = ScaledJpeg(jpeg, kWidth, .78f);
      if (!small) return;
      CefPostTask(TID_UI, base::BindOnce([](std::weak_ptr<TabThumbnails::Store> store, std::string id, std::string jpeg) {
        if (const auto alive = store.lock()) alive->Put(id, std::move(jpeg));
      }, store, std::move(id), std::move(*small)));
    }, store_, id_, std::move(json)));
  }
  std::weak_ptr<TabThumbnails::Store> store_;
  CefRefPtr<CefBrowser> page_;
  CefRefPtr<CefRegistration> registration_;
  std::string id_;
  int shot_id_ = -1;
  bool done_ = false;
  IMPLEMENT_REFCOUNTING(Shot);
};
}  // namespace

TabThumbnails::TabThumbnails() : store_(std::make_shared<Store>()) {}

void TabThumbnails::Capture(CefRefPtr<CefBrowser> page, const std::string& id) {
  if (!page || id.empty()) return;
  CefRefPtr<Shot> shot = new Shot(store_, page, id);
  shot->Start(page->GetHost()->AddDevToolsMessageObserver(shot));
}

std::string TabThumbnails::Get(const std::string& id) const {
  const auto found = store_->images.find(id);
  return found == store_->images.end() ? std::string() : found->second;
}

void TabThumbnails::Forget(const std::string& id) {
  store_->images.erase(id);
  store_->order.remove(id);
}
}  // namespace red
