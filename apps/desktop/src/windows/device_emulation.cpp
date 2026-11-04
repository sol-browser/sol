#include "windows/device_emulation.h"
#include "windows/embedded_content.h"
#include "workspace/controller.h"
#include "include/cef_devtools_message_observer.h"
#include "include/cef_version.h"
#include "include/cef_app.h"
#include "include/base/cef_callback.h"
#include "include/wrapper/cef_closure_task.h"
#include <cmath>
namespace red {
namespace {
class DeviceObserver final : public CefDevToolsMessageObserver {
 public:
  explicit DeviceObserver(std::weak_ptr<DeviceEmulation> owner) : owner_(std::move(owner)) {}
  void OnDevToolsMethodResult(CefRefPtr<CefBrowser>, int id, bool success, const void*, size_t) override {
    // Browser-domain commands may reply inside ExecuteDevToolsMethod. Defer
    // delivery until Send has registered every returned request ID.
    CefPostTask(TID_UI, base::BindOnce([](std::weak_ptr<DeviceEmulation> weak, int id, bool success) {
      if (auto owner = weak.lock()) owner->Result(id, success);
    }, owner_, id, success));
  }
 private:
  std::weak_ptr<DeviceEmulation> owner_;
  IMPLEMENT_REFCOUNTING(DeviceObserver);
};
}
DeviceEmulation::~DeviceEmulation() { Reset(); }
void DeviceEmulation::Set(const DeviceConfig& config) {
  if (config_==config && error_.empty()) return;
  config_ = config; error_.clear(); dirty_ = true;
  if (!observer_) observer_ = page_->GetHost()->AddDevToolsMessageObserver(new DeviceObserver(weak_from_this()));
  if (!config.enabled) Apply(1);
}
void DeviceEmulation::Apply(double scale) {
  if (std::abs(scale_ - scale) > .001) { scale_ = scale; dirty_ = true; }
  if (!busy_ && dirty_) Send();
}
void DeviceEmulation::Send() {
  if (!page_ || !page_->IsValid()) return;
  dirty_ = false; busy_ = true; batch_enabled_ = config_.enabled; sent_ = true;
  const auto changes=PlanDeviceCommands(last_sent_,config_,last_scale_,scale_);
  last_sent_=config_; last_scale_=scale_;
  ++batch_;
  auto send = [&](const char* method, CefRefPtr<CefDictionaryValue> params) {
    if (auto s=state_.lock();s && s->devtools_regression) {
      auto data=CefDictionaryValue::Create();data->SetString("method",method);data->SetInt("batch",batch_);
      s->Record("devtools_probe_command",data);
    }
    const int id = page_->GetHost()->ExecuteDevToolsMethod(0, method, params);
    if (id > 0) requests_[id] = method; else error_ = "Device command could not be sent";
  };
  auto metrics = CefDictionaryValue::Create();
  if (changes.metrics && config_.enabled) {
    metrics->SetInt("width", config_.width); metrics->SetInt("height", config_.height);
    metrics->SetInt("screenWidth", config_.width); metrics->SetInt("screenHeight", config_.height);
    metrics->SetDouble("deviceScaleFactor", config_.dpr); metrics->SetBool("mobile", config_.mobile);
    metrics->SetDouble("scale", scale_);
    auto orientation = CefDictionaryValue::Create();
    const bool landscape = config_.width > config_.height;
    orientation->SetString("type", landscape ? "landscapePrimary" : "portraitPrimary"); orientation->SetInt("angle", landscape ? 90 : 0);
    metrics->SetDictionary("screenOrientation", orientation);
    send("Emulation.setDeviceMetricsOverride", metrics);
  } else if (changes.metrics) {
    send("Emulation.clearDeviceMetricsOverride", metrics);
    send("Emulation.resetPageScaleFactor", nullptr);
  }
  if (changes.touch) {
    auto touch = CefDictionaryValue::Create(); touch->SetBool("enabled", config_.enabled && config_.touch);
    touch->SetInt("maxTouchPoints", 1); send("Emulation.setTouchEmulationEnabled", touch);
    auto mouse = CefDictionaryValue::Create(); mouse->SetBool("enabled", config_.enabled && config_.touch);
    mouse->SetString("configuration", config_.mobile ? "mobile" : "desktop"); send("Emulation.setEmitTouchEventsForMouse", mouse);
  }
  if (changes.user_agent) {
  auto ua = CefDictionaryValue::Create();
  if (config_.enabled && config_.mobile) {
    const std::string major = std::to_string(CHROME_VERSION_MAJOR);
    ua->SetString("userAgent", "Mozilla/5.0 (Linux; Android 14; K) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/" + major + ".0.0.0 Mobile Safari/537.36");
    ua->SetString("platform", "Linux armv8l");
    auto metadata = CefDictionaryValue::Create();
    metadata->SetString("platform", "Android"); metadata->SetString("platformVersion", "14.0.0");
    metadata->SetString("architecture", "arm"); metadata->SetString("model", "K"); metadata->SetBool("mobile", true);
    ua->SetDictionary("userAgentMetadata", metadata);
  } else ua->SetString("userAgent", "");  // Empty restores Chromium's own UA and Client Hints.
  send("Emulation.setUserAgentOverride", ua);
  }
  if (requests_.empty()) { busy_=false; Changed(); return; }
  CefPostDelayedTask(TID_UI, base::BindOnce([](std::weak_ptr<DeviceEmulation> weak, unsigned batch) {
    if (auto owner = weak.lock()) owner->Timeout(batch);
  }, weak_from_this(), batch_), 5000);
}
void DeviceEmulation::Result(int id, bool success) {
  const auto at = requests_.find(id); if (at == requests_.end()) return;
  if (auto s = state_.lock(); s && s->input_probe) {
    auto details = CefDictionaryValue::Create(); details->SetString("method", at->second); details->SetBool("success", success);
    s->Record("device_emulation_result", details);
  }
  if (!success && error_.empty()) error_ = "Device preview command failed: " + at->second;
  requests_.erase(at); if (!requests_.empty()) return;
  busy_ = false;
  if (!error_.empty() && batch_enabled_) { config_.enabled = false; dirty_ = true; last_sent_.reset(); }
  if (dirty_) Send();
  Changed();
}
void DeviceEmulation::Timeout(unsigned batch) {
  if (!busy_ || batch_ != batch) return;
  busy_ = false; dirty_ = false; requests_.clear();
  error_ = "Device preview command timed out"; config_.enabled = false; Reset(); Changed();
}
void DeviceEmulation::Reset() {
  if (!sent_ || !page_ || !page_->IsValid()) return;
  page_->GetHost()->ExecuteDevToolsMethod(0, "Emulation.clearDeviceMetricsOverride", nullptr);
  auto off = CefDictionaryValue::Create(); off->SetBool("enabled", false);
  page_->GetHost()->ExecuteDevToolsMethod(0, "Emulation.setTouchEmulationEnabled", off);
  page_->GetHost()->ExecuteDevToolsMethod(0, "Emulation.setEmitTouchEventsForMouse", off);
  auto ua = CefDictionaryValue::Create(); ua->SetString("userAgent", "");
  page_->GetHost()->ExecuteDevToolsMethod(0, "Emulation.setUserAgentOverride", ua);
  page_->GetHost()->ExecuteDevToolsMethod(0, "Emulation.resetPageScaleFactor", nullptr);
  sent_=false; last_sent_.reset();
}
void DeviceEmulation::Changed() {
  if (auto s = state_.lock(); s && s->workspace && !s->workspace->WindowClosing()) {
    if (s->embedded) s->embedded->Relayout();
    if (!error_.empty()) s->workspace->Notify(error_);
    s->workspace->Refresh();
  }
}
}  // namespace red
