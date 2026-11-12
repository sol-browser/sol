#include "windows/capture_window.h"
#include <dwmapi.h>
#include <algorithm>
#include <cmath>
#include <map>
#include "app/session.h"
#include "include/base/cef_callback.h"
#include "include/cef_devtools_message_observer.h"
#include "include/cef_parser.h"
#include "include/views/cef_fill_layout.h"
#include "include/wrapper/cef_closure_task.h"
#include "trust_policy.h"
#include "windows/branding.h"
#include "windows/embedded_content.h"
#include "windows/focus.h"
#include "windows/image_scale.h"
#include "windows/page_capture.h"
#include "windows/ui_context_menu.h"
#include "windows/window_close.h"
#include "workspace/controller.h"

namespace red {
namespace {
constexpr size_t kMaxReply = 96u * 1024 * 1024;  // DOMSnapshot of a large page, or a page-sized PNG, as JSON.
constexpr size_t kMaxBoxes = 6000;
const cef_color_t kBackground = CefColorSetARGB(255, 22, 19, 27);

// The picture each capture window's document may load, by its browser.
struct Frames { std::mutex mutex; std::map<int, std::pair<unsigned, std::shared_ptr<const CaptureWindow::Shot>>> pictures; };
Frames& FrameStore() { static auto* frames = new Frames; return *frames; }
void PublishFrame(int browser, unsigned generation, std::shared_ptr<const CaptureWindow::Shot> shot) {
  auto& store = FrameStore(); std::lock_guard lock(store.mutex);
  if (shot) store.pictures[browser] = {generation, std::move(shot)}; else store.pictures.erase(browser);
}

double Number(CefRefPtr<CefDictionaryValue> dictionary, const char* key) {
  if (!dictionary) return std::nan("");
  const auto type = dictionary->GetType(key);
  return type == VTYPE_INT ? dictionary->GetInt(key) : type == VTYPE_DOUBLE ? dictionary->GetDouble(key) : std::nan("");
}
double Number(CefRefPtr<CefListValue> list, size_t index) {
  const auto type = list->GetType(index);
  return type == VTYPE_INT ? list->GetInt(index) : type == VTYPE_DOUBLE ? list->GetDouble(index) : std::nan("");
}
// Width of a PNG from its header (IHDR), or 0.
int PngWidth(const std::string& png) {
  if (png.size() < 24 || png.compare(1, 3, "PNG") != 0) return 0;
  const auto* b = reinterpret_cast<const unsigned char*>(png.data()) + 16;
  return (b[0] << 24) | (b[1] << 16) | (b[2] << 8) | b[3];
}

// The element boxes of the page's main document from DOMSnapshot.captureSnapshot (document coordinates,
// CSS pixels): elements only, not the root and body, not tiny ones. Read once, nothing runs in the page.
void ReadBoxes(CefRefPtr<CefDictionaryValue> snapshot, CaptureWindow::Shot& shot) {
  const auto documents = snapshot ? snapshot->GetList("documents") : nullptr;
  const auto strings = snapshot ? snapshot->GetList("strings") : nullptr;
  const auto document = documents && documents->GetSize() ? documents->GetDictionary(0) : nullptr;
  const auto nodes = document ? document->GetDictionary("nodes") : nullptr;
  const auto layout = document ? document->GetDictionary("layout") : nullptr;
  if (!nodes || !layout || !strings) return;
  const auto types = nodes->GetList("nodeType"), names = nodes->GetList("nodeName");
  const auto indices = layout->GetList("nodeIndex"), bounds = layout->GetList("bounds");
  if (!types || !names || !indices || !bounds) return;
  for (size_t i = 0; i < indices->GetSize() && i < bounds->GetSize() && shot.boxes.size() < kMaxBoxes; ++i) {
    const int node = indices->GetInt(i);
    if (node < 0 || static_cast<size_t>(node) >= types->GetSize() || types->GetInt(node) != 1) continue;
    const int name = static_cast<size_t>(node) < names->GetSize() ? names->GetInt(node) : -1;
    if (name >= 0 && static_cast<size_t>(name) < strings->GetSize()) {
      const auto tag = strings->GetString(name).ToString();
      if (tag == "HTML" || tag == "BODY" || tag == "HEAD") continue;
    }
    const auto box = bounds->GetList(i);
    if (!box || box->GetSize() < 4) continue;
    const double x = Number(box, 0), y = Number(box, 1), w = Number(box, 2), h = Number(box, 3);
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(w) || !std::isfinite(h) || w < 8 || h < 8) continue;
    if (x + w < 0 || y + h < 0 || x > shot.page_width + 1 || y > shot.page_height + 1) continue;
    shot.boxes.push_back({static_cast<int>(std::lround(x)), static_cast<int>(std::lround(y)), static_cast<int>(std::lround(w)), static_cast<int>(std::lround(h))});
  }
}

// The three readings of one still picture: the viewport, the picture, the element boxes.
class Snapper final : public CefDevToolsMessageObserver {
 public:
  Snapper(std::weak_ptr<CaptureWindow> owner, unsigned request) : owner_(std::move(owner)), request_(request) {}
  void Start(CefRefPtr<CefBrowser> page) {
    registration_ = page->GetHost()->AddDevToolsMessageObserver(this);
    metrics_id_ = page->GetHost()->ExecuteDevToolsMethod(0, "Page.getLayoutMetrics", nullptr);
    // A surface copy at the page's own size: no clip, so nothing on screen changes.
    auto shot = CefDictionaryValue::Create();
    shot->SetString("format", "png"); shot->SetBool("fromSurface", true); shot->SetBool("captureBeyondViewport", false);
    shot_id_ = page->GetHost()->ExecuteDevToolsMethod(0, "Page.captureScreenshot", shot);
    auto snapshot = CefDictionaryValue::Create();
    snapshot->SetList("computedStyles", CefListValue::Create());
    snapshot_id_ = page->GetHost()->ExecuteDevToolsMethod(0, "DOMSnapshot.captureSnapshot", snapshot);
    CefPostDelayedTask(TID_UI, base::BindOnce(&Snapper::TimedOut, CefRefPtr<Snapper>(this)), 8000);
  }
  void OnDevToolsMethodResult(CefRefPtr<CefBrowser>, int id, bool success, const void* data, size_t size) override {
    std::string json;
    if (success && size <= kMaxReply) json.assign(static_cast<const char*>(data), size);
    CefPostTask(TID_UI, base::BindOnce(&Snapper::Result, CefRefPtr<Snapper>(this), id, std::move(json)));
  }

 private:
  void Result(int id, std::string json) {
    if (done_) return;
    if (id == metrics_id_) metrics_ = std::move(json), got_ |= 1;
    else if (id == shot_id_) picture_ = std::move(json), got_ |= 2;
    else if (id == snapshot_id_) boxes_ = std::move(json), got_ |= 4;
    else return;
    if (got_ != 7) return;
    done_ = true;
    registration_ = nullptr;
    // Large pages give large replies: read them off the UI thread.
    CefPostTask(TID_FILE_USER_BLOCKING, base::BindOnce([](std::weak_ptr<CaptureWindow> owner, unsigned request, std::string metrics,
                                                          std::string picture, std::string boxes) {
      auto shot = std::make_shared<CaptureWindow::Shot>();
      const auto m = metrics.empty() ? nullptr : CefParseJSON(metrics, JSON_PARSER_RFC);
      const auto p = picture.empty() ? nullptr : CefParseJSON(picture, JSON_PARSER_RFC);
      bool ok = m && m->GetType() == VTYPE_DICTIONARY && p && p->GetType() == VTYPE_DICTIONARY;
      if (ok) {
        const auto view = m->GetDictionary()->GetDictionary("cssVisualViewport"), size = m->GetDictionary()->GetDictionary("cssContentSize");
        shot->view_x = Number(view, "pageX"); shot->view_y = Number(view, "pageY");
        shot->view_width = Number(view, "clientWidth"); shot->view_height = Number(view, "clientHeight");
        shot->page_width = Number(size, "width"); shot->page_height = Number(size, "height");
        const auto binary = CefBase64Decode(p->GetDictionary()->GetString("data"));
        ok = binary && binary->GetSize() && std::isfinite(shot->view_x) && std::isfinite(shot->view_y) && shot->view_width >= 50 &&
             shot->view_height >= 50 && std::isfinite(shot->page_width) && std::isfinite(shot->page_height);
        if (ok) { shot->png.resize(binary->GetSize()); binary->GetData(shot->png.data(), shot->png.size(), 0); shot->view_png = shot->png; }
        shot->pic_x = shot->view_x; shot->pic_y = shot->view_y; shot->pic_width = shot->view_width; shot->pic_height = shot->view_height;
      }
      if (ok) {
        const auto b = boxes.empty() ? nullptr : CefParseJSON(boxes, JSON_PARSER_RFC);
        if (b && b->GetType() == VTYPE_DICTIONARY) ReadBoxes(b->GetDictionary(), *shot);
      }
      CefPostTask(TID_UI, base::BindOnce([](std::weak_ptr<CaptureWindow> owner, unsigned request, std::shared_ptr<CaptureWindow::Shot> shot) {
        if (const auto self = owner.lock()) self->Snapped(request, shot);
      }, owner, request, ok ? shot : nullptr));
    }, owner_, request_, std::move(metrics_), std::move(picture_), std::move(boxes_)));
  }
  void TimedOut() {
    if (done_) return;
    done_ = true; registration_ = nullptr;
    if (const auto owner = owner_.lock()) owner->Snapped(request_, nullptr);
  }
  std::weak_ptr<CaptureWindow> owner_;
  unsigned request_;
  CefRefPtr<CefRegistration> registration_;
  std::string metrics_, picture_, boxes_;
  int metrics_id_ = -1, shot_id_ = -1, snapshot_id_ = -1, got_ = 0;
  bool done_ = false;
  IMPLEMENT_REFCOUNTING(Snapper);
};

// The whole page as one picture: Chrome's capture with a clip beyond the viewport. It resizes the page for a
// moment, which is why it runs only while the capture window covers the page. Fixed elements (headers, chat
// buttons) are drawn where the page is scrolled to, so the page is put at its top for the picture and back
// where it was afterwards, from an isolated world: the website's own scripts cannot change how that is done.
class FullSnapper final : public CefDevToolsMessageObserver {
 public:
  FullSnapper(std::weak_ptr<CaptureWindow> owner, unsigned request, double width, double height)
      : owner_(std::move(owner)), request_(request), width_(width), height_(height) {}
  void Start(CefRefPtr<CefBrowser> page, double scale) {
    page_ = page; scale_ = scale;
    registration_ = page->GetHost()->AddDevToolsMessageObserver(this);
    tree_id_ = page->GetHost()->ExecuteDevToolsMethod(0, "Page.getFrameTree", nullptr);
    CefPostDelayedTask(TID_UI, base::BindOnce(&FullSnapper::Result, CefRefPtr<FullSnapper>(this), -1, std::string()), 10000);
  }
  void OnDevToolsMethodResult(CefRefPtr<CefBrowser>, int id, bool success, const void* data, size_t size) override {
    std::string json;
    if (success && size <= kMaxReply) json.assign(static_cast<const char*>(data), size);
    CefPostTask(TID_UI, base::BindOnce(&FullSnapper::Result, CefRefPtr<FullSnapper>(this), id, std::move(json)));
  }

 private:
  static CefRefPtr<CefDictionaryValue> Parse(const std::string& json) {
    const auto value = json.empty() ? nullptr : CefParseJSON(json, JSON_PARSER_RFC);
    return value && value->GetType() == VTYPE_DICTIONARY ? value->GetDictionary() : nullptr;
  }
  int Evaluate(const std::string& expression) {
    auto params = CefDictionaryValue::Create();
    params->SetString("expression", expression); params->SetInt("contextId", context_); params->SetBool("returnByValue", true);
    return page_->GetHost()->ExecuteDevToolsMethod(0, "Runtime.evaluate", params);
  }
  void Shoot() {
    auto clip = CefDictionaryValue::Create();
    clip->SetDouble("x", 0); clip->SetDouble("y", 0); clip->SetDouble("width", width_); clip->SetDouble("height", height_);
    clip->SetDouble("scale", scale_);
    auto params = CefDictionaryValue::Create();
    params->SetString("format", "png"); params->SetDictionary("clip", clip); params->SetBool("captureBeyondViewport", true);
    shot_id_ = page_->GetHost()->ExecuteDevToolsMethod(0, "Page.captureScreenshot", params);
  }
  void Result(int id, std::string json) {
    if (done_) return;
    if (id == -1) {  // The time is up: put the page back if it was moved, give up.
      if (moved_ && !restore_id_) Evaluate(Restore());
      Finish(std::string());
      return;
    }
    if (id == tree_id_) {
      const auto reply = Parse(json);
      const auto tree = reply ? reply->GetDictionary("frameTree") : nullptr;
      const auto frame = tree ? tree->GetDictionary("frame") : nullptr;
      if (!frame || frame->GetType("id") != VTYPE_STRING) { Shoot(); return; }
      auto params = CefDictionaryValue::Create();
      params->SetString("frameId", frame->GetString("id")); params->SetString("worldName", "sol-capture");
      world_id_ = page_->GetHost()->ExecuteDevToolsMethod(0, "Page.createIsolatedWorld", params);
    } else if (id == world_id_) {
      const auto reply = Parse(json);
      if (!reply || reply->GetType("executionContextId") != VTYPE_INT) { Shoot(); return; }
      context_ = reply->GetInt("executionContextId");
      top_id_ = Evaluate("(() => { const x = scrollX, y = scrollY; scrollTo({ left: 0, top: 0, behavior: 'instant' }); return [x, y]; })()");
    } else if (id == top_id_) {
      const auto reply = Parse(json);
      const auto result = reply ? reply->GetDictionary("result") : nullptr;
      const auto value = result && result->GetType("value") == VTYPE_LIST ? result->GetList("value") : nullptr;
      if (value && value->GetSize() == 2) {
        const auto number = [&value](size_t i) {
          return value->GetType(i) == VTYPE_INT ? value->GetInt(i) : value->GetType(i) == VTYPE_DOUBLE ? value->GetDouble(i) : 0.0;
        };
        x_ = number(0); y_ = number(1); moved_ = true;
      }
      Shoot();
    } else if (id == shot_id_) {
      picture_ = std::move(json);
      if (moved_) { restore_id_ = Evaluate(Restore()); return; }
      Finish(std::move(picture_));
    } else if (id == restore_id_) {
      Finish(std::move(picture_));
    }
  }
  std::string Restore() const {
    return "scrollTo({ left: " + std::to_string(x_) + ", top: " + std::to_string(y_) + ", behavior: 'instant' })";
  }
  void Finish(std::string json) {
    done_ = true; registration_ = nullptr; page_ = nullptr;
    CefPostTask(TID_FILE_USER_BLOCKING, base::BindOnce([](std::weak_ptr<CaptureWindow> owner, unsigned request, std::string json,
                                                          double width, double height) {
      std::optional<std::string> png;
      const auto value = json.empty() ? nullptr : CefParseJSON(json, JSON_PARSER_RFC);
      if (value && value->GetType() == VTYPE_DICTIONARY) {
        const auto binary = CefBase64Decode(value->GetDictionary()->GetString("data"));
        if (binary && binary->GetSize()) { png.emplace(binary->GetSize(), '\0'); binary->GetData(png->data(), png->size(), 0); }
      }
      CefPostTask(TID_UI, base::BindOnce([](std::weak_ptr<CaptureWindow> owner, unsigned request, std::optional<std::string> png,
                                            double width, double height) {
        if (const auto self = owner.lock()) self->FullSnapped(request, std::move(png), width, height);
      }, owner, request, std::move(png), width, height));
    }, owner_, request_, std::move(json), width_, height_));
  }
  std::weak_ptr<CaptureWindow> owner_;
  unsigned request_;
  double width_, height_, scale_ = 1, x_ = 0, y_ = 0;
  CefRefPtr<CefBrowser> page_;
  CefRefPtr<CefRegistration> registration_;
  std::string picture_;
  int tree_id_ = 0, world_id_ = 0, top_id_ = 0, shot_id_ = 0, restore_id_ = 0, context_ = 0;
  bool done_ = false, moved_ = false;
  IMPLEMENT_REFCOUNTING(FullSnapper);
};

// Cuts |left, top, width, height| (fractions of the picture) off the UI thread, delivers it, closes the window.
void CropThenDone(std::weak_ptr<CaptureWindow> weak, std::weak_ptr<State> state, std::string png, double left, double top,
                  double width, double height, ScreenshotAction action, bool region) {
  CefPostTask(TID_FILE_USER_BLOCKING, base::BindOnce([](std::weak_ptr<CaptureWindow> weak, std::weak_ptr<State> state, std::string png,
                                                        double left, double top, double width, double height, ScreenshotAction action, bool region) {
    auto cropped = CroppedPng(png, left, top, width, height);
    CefPostTask(TID_UI, base::BindOnce([](std::weak_ptr<CaptureWindow> weak, std::weak_ptr<State> state, std::optional<std::string> cropped,
                                          ScreenshotAction action, bool region) {
      const auto owner = state.lock();
      if (owner && cropped) DeliverScreenshot(owner, std::move(*cropped), action, region, false);
      else if (owner && owner->workspace) owner->workspace->Notify("Could not take the screenshot.");
      if (const auto self = weak.lock()) self->Done();
    }, weak, state, std::move(cropped), action, region));
  }, std::move(weak), std::move(state), std::move(png), left, top, width, height, action, region));
}

class CaptureClient final : public CefClient, public CefLifeSpanHandler, public CefRequestHandler, public CefMessageRouterBrowserSide::Handler {
 public:
  explicit CaptureClient(std::weak_ptr<CaptureWindow> owner) : owner_(owner) {
    router_ = CefMessageRouterBrowserSide::Create(CefMessageRouterConfig()); router_->AddHandler(this, false);
  }
  ~CaptureClient() override { router_->RemoveHandler(this); }
  CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return this; }
  CefRefPtr<CefContextMenuHandler> GetContextMenuHandler() override { return new UiContextMenu(std::weak_ptr<State>()); }
  CefRefPtr<CefRequestHandler> GetRequestHandler() override { return this; }
  void OnAfterCreated(CefRefPtr<CefBrowser> browser) override { if (auto owner = owner_.lock()) owner->BrowserCreated(browser); }
  void OnBeforeClose(CefRefPtr<CefBrowser> browser) override {
    router_->OnBeforeClose(browser); if (auto owner = owner_.lock()) owner->BrowserClosed(browser->GetIdentifier());
  }
  bool OnBeforeBrowse(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, CefRefPtr<CefRequest> request, bool, bool) override {
    router_->OnBeforeBrowse(browser, frame); return !frame->IsMain() || request->GetURL().ToString() != kCaptureUrl;
  }
  bool OnBeforePopup(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, int, const CefString&, const CefString&,
      cef_window_open_disposition_t, bool, const CefPopupFeatures&, CefWindowInfo&, CefRefPtr<CefClient>&,
      CefBrowserSettings&, CefRefPtr<CefDictionaryValue>&, bool*) override { return true; }
  void OnRenderProcessTerminated(CefRefPtr<CefBrowser> browser, TerminationStatus, int, const CefString&) override {
    router_->OnRenderProcessTerminated(browser); if (auto owner = owner_.lock()) owner->Close();
  }
  bool OnProcessMessageReceived(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, CefProcessId source,
      CefRefPtr<CefProcessMessage> message) override { return router_->OnProcessMessageReceived(browser, frame, source, message); }
  bool OnQuery(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, int64_t, const CefString& request,
      bool persistent, CefRefPtr<Callback> callback) override {
    if (auto owner = owner_.lock()) return owner->Query(browser, frame, request, persistent, callback);
    callback->Failure(410, "Capture closed"); return true;
  }
 private:
  std::weak_ptr<CaptureWindow> owner_;
  CefRefPtr<CefMessageRouterBrowserSide> router_;
  IMPLEMENT_REFCOUNTING(CaptureClient);
};
class CaptureView final : public CefBrowserViewDelegate {
 public: cef_runtime_style_t GetBrowserRuntimeStyle() override { return CEF_RUNTIME_STYLE_ALLOY; }
 private: IMPLEMENT_REFCOUNTING(CaptureView);
};
class CaptureDelegate final : public CefWindowDelegate {
 public:
  CaptureDelegate(std::weak_ptr<CaptureWindow> owner, CefRefPtr<CefBrowserView> view) : owner_(owner), view_(view) {}
  cef_runtime_style_t GetWindowRuntimeStyle() override { return CEF_RUNTIME_STYLE_ALLOY; }
  bool IsFrameless(CefRefPtr<CefWindow>) override { return true; }
  bool CanResize(CefRefPtr<CefWindow>) override { return false; }
  bool CanMaximize(CefRefPtr<CefWindow>) override { return false; }
  bool CanMinimize(CefRefPtr<CefWindow>) override { return false; }
  CefRefPtr<CefWindow> GetParentWindow(CefRefPtr<CefWindow>, bool* is_menu, bool*) override {
    *is_menu = false;  // Owned: above the browser window, over its page area.
    if (const auto owner = owner_.lock()) return owner->parent();
    return nullptr;
  }
  void OnWindowCreated(CefRefPtr<CefWindow> window) override {
    window->SetTitle("Capture — Sol"); window->SetBackgroundColor(kBackground);
    const auto icon = RedWindowIcon(); window->SetWindowIcon(icon); window->SetWindowAppIcon(icon);
    // The page area has small rounded corners; the window follows them.
    const DWM_WINDOW_CORNER_PREFERENCE corners = DWMWCP_ROUNDSMALL;
    DwmSetWindowAttribute(window->GetWindowHandle(), DWMWA_WINDOW_CORNER_PREFERENCE, &corners, sizeof(corners));
    window->SetToFillLayout(); window->AddChildView(view_);
    if (auto owner = owner_.lock()) owner->Created(window);
  }
  void OnWindowActivationChanged(CefRefPtr<CefWindow>, bool active) override {
    if (auto owner = owner_.lock()) owner->ActivationChanged(active);
  }
  bool CanClose(CefRefPtr<CefWindow>) override {
    const auto browser = view_ ? view_->GetBrowser() : nullptr;
    return !browser || browser->GetHost()->TryCloseBrowser();
  }
  void OnWindowDestroyed(CefRefPtr<CefWindow>) override { view_ = nullptr; if (auto owner = owner_.lock()) owner->Destroyed(); }
 private:
  std::weak_ptr<CaptureWindow> owner_; CefRefPtr<CefBrowserView> view_;
  IMPLEMENT_REFCOUNTING(CaptureDelegate);
};
}  // namespace

CefRefPtr<CefWindow> CaptureWindow::parent() const {
  const auto state = state_.lock(); return state ? state->window : nullptr;
}

void CaptureWindow::Start() {
  const auto state = state_.lock();
  if (!state || !state->window || !state->content || !state->ui_context || is_open() || snapping_) return;
  page_ = state->content;
  finishing_ = full_failed_ = full_running_ = false; full_passes_ = 0; ++full_request_;
  Snap();
  state->Record("capture_window_started");
}

void CaptureWindow::Snap() {
  if (!page_) return;
  snapping_ = true;
  CefRefPtr<Snapper> snapper = new Snapper(weak_from_this(), ++request_);
  snapper->Start(page_);
}

void CaptureWindow::Snapped(unsigned request, std::shared_ptr<Shot> shot) {
  if (request != request_) return;  // A newer picture was asked for.
  snapping_ = false;
  const auto state = state_.lock();
  if (!state) return;
  if (!shot) {
    if (pending_scroll_) { pending_scroll_->Failure(500, "Could not take a new picture"); pending_scroll_ = nullptr; }
    if (!window_ && !creating_ && state->workspace) state->workspace->Notify("Could not capture this page.");
    return;
  }
  shot_ = std::move(shot);
  ++generation_;
  if (browser_) PublishFrame(browser_->GetIdentifier(), generation_, shot_);
  auto details = CefDictionaryValue::Create();
  details->SetInt("generation", static_cast<int>(generation_)); details->SetInt("boxes", static_cast<int>(shot_->boxes.size()));
  details->SetDouble("view_x", shot_->view_x); details->SetDouble("view_y", shot_->view_y);
  state->Record("capture_frame", details);
  if (pending_scroll_) { pending_scroll_->Success(Response()); pending_scroll_ = nullptr; }
  if (window_ || creating_) return;
  creating_ = true;
  CefBrowserSettings settings; settings.background_color = kBackground;
  auto extra = CefDictionaryValue::Create(); extra->SetBool("trusted_ui", true);
  auto view = CefBrowserView::CreateBrowserView(new CaptureClient(weak_from_this()), std::string(kCaptureUrl), settings, extra,
                                                state->ui_context, new CaptureView());
  view->SetBackgroundColor(settings.background_color);
  CefWindow::CreateTopLevelWindow(new CaptureDelegate(weak_from_this(), view));
}

void CaptureWindow::Created(CefRefPtr<CefWindow> window) {
  window_ = window;
  // Exactly over the page area (until the document is ready, it stays hidden).
  const auto state = state_.lock();
  if (!state || !state->window || !state->embedded) return;
  const auto area = page_ ? state->embedded->ExpectedPageBounds(page_->GetIdentifier()) : CefRect();
  const auto origin = state->window->GetClientAreaBoundsInScreen();
  const double scale = state->window->GetDisplay()->GetDeviceScaleFactor();
  window->SetBounds(CefRect(origin.x + static_cast<int>(std::lround(area.x / scale)), origin.y + static_cast<int>(std::lround(area.y / scale)),
                            static_cast<int>(std::lround(area.width / scale)), static_cast<int>(std::lround(area.height / scale))));
}

void CaptureWindow::Show() {
  if (!window_ || !ready_) return;
  window_->Show(); window_->Activate();
  if (browser_) browser_->GetHost()->SetFocus(true);
}

void CaptureWindow::BrowserCreated(CefRefPtr<CefBrowser> browser) {
  browser_ = browser; creating_ = false;
  if (const auto state = state_.lock()) {
    state->capture_id = browser->GetIdentifier(); RegisterSessionUi(state, browser->GetIdentifier());
    if (shot_) PublishFrame(browser->GetIdentifier(), generation_, shot_);
    state->browsers[browser->GetIdentifier()] = browser;
    if (!state->window || (state->workspace && state->workspace->WindowClosing())) browser->GetHost()->CloseBrowser(true);
  }
}

void CaptureWindow::BrowserClosed(int id) {
  browser_ = nullptr; ready_ = creating_ = false;
  PublishFrame(id, 0, nullptr);
  if (pending_scroll_) { pending_scroll_->Failure(410, "Capture closed"); pending_scroll_ = nullptr; }
  if (pending_full_) { pending_full_->Failure(410, "Capture closed"); pending_full_ = nullptr; }
  if (const auto state = state_.lock()) {
    UnregisterSessionUi(state, id); state->browsers.erase(id); state->capture_id = -1;
    if (state->window_close) state->window_close->OnClosed(id);
    if (!state->window && state->browsers.empty()) QuitIfLastWindow(state);
  }
}

void CaptureWindow::Destroyed() {
  window_ = nullptr; ready_ = false; shot_.reset(); page_ = nullptr; finishing_ = activated_ = full_running_ = closing_after_full_ = false; reclaimed_ = 0; ++full_request_;
  if (const auto state = state_.lock()) state->Record("capture_window_closed");
  // The page in front gets the keyboard back.
  if (const auto state = state_.lock(); state && state->window) {
    state->window->Activate();
    if (state->content) FocusBrowser(state->content);
  }
}

void CaptureWindow::ActivationChanged(bool active) {
  if (const auto state = state_.lock()) {
    auto details = CefDictionaryValue::Create(); details->SetBool("active", active); state->Record("capture_window_active", details);
  }
  // A click outside (the sidebar, another app) ends the capture, as with a menu; not while it is saving, and not
  // before the window was ever active (Windows can refuse it the foreground and report that as a deactivation).
  if (active) { activated_ = true; return; }
  if (!activated_ || finishing_ || !window_) return;
  // A page can pull Sol's window to the front by itself (window.focus(), focus juggling on Reddit): that is not a
  // person leaving the capture. Another app in front, or a mouse button held on Sol's window, is.
  const HWND front = GetForegroundWindow();
  DWORD process = 0;
  if (front) GetWindowThreadProcessId(front, &process);
  const bool pressed = ((GetAsyncKeyState(VK_LBUTTON) | GetAsyncKeyState(VK_RBUTTON) | GetAsyncKeyState(VK_MBUTTON)) & 0x8000) != 0;
  if (front && process == GetCurrentProcessId() && !pressed && reclaimed_ < 5) {
    ++reclaimed_;
    if (const auto state = state_.lock()) state->Record("capture_window_reclaimed");
    window_->Activate();
    if (browser_) browser_->GetHost()->SetFocus(true);
    return;
  }
  if (const auto state = state_.lock()) {
    wchar_t name[64] = {}; if (front) GetClassNameW(front, name, 64);
    auto details = CefDictionaryValue::Create(); details->SetString("front", std::wstring(name)); details->SetBool("ours", process == GetCurrentProcessId());
    details->SetBool("pressed", pressed); state->Record("capture_window_left", details);
  }
  Close();
}

void CaptureWindow::Close() {
  if (!window_) return;
  finishing_ = true;  // Closing: no reclaiming, scrolling or second answer on the way out.
  if (full_running_) {
    // A whole-page picture resizes the page until it is taken: the window covers it until then (at most 1.5 s).
    if (closing_after_full_) return;
    closing_after_full_ = true;
    CefPostDelayedTask(TID_UI, base::BindOnce([](std::weak_ptr<CaptureWindow> weak) {
      if (const auto self = weak.lock(); self && self->window_) self->window_->Close();
    }, weak_from_this()), 1500);
    return;
  }
  window_->Close();
}

std::shared_ptr<const std::string> CaptureFrame(int browser, const std::string& path) {
  auto& store = FrameStore(); std::lock_guard lock(store.mutex);
  const auto found = store.pictures.find(browser);
  if (found == store.pictures.end()) return nullptr;
  const auto generation = std::to_string(found->second.first);
  const auto& shot = found->second.second;
  // The picture (the view, or the whole page), and the picture of the view as it was on screen.
  if (path == "/capture-frame-" + generation + ".png") return std::shared_ptr<const std::string>(shot, &shot->png);
  if (path == "/capture-view-" + generation + ".png" && !shot->view_png.empty()) return std::shared_ptr<const std::string>(shot, &shot->view_png);
  return nullptr;
}

std::string CaptureWindow::Response() const {
  auto response = CefDictionaryValue::Create();
  response->SetInt("generation", static_cast<int>(generation_));
  if (shot_) {
    auto view = CefDictionaryValue::Create();
    view->SetDouble("x", shot_->view_x); view->SetDouble("y", shot_->view_y);
    view->SetDouble("width", shot_->view_width); view->SetDouble("height", shot_->view_height);
    response->SetDictionary("view", view);
    auto page = CefDictionaryValue::Create();
    page->SetDouble("width", shot_->page_width); page->SetDouble("height", shot_->page_height);
    response->SetDictionary("page", page);
    auto picture = CefDictionaryValue::Create();
    picture->SetDouble("x", shot_->pic_x); picture->SetDouble("y", shot_->pic_y);
    picture->SetDouble("width", shot_->pic_width); picture->SetDouble("height", shot_->pic_height);
    response->SetDictionary("picture", picture);
    response->SetBool("full", shot_->full);
    response->SetBool("more", FullComing());
    auto boxes = CefListValue::Create();
    size_t i = 0;
    for (const auto& box : shot_->boxes) for (const int v : box) boxes->SetInt(i++, v);
    response->SetList("boxes", boxes);
  }
  auto value = CefValue::Create(); value->SetDictionary(response);
  return CefWriteJSON(value, JSON_WRITER_DEFAULT);
}

// A picture beyond the viewport resizes the page for a moment; it gets back to its size under this window first.
void SettleThenDone(std::weak_ptr<CaptureWindow> weak) {
  CefPostDelayedTask(TID_UI, base::BindOnce([](std::weak_ptr<CaptureWindow> weak) {
    if (const auto self = weak.lock()) self->Done();
  }, std::move(weak)), 150);
}

void CaptureWindow::Done() {
  Close();
}

void CaptureWindow::Finish(CefRefPtr<CefDictionaryValue> data, CefRefPtr<CefMessageRouterBrowserSide::Callback> callback) {
  const auto state = state_.lock();
  const auto kind = data->GetString("kind").ToString(), choice = data->GetString("action").ToString();
  if (!state || !shot_ || !page_ || (choice != "copy" && choice != "download") || (kind != "visible" && kind != "full" && kind != "rect")) {
    callback->Failure(400, "Invalid capture"); return;
  }
  const auto action = choice == "copy" ? ScreenshotAction::Copy : ScreenshotAction::Save;
  // "view": the window still shows the page as it was on screen (fixed headers where they were), not yet scrolled.
  const bool from_view = data->GetType("source") == VTYPE_STRING && data->GetString("source").ToString() == "view";
  auto answer = CefDictionaryValue::Create(); answer->SetString("kind", kind); answer->SetString("action", choice);
  state->Record("capture_answer", answer);
  callback->Success(Response());
  finishing_ = true;
  const auto& s = *shot_;
  const auto inside = [&s](double x, double y, double w, double h) {
    return x >= s.pic_x - 1 && y >= s.pic_y - 1 && x + w <= s.pic_x + s.pic_width + 1 && y + h <= s.pic_y + s.pic_height + 1;
  };
  const double x = Number(data, "x"), y = Number(data, "y"), w = Number(data, "width"), h = Number(data, "height");
  const bool area = std::isfinite(x) && std::isfinite(y) && std::isfinite(w) && std::isfinite(h) && w >= 1 && h >= 1 && x >= -1 && y >= -1 &&
                    x <= 1e6 && y <= 1e6 && w <= 1e6 && h <= 1e6;
  const auto crop = [&](bool region) {
    CropThenDone(weak_from_this(), state_, s.png, (x - s.pic_x) / s.pic_width, (y - s.pic_y) / s.pic_height, w / s.pic_width,
                 h / s.pic_height, action, region);
  };
  const auto in_view = [&s](double x, double y, double w, double h) {
    return x >= s.view_x - 1 && y >= s.view_y - 1 && x + w <= s.view_x + s.view_width + 1 && y + h <= s.view_y + s.view_height + 1;
  };
  if (kind == "visible") {
    // The part of the page the window shows now: the screen as it was, or a part of the whole-page picture.
    if (from_view && !s.view_png.empty()) { DeliverScreenshot(state, s.view_png, action, false, false); Done(); return; }
    if (s.full && area && inside(x, y, w, h)) { crop(false); return; }
    DeliverScreenshot(state, s.png, action, false, false); Done(); return;
  }
  // The display's scale: the picture of the view is in device pixels.
  const int view_width = PngWidth(s.view_png.empty() ? s.png : s.view_png);
  const double dpr = view_width > 0 ? view_width / s.view_width : 1;
  if (kind == "full") {
    if (s.full && s.pic_width >= s.page_width - 1 && s.pic_height >= s.page_height - 1) {
      DeliverScreenshot(state, s.png, action, false, true); Done(); return;
    }
    // Chrome's clip resizes the page for a moment: this window still covers it.
    CaptureDocument(state, page_, true, 0, 0, 1, 1, dpr, action, [weak = weak_from_this()] { SettleThenDone(weak); });
    return;
  }
  if (!area) { finishing_ = false; return; }
  if (from_view && !s.view_png.empty() && in_view(x, y, w, h)) {
    CropThenDone(weak_from_this(), state_, s.view_png, (x - s.view_x) / s.view_width, (y - s.view_y) / s.view_height, w / s.view_width,
                 h / s.view_height, action, true);
    return;
  }
  if (inside(x, y, w, h)) { crop(true); return; }
  CaptureDocument(state, page_, false, x, y, w, h, dpr, action, [weak = weak_from_this()] { SettleThenDone(weak); });
}

void CaptureWindow::StartFull() {
  const auto state = state_.lock();
  if (!state || !shot_ || !page_ || !window_ || finishing_ || full_running_ || !FullComing()) return;
  const auto& s = *shot_;
  const double width = std::ceil(s.page_width), height = std::ceil(s.page_height);
  if (s.view_x <= 0 && s.view_y <= 0 && width <= s.view_width + 1 && height <= s.view_height + 1) {
    // The page fits in the window: the picture of the view is the whole page.
    shot_->full = true; full_passes_ = kFullPasses;
  } else if (width < 1 || height < 1 || width > 8000 || height > 16000) {
    full_failed_ = true;  // Too large for one picture: the page scrolls underneath instead.
  } else {
    const int png_width = PngWidth(s.view_png.empty() ? s.png : s.view_png);
    const double dpr = png_width > 0 ? png_width / s.view_width : 1;
    full_running_ = true; full_started_ = GetTickCount64();
    CefRefPtr<FullSnapper> snapper = new FullSnapper(weak_from_this(), ++full_request_, width, height);
    // Chrome multiplies the clip's scale by the display's: 1 is device pixels, lower only to stay under 40 MP.
    snapper->Start(page_, std::clamp(std::sqrt(40e6 / (width * height * dpr * dpr)), 0.05, 1.0));
    return;
  }
  if (pending_full_) { pending_full_->Success(Response()); pending_full_ = nullptr; }
}

void CaptureWindow::FullSnapped(unsigned request, std::optional<std::string> png, double width, double height) {
  if (request != full_request_) return;
  full_running_ = false;
  if (closing_after_full_) {
    CefPostDelayedTask(TID_UI, base::BindOnce([](std::weak_ptr<CaptureWindow> weak) {
      if (const auto self = weak.lock(); self && self->window_) self->window_->Close();
    }, weak_from_this()), 150);
    return;
  }
  const auto state = state_.lock();
  if (!state || !window_ || finishing_ || !shot_) return;
  auto details = CefDictionaryValue::Create();
  details->SetBool("ok", png.has_value()); details->SetInt("pass", static_cast<int>(full_passes_ + 1));
  details->SetDouble("width", width); details->SetDouble("height", height);
  details->SetInt("ms", static_cast<int>(GetTickCount64() - full_started_));
  state->Record("capture_full", details);
  if (!png) {
    full_failed_ = true;
  } else {
    auto next = std::make_shared<Shot>(*shot_);
    next->png = std::move(*png); next->full = true;
    next->pic_x = 0; next->pic_y = 0; next->pic_width = width; next->pic_height = height;
    shot_ = std::move(next); ++generation_; ++full_passes_;
    // A slow picture (a long page, a high display scale) is not taken twice: Save would wait for it.
    if (GetTickCount64() - full_started_ > 1000) full_passes_ = kFullPasses;
    if (browser_) PublishFrame(browser_->GetIdentifier(), generation_, shot_);
    // Again a moment later: images the page loads only when they come into view are in the second picture.
    if (FullComing()) CefPostDelayedTask(TID_UI, base::BindOnce([](std::weak_ptr<CaptureWindow> weak) {
      if (const auto self = weak.lock()) self->StartFull();
    }, weak_from_this()), 1200);
  }
  if (pending_full_) { pending_full_->Success(Response()); pending_full_ = nullptr; }
}

bool CaptureWindow::Query(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, const CefString& raw, bool persistent,
                          CefRefPtr<CefMessageRouterBrowserSide::Callback> callback) {
  const auto state = state_.lock();
  if (!state || !IsCaptureUi(browser->GetIdentifier(), state->capture_id, frame->IsMain(), frame->GetURL().ToString())) {
    callback->Failure(403, "Untrusted capture document"); return true;
  }
  if (persistent || raw.length() > 1024) { callback->Failure(400, "Invalid capture request"); return true; }
  const auto value = CefParseJSON(raw, JSON_PARSER_RFC);
  if (!value || value->GetType() != VTYPE_DICTIONARY || value->GetDictionary()->GetType("method") != VTYPE_STRING) {
    callback->Failure(400, "Invalid capture request"); return true;
  }
  const auto data = value->GetDictionary(); const auto method = data->GetString("method").ToString();
  if (HandleUiFocus(state, browser, method, data->GetSize(), persistent, callback)) return true;
  if (!state->workspace || state->workspace->WindowClosing()) { callback->Failure(409, "Browser is closing"); return true; }
  if (method == "capture.ready" && data->GetSize() == 1) {
    ready_ = true; callback->Success(Response()); Show();
    // The whole page once the window covers it (a moment for it to be on screen).
    CefPostDelayedTask(TID_UI, base::BindOnce([](std::weak_ptr<CaptureWindow> weak) {
      if (const auto self = weak.lock()) self->StartFull();
    }, weak_from_this()), 120);
    return true;
  }
  if (method == "capture.full" && data->GetSize() == 2 && data->GetType("after") == VTYPE_INT) {
    // Answers when a whole-page picture newer than |after| is there, or when none is coming.
    if ((shot_ && shot_->full && generation_ > static_cast<unsigned>(std::max(0, data->GetInt("after")))) || !FullComing()) {
      callback->Success(Response()); return true;
    }
    if (pending_full_) pending_full_->Success(Response());
    pending_full_ = callback;
    return true;
  }
  if (method == "capture.cancel" && data->GetSize() == 1) {
    auto answer = CefDictionaryValue::Create(); answer->SetString("kind", "cancel"); state->Record("capture_answer", answer);
    callback->Success(Response()); if (!finishing_) Close(); return true;
  }
  if (method == "capture.scroll" && data->GetSize() == 5 && !finishing_ && page_) {
    // The wheel turns the page underneath at the pointer, then a new picture replaces the old one.
    const double x = Number(data, "x"), y = Number(data, "y"), dx = Number(data, "dx"), dy = Number(data, "dy");
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(dx) || !std::isfinite(dy) || std::abs(dx) > 1e5 || std::abs(dy) > 1e5) {
      callback->Failure(400, "Invalid scroll"); return true;
    }
    auto wheel = CefDictionaryValue::Create();
    wheel->SetString("type", "mouseWheel"); wheel->SetDouble("x", std::max(0.0, x)); wheel->SetDouble("y", std::max(0.0, y));
    wheel->SetDouble("deltaX", dx); wheel->SetDouble("deltaY", dy);
    page_->GetHost()->ExecuteDevToolsMethod(0, "Input.dispatchMouseEvent", wheel);
    if (pending_scroll_) pending_scroll_->Success(Response());
    pending_scroll_ = callback;
    CefPostDelayedTask(TID_UI, base::BindOnce([](std::weak_ptr<CaptureWindow> weak) {
      if (const auto self = weak.lock(); self && self->window_) self->Snap();
    }, weak_from_this()), 140);
    return true;
  }
  if (method == "capture.finish" && !finishing_) { Finish(data, callback); return true; }
  callback->Failure(400, "Unsupported capture command"); return true;
}
}  // namespace red
