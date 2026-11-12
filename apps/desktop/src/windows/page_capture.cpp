#include "windows/page_capture.h"
#include "windows/image_scale.h"
#include <windows.h>
#include <shlobj.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>
#include <filesystem>
#include <fstream>
#include "include/base/cef_callback.h"
#include "include/cef_devtools_message_observer.h"
#include "include/cef_parser.h"
#include "include/wrapper/cef_closure_task.h"
#include "app/browsing_profiles.h"
#include "app/preferences.h"
#include "app/session.h"
#include "downloads/download_manager.h"
#include "downloads/download_names.h"
#include "windows/capture_window.h"
#include "windows/embedded_content.h"
#include "workspace/controller.h"

namespace red {
namespace {
constexpr int kMaxPageHeight = 16000;  // CSS pixels: beyond this the GPU cannot make one picture.
constexpr int kMaxPageWidth = 8000;
constexpr size_t kMaxPng = 48u * 1024 * 1024;
constexpr double kMaxPixels = 40e6;    // The largest picture, in device pixels.

// Screenshots are saved as downloads are: the download folder from Settings (Windows Downloads by default),
// and "Ask where to save each file" asks here too.
struct SaveTarget { std::filesystem::path directory; bool ask = false; };
SaveTarget ScreenshotTarget(const std::shared_ptr<State>& state) {
  SaveTarget target{state->downloads_directory.empty() ? DownloadManager::DefaultDirectory() : state->downloads_directory,
                    state->probe && state->screenshot_ask_probe};
  if (!state->probe && state->session && state->session->preferences) {
    const auto& prefs = state->session->preferences->Get();
    if (!prefs.download_directory.empty()) target.directory = prefs.download_directory;
    target.ask = prefs.ask_download;
  }
  return target;
}

bool WriteFile(const std::filesystem::path& path, const std::string& png) {
  std::ofstream out(path, std::ios::binary);
  return static_cast<bool>(out.write(png.data(), static_cast<std::streamsize>(png.size())));
}

class SaveChosen final : public CefRunFileDialogCallback {
 public:
  explicit SaveChosen(std::function<void(const std::vector<CefString>&)> done) : done_(std::move(done)) {}
  void OnFileDialogDismissed(const std::vector<CefString>& paths) override { done_(paths); }
 private:
  std::function<void(const std::vector<CefString>&)> done_;
  IMPLEMENT_REFCOUNTING(SaveChosen);
};

std::wstring FileName() {
  const auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
  std::tm local{};
  localtime_s(&local, &now);
  wchar_t text[64];
  wcsftime(text, std::size(text), L"Sol Screenshot %Y-%m-%d at %H.%M.%S.png", &local);
  return text;
}

// "PNG" is the clipboard format browsers, chat and design apps read for pasted images.
HGLOBAL GlobalCopy(const std::string& bytes) {
  HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes.size());
  if (!memory) return nullptr;
  void* destination = GlobalLock(memory);
  if (!destination) { GlobalFree(memory); return nullptr; }
  std::memcpy(destination, bytes.data(), bytes.size());
  GlobalUnlock(memory);
  return memory;
}
// The picture on the clipboard as "PNG" (browsers, Office, chat apps that keep the quality) and as a Windows
// bitmap, CF_DIB (Paint, Telegram, most other apps read only that; Windows makes CF_BITMAP and CF_DIBV5 from it).
bool CopyPng(HWND owner, const std::string& png) {
  HGLOBAL picture = GlobalCopy(png);
  if (!picture) return false;
  const auto packed = PackedDib(png, 16ull * 1000 * 1000);  // Larger ones only as PNG (64 MB of pixels at most here).
  HGLOBAL bitmap = packed ? GlobalCopy(*packed) : nullptr;
  if (!OpenClipboard(owner)) { GlobalFree(picture); if (bitmap) GlobalFree(bitmap); return false; }
  bool copied = EmptyClipboard() && SetClipboardData(RegisterClipboardFormatW(L"PNG"), picture);
  if (!copied) GlobalFree(picture);
  if (bitmap && !(copied && SetClipboardData(CF_DIB, bitmap))) GlobalFree(bitmap);
  CloseClipboard();
  return copied;
}

// Chrome writes whole numbers as integers: read either kind.
double Number(CefRefPtr<CefDictionaryValue> dictionary, const char* key) {
  const auto type = dictionary->GetType(key);
  return type == VTYPE_INT ? dictionary->GetInt(key) : type == VTYPE_DOUBLE ? dictionary->GetDouble(key) : std::nan("");
}

void Record(const std::shared_ptr<State>& state, bool ok, bool region, bool full, bool saved, bool copied) {
  auto details = CefDictionaryValue::Create();
  details->SetBool("ok", ok); details->SetBool("full", full); details->SetBool("region", region);
  details->SetBool("saved", saved); details->SetBool("copied", copied);
  state->Record("page_screenshot", details);
}

// One capture of the page: the visible part (a surface copy: no clip, nothing on screen moves), or a
// region / the whole page with Chrome's clip (the page's size first for the whole page).
class Capture final : public CefDevToolsMessageObserver {
 public:
  Capture(std::weak_ptr<State> state, CefRefPtr<CefBrowser> page, bool full, bool region, double x, double y, double width, double height,
          double scale, ScreenshotAction action, std::function<void()> done)
      : state_(std::move(state)), page_(page), full_(full), region_(region), x_(x), y_(y), width_(width), height_(height), scale_(scale),
        action_(action), done_callback_(std::move(done)) {}
  void Start(CefRefPtr<CefRegistration> registration) {
    registration_ = registration;
    if (full_) layout_id_ = page_->GetHost()->ExecuteDevToolsMethod(0, "Page.getLayoutMetrics", nullptr);
    else Shoot();
    // A page that is hidden or busy may never answer: the person is told, not left waiting.
    CefPostDelayedTask(TID_UI, base::BindOnce(&Capture::TimedOut, CefRefPtr<Capture>(this)), 10000);
  }
  void OnDevToolsMethodResult(CefRefPtr<CefBrowser>, int id, bool success, const void* data, size_t size) override {
    std::string json;
    if (success && size <= 2 * kMaxPng) json.assign(static_cast<const char*>(data), size);
    CefPostTask(TID_UI, base::BindOnce(&Capture::Result, CefRefPtr<Capture>(this), id, std::move(json)));
  }

 private:
  void TimedOut() { if (!finished_) Fail(); }
  void Shoot() {
    auto params = CefDictionaryValue::Create();
    params->SetString("format", "png");
    if (full_ || region_) {
      auto clip = CefDictionaryValue::Create();
      clip->SetDouble("x", x_); clip->SetDouble("y", y_); clip->SetDouble("width", width_); clip->SetDouble("height", height_);
      // Chrome multiplies the clip's scale by the display's: 1 is device pixels. Lower only to stay under kMaxPixels.
      clip->SetDouble("scale", std::clamp(std::sqrt(kMaxPixels / (width_ * height_ * scale_ * scale_)), 0.05, 1.0));
      params->SetDictionary("clip", clip);
      params->SetBool("captureBeyondViewport", true);
    } else {
      params->SetBool("fromSurface", true); params->SetBool("captureBeyondViewport", false);
    }
    shot_id_ = page_->GetHost()->ExecuteDevToolsMethod(0, "Page.captureScreenshot", params);
  }
  void Result(int id, std::string json) {
    if (finished_ || (id != layout_id_ && id != shot_id_)) return;
    const auto value = json.empty() ? nullptr : CefParseJSON(json, JSON_PARSER_RFC);
    if (!value || value->GetType() != VTYPE_DICTIONARY) { Fail(); return; }
    const auto result = value->GetDictionary();
    if (id == layout_id_) {
      const auto size = result->GetDictionary("cssContentSize");
      const double full_width = size ? Number(size, "width") : std::nan(""), full_height = size ? Number(size, "height") : std::nan("");
      if (!std::isfinite(full_width) || !std::isfinite(full_height)) { Fail(); return; }
      x_ = 0; y_ = 0;
      width_ = std::clamp(std::round(full_width), 1.0, double(kMaxPageWidth));
      height_ = std::clamp(std::round(full_height), 1.0, double(kMaxPageHeight));
      Shoot();
      return;
    }
    const auto binary = CefBase64Decode(result->GetString("data"));
    if (!binary || binary->GetSize() == 0 || binary->GetSize() > kMaxPng) { Fail(); return; }
    std::string png(binary->GetSize(), 0);
    binary->GetData(png.data(), png.size(), 0);
    finished_ = true;
    registration_ = nullptr;
    if (const auto state = state_.lock()) DeliverScreenshot(state, std::move(png), action_, region_, full_);
    if (done_callback_) done_callback_();
  }
  void Fail() {
    finished_ = true;
    registration_ = nullptr;
    if (const auto state = state_.lock()) {
      if (state->workspace) state->workspace->Notify("Could not take the screenshot.");
      Record(state, false, region_, full_, false, false);
    }
    if (done_callback_) done_callback_();
  }
  std::weak_ptr<State> state_;
  CefRefPtr<CefBrowser> page_;
  CefRefPtr<CefRegistration> registration_;
  bool full_, region_;
  double x_, y_, width_, height_, scale_;  // |scale_|: the display's scale (device pixels per CSS pixel).
  ScreenshotAction action_;
  std::function<void()> done_callback_;
  bool finished_ = false;
  int layout_id_ = -1, shot_id_ = -1;
  IMPLEMENT_REFCOUNTING(Capture);
};
}  // namespace

void DeliverScreenshot(const std::shared_ptr<State>& state, std::string png, ScreenshotAction action, bool region, bool full) {
  if (!state) return;
  const bool copied = action != ScreenshotAction::Save && CopyPng(state->window ? state->window->GetWindowHandle() : nullptr, png);
  // A saved picture is listed with the downloads, from the profile of the page it shows (as its downloads are).
  const auto page = state->content;
  const auto downloads = [page](const std::shared_ptr<State>& state) {
    const auto profile = page ? ProfileForBrowser(state, page) : nullptr;
    return profile && profile->downloads ? profile->downloads : state->downloads;
  };
  const std::string url = page && page->GetMainFrame() ? page->GetMainFrame()->GetURL().ToString() : "";
  const auto finish = [](const std::shared_ptr<State>& state, bool saved, bool copied, bool region, bool full) {
    const bool ok = saved || copied;
    if (ok && state->embedded)
      state->embedded->Notify("copy", copied ? (saved ? L"Screenshot saved and copied" : L"Screenshot copied") : L"Screenshot saved");
    else if (!ok && state->workspace) state->workspace->Notify("Could not take the screenshot.");
    Record(state, ok, region, full, saved, copied);
  };
  if (action == ScreenshotAction::Copy) { finish(state, false, copied, region, full); return; }
  const auto target = ScreenshotTarget(state);
  std::error_code error;
  if (!target.directory.empty() && !std::filesystem::is_directory(target.directory, error)) std::filesystem::create_directories(target.directory, error);
  const auto name = FileName();
  const auto host = state->content ? state->content->GetHost() : nullptr;
  if (target.ask && host) {
    // As with a download: Windows' "Save as", in the download folder, with the name filled in.
    auto picture = std::make_shared<std::string>(std::move(png));
    host->RunFileDialog(FILE_DIALOG_SAVE, "Save screenshot", (target.directory / name).wstring(), {".png"},
        new SaveChosen([finish, downloads, url, weak = std::weak_ptr<State>(state), picture, copied, region, full](const std::vector<CefString>& paths) {
          const auto state = weak.lock();
          if (!state) return;
          if (paths.size() != 1) {  // Cancelled: the copy (if any) stands, no file.
            if (copied) finish(state, false, true, region, full);
            else { auto details = CefDictionaryValue::Create(); details->SetBool("cancelled", true); state->Record("page_screenshot", details); }
            return;
          }
          std::filesystem::path path = paths.front().ToWString();
          if (path.extension() != L".png") path += L".png";
          const bool saved = path.is_absolute() && WriteFile(path, *picture);
          if (const auto list = saved ? downloads(state) : nullptr) list->AddFinishedFile(path, url);
          finish(state, saved, copied, region, full);
        }));
    return;
  }
  // Never overwrite: "Sol Screenshot ... (1).png", as downloads do.
  const auto path = UniqueDownloadPath(target.directory, name, [](const std::filesystem::path& p) { std::error_code e; return std::filesystem::exists(p, e); });
  const bool saved = !target.directory.empty() && WriteFile(path, png);
  if (const auto list = saved ? downloads(state) : nullptr) list->AddFinishedFile(path, url);
  finish(state, saved, copied, region, full);
}

void CaptureDocument(const std::shared_ptr<State>& state, CefRefPtr<CefBrowser> page, bool full, double x, double y, double width,
                     double height, double dpr, ScreenshotAction action, std::function<void()> done) {
  if (!state || !page) { if (done) done(); return; }
  const double device = std::clamp(std::isfinite(dpr) ? dpr : 1.0, 1.0, 4.0);
  width = std::clamp(std::round(width), 1.0, double(kMaxPageWidth)); height = std::clamp(std::round(height), 1.0, double(kMaxPageHeight));
  CefRefPtr<Capture> capture = new Capture(state, page, full, !full, std::max(0.0, x), std::max(0.0, y), width, height, device, action, std::move(done));
  capture->Start(page->GetHost()->AddDevToolsMessageObserver(capture));
}

void CapturePage(const std::shared_ptr<State>& state, bool full_page) {
  const auto page = state ? state->content : nullptr;
  if (!page || !state->workspace || state->workspace->WindowClosing()) return;
  if (full_page) { CaptureDocument(state, page, true, 0, 0, 1, 1, 1, ScreenshotAction::Both); return; }
  CefRefPtr<Capture> capture = new Capture(state, page, false, false, 0, 0, 0, 0, 1, ScreenshotAction::Both, nullptr);
  capture->Start(page->GetHost()->AddDevToolsMessageObserver(capture));
}

void StartCapturePicker(const std::shared_ptr<State>& state) {
  if (!state || !state->workspace || state->workspace->WindowClosing()) return;
  if (!state->content) { state->workspace->Notify("Open a page to capture it."); return; }
  if (!state->capture_window) state->capture_window = std::make_shared<CaptureWindow>(state);
  state->capture_window->Start();
}
}  // namespace red
