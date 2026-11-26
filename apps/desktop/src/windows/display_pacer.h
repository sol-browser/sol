#pragma once
#include <windows.h>
#include <dwmapi.h>

#include <atomic>
#include <functional>
#include <memory>
#include <thread>

#include "include/base/cef_callback.h"
#include "include/cef_task.h"
#include "include/wrapper/cef_closure_task.h"

namespace red {
// Runs |step| on the UI thread once per composed display frame: a worker waits
// for each composition (DwmFlush, i.e. every vsync at any refresh rate) and
// posts one step, never more than one in flight, so a busy UI thread does not
// queue them up. Delayed tasks (~15.6 ms timer granularity) skip frames on
// fast displays. Stops when |step| returns false, the returned flag is set, or
// the UI thread is gone.
inline std::shared_ptr<std::atomic<bool>> StartDisplayPacer(std::function<bool()> step) {
  struct Pacer { std::atomic<bool> pending{false}; std::shared_ptr<std::atomic<bool>> stop; std::function<bool()> step; };
  auto pacer = std::make_shared<Pacer>();
  pacer->stop = std::make_shared<std::atomic<bool>>(false);
  pacer->step = std::move(step);
  std::thread([pacer] {
    while (!*pacer->stop) {
      if (FAILED(DwmFlush())) Sleep(4);  // Composition off: still bounded.
      if (*pacer->stop || pacer->pending.exchange(true)) continue;
      if (!CefPostTask(TID_UI, base::BindOnce([](std::shared_ptr<Pacer> pacer) {
            pacer->pending = false;
            if (*pacer->stop) return;
            if (!pacer->step()) *pacer->stop = true;
          }, pacer)))
        *pacer->stop = true;  // UI thread gone (shutdown).
    }
  }).detach();
  return pacer->stop;
}
}  // namespace red
