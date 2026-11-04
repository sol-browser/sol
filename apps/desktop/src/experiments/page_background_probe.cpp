#include "experiments/page_background_probe.h"
#include "experiments/shell_capture.h"
#include "include/base/cef_callback.h"
#include "include/wrapper/cef_closure_task.h"

namespace red {
namespace {
void Sample(std::weak_ptr<State> weak, CefRefPtr<CefBrowser> page, const char* step) {
  const auto state = weak.lock();
  if (!state || !page->IsValid()) return;
  const HWND window = page->GetHost()->GetWindowHandle();
  RECT bounds{}; GetClientRect(window, &bounds);
  auto data = CefDictionaryValue::Create();
#if defined(RED_CEF_CONTENT_TRANSFORM)
  data->SetBool("transform_available", true);
#else
  data->SetBool("transform_available", false);
#endif
  data->SetString("step", step);
  data->SetBool("visible", IsWindowVisible(window) != FALSE);
  data->SetInt("width", bounds.right); data->SetInt("height", bounds.bottom);
  RECT screen{}; GetWindowRect(window,&screen);
  data->SetInt("left",screen.left); data->SetInt("top",screen.top);
#if defined(RED_CEF_PAGE_CONTAINER)
  data->SetBool("container_available",true);
#else
  data->SetBool("container_available",false);
#endif
  const auto output = state->evidence_directory / (std::string("page-background-") + step + ".png");
  // Browser compositor readback of this fixture only. This tests the page's
  // own alpha/color, not the DWM backdrop or arbitrary desktop pixels.
  const auto registration = CaptureShell(page, output);
  CefPostDelayedTask(TID_UI, base::BindOnce([](CefRefPtr<CefRegistration>) {}, registration), 600);
  state->Record("page_background_sample", data);
}
void Transform(CefRefPtr<CefBrowser> page, double scale) {
#if defined(RED_CEF_PAGE_CONTAINER)
  if (page->IsValid()) page->GetHost()->SetContentContainerTransform(scale, 0, 0, CefColorSetARGB(255,0,0,0));
#elif defined(RED_CEF_CONTENT_TRANSFORM)
  if (page->IsValid()) page->GetHost()->SetContentTransform(scale, 0, 0);
#endif
}
void Animate(CefRefPtr<CefBrowser> page, double scale) {
#if defined(RED_CEF_PAGE_CONTAINER)
  if (page->IsValid()) page->GetHost()->SetContentContainerTransform(scale,0,1200,CefColorSetARGB(255,0,0,0));
#endif
}
}
void ProbePageBackground(std::weak_ptr<State> weak, CefRefPtr<CefBrowser> page) {
  CefPostDelayedTask(TID_UI, base::BindOnce(&Sample, weak, page, "normal"), 1500);
  CefPostDelayedTask(TID_UI, base::BindOnce(&Transform, page, 0.9), 1800);
  CefPostDelayedTask(TID_UI, base::BindOnce(&Sample, weak, page, "scaled"), 2400);
  CefPostDelayedTask(TID_UI, base::BindOnce(&Transform, page, 1.0), 2700);
  CefPostDelayedTask(TID_UI, base::BindOnce(&Sample, weak, page, "restored"), 3300);
  CefPostDelayedTask(TID_UI, base::BindOnce([](std::weak_ptr<State> weak) {
    if (const auto state = weak.lock()) state->link_headers_done = true;
  }, weak), 3600);
}
void ProbePageContainerMotion(std::weak_ptr<State> weak, CefRefPtr<CefBrowser> page) {
  CefPostDelayedTask(TID_UI,base::BindOnce(&Sample,weak,page,"normal"),1500);
  CefPostDelayedTask(TID_UI,base::BindOnce(&Animate,page,.9),1800);
  CefPostDelayedTask(TID_UI,base::BindOnce(&Sample,weak,page,"motion-100"),1900);
  CefPostDelayedTask(TID_UI,base::BindOnce(&Sample,weak,page,"motion-400"),2200);
  CefPostDelayedTask(TID_UI,base::BindOnce(&Sample,weak,page,"motion-800"),2600);
  CefPostDelayedTask(TID_UI,base::BindOnce(&Sample,weak,page,"scaled"),3300);
  CefPostDelayedTask(TID_UI,base::BindOnce(&Animate,page,1.),3600);
  CefPostDelayedTask(TID_UI,base::BindOnce(&Sample,weak,page,"return-400"),4000);
  CefPostDelayedTask(TID_UI,base::BindOnce(&Sample,weak,page,"restored"),5100);
  CefPostDelayedTask(TID_UI,base::BindOnce([](std::weak_ptr<State> weak) {
    if (const auto state=weak.lock()) state->link_headers_done=true;
  },weak),5400);
}
}
