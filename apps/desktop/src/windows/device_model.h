#pragma once
#include <cmath>
#include <optional>
namespace red {
struct DeviceConfig {
  bool enabled=false,mobile=true,touch=true;
  int width=390,height=844;
  double dpr=3,scale=0;
  bool operator==(const DeviceConfig&) const = default;
};
struct DeviceCommands { bool metrics=false,touch=false,user_agent=false; };
inline DeviceCommands PlanDeviceCommands(const std::optional<DeviceConfig>& previous,
                                        const DeviceConfig& next,double previous_scale,double scale) {
  if (!previous) return {true,true,true};
  const auto& p=*previous;
  return {
    p.enabled!=next.enabled || (next.enabled && (p.width!=next.width || p.height!=next.height || p.dpr!=next.dpr || p.mobile!=next.mobile || std::abs(previous_scale-scale)>.001)),
    (p.enabled&&p.touch)!=(next.enabled&&next.touch) || (next.enabled&&next.touch&&p.mobile!=next.mobile),
    (p.enabled&&p.mobile)!=(next.enabled&&next.mobile)
  };
}
}
