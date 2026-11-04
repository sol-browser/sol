#include "windows/device_model.h"
#include <cstdlib>
namespace { void Check(bool ok) { if (!ok) std::abort(); } }
int main() {
  red::DeviceConfig phone; phone.enabled=true;
  auto plan=red::PlanDeviceCommands(std::nullopt,phone,1,.5);
  Check(plan.metrics && plan.touch && plan.user_agent);
  plan=red::PlanDeviceCommands(phone,phone,.5,.5);
  Check(!plan.metrics && !plan.touch && !plan.user_agent);
  plan=red::PlanDeviceCommands(phone,phone,.5,.7);
  Check(plan.metrics && !plan.touch && !plan.user_agent);
  auto rotated=phone; rotated.width=phone.height;rotated.height=phone.width;
  plan=red::PlanDeviceCommands(phone,rotated,.5,.5);
  Check(plan.metrics && !plan.touch && !plan.user_agent);
  auto desktop=phone;desktop.mobile=false;
  plan=red::PlanDeviceCommands(phone,desktop,.5,.5);
  Check(plan.metrics && plan.touch && plan.user_agent);
  auto mouse=phone;mouse.touch=false;
  plan=red::PlanDeviceCommands(phone,mouse,.5,.5);
  Check(!plan.metrics && plan.touch && !plan.user_agent);
  auto off=phone;off.enabled=false;
  plan=red::PlanDeviceCommands(phone,off,.5,1);
  Check(plan.metrics && plan.touch && plan.user_agent);
  desktop.enabled=false;
  plan=red::PlanDeviceCommands(off,desktop,1,1);
  Check(!plan.metrics && !plan.touch && !plan.user_agent);
}
