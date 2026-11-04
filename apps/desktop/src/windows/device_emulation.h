#pragma once
#include "app/state.h"
#include "windows/device_model.h"
namespace red {
class DeviceEmulation : public std::enable_shared_from_this<DeviceEmulation> {
 public:
  DeviceEmulation(std::weak_ptr<State> state, CefRefPtr<CefBrowser> page) : state_(std::move(state)), page_(page) {}
  ~DeviceEmulation();
  void Set(const DeviceConfig& config);
  void Apply(double scale);
  void Result(int id, bool success);
  const DeviceConfig& config() const { return config_; }
  bool pending() const { return busy_; }
  const std::string& error() const { return error_; }
  double effective_scale() const { return scale_; }
 private:
  void Send();
  void Reset();
  void Changed();
  void Timeout(unsigned batch);
  std::weak_ptr<State> state_;
  CefRefPtr<CefBrowser> page_;
  CefRefPtr<CefRegistration> observer_;
  DeviceConfig config_;
  std::optional<DeviceConfig> last_sent_;
  double last_scale_=1;
  double scale_ = 1;
  bool busy_ = false, dirty_ = false, sent_ = false, batch_enabled_ = false;
  unsigned batch_ = 0;
  std::map<int, std::string> requests_;
  std::string error_;
};
}  // namespace red
