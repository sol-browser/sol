#pragma once
#include <algorithm>
#include <cmath>
#include <memory>
#include <thread>
#include <windows.h>
#include <dwmapi.h>
#include "windows/embedded_content.h"
#include "windows/motion.h"
#include "include/views/cef_window.h"
#include "include/cef_task.h"
#include "include/cef_parser.h"
#include "include/base/cef_callback.h"
#include "include/wrapper/cef_closure_task.h"

namespace red {
// Only our owned Alloy UI windows. Keep the real viewport at its final size;
// Windows fades the entire surface without resizing Chromium per frame.
class PopupPresentation {
 public:
  void Attach(CefRefPtr<CefWindow> window, std::weak_ptr<State> state) {
    Reset(); motion_ = std::make_shared<Motion>(); motion_->window = window;
    motion_->state = std::move(state);
    const auto handle = window->GetWindowHandle();
    motion_->handle = handle;
    const auto style = GetWindowLongPtr(handle, GWL_EXSTYLE);
    if (!(GetClassLongPtr(handle, GCL_STYLE) & (CS_OWNDC | CS_CLASSDC))) {
      SetLastError(0);
      const bool set = (style & WS_EX_LAYERED) || SetWindowLongPtr(handle, GWL_EXSTYLE, style | WS_EX_LAYERED) || !GetLastError();
      motion_->layered = set && SetLayeredWindowAttributes(handle, 0, 0, LWA_ALPHA);
      if (!motion_->layered && !(style & WS_EX_LAYERED)) SetWindowLongPtr(handle, GWL_EXSTYLE, style);
    }
  }
  void Reset() { Settle(); if (motion_) motion_->window = nullptr; motion_.reset(); }
  void Settle() {
    if (!motion_) return;
    if (motion_->closing) { Conceal(); return; }
    ++motion_->generation; Uncloak(*motion_); ClearProxy(*motion_);
    if (motion_->layered && IsWindow(motion_->handle)) SetLayeredWindowAttributes(motion_->handle, 0, 255, LWA_ALPHA);
  }
  void Conceal() {
    if (!motion_ || !motion_->window) return;
    motion_->closing = false;
    ++motion_->generation; motion_->window->Hide();
    Settle();
    if (motion_->layered) SetLayeredWindowAttributes(motion_->window->GetWindowHandle(), 0, 0, LWA_ALPHA);
    SetOwner(*motion_, false);
  }
  // |fade_ms| > 0: close by the window's own alpha over that time (menus: quicker than a popover).
  bool Dismiss(bool bottom, float origin_x, int fade_ms = 0) {
    if (!motion_ || !motion_->window || !motion_->layered || motion_->reduced || !NativeMotionEnabled()) {
      Conceal(); return false;
    }
    auto& m = *motion_;
    const auto state = m.state.lock();
    if (!state || !state->embedded) { Conceal(); return false; }
    if (!m.composable || fade_ms > 0) {
      // Over another popup the composed proxy would draw below it: fade the window itself.
      const auto generation = ++m.generation;
      m.closing = true; m.started = GetTickCount64(); m.fade_ms = fade_ms > 0 ? fade_ms : kPopupClosingMs;
      ClearProxy(m); Uncloak(m);
      SetLayeredWindowAttributes(m.handle, 0, 255, LWA_ALPHA);
      FadeOut(motion_, generation);
      return true;
    }
    const double t = std::min(1.0, (GetTickCount64() - m.growth_started) / static_cast<double>(kPopupOpeningMs));
    const float from = m.composed ? static_cast<float>(1 - std::pow(1 - t, 3)) : 1.f;
    const auto generation = ++m.generation;
    SetLayeredWindowAttributes(m.handle, 0, 255, LWA_ALPHA);
    // Keep the real window visible while creating/presenting its replacement.
    // Cloaking first left a blank frame before the exit proxy appeared above
    // the floating sidebar. The close cubic starts almost stationary, so one
    // composed frame of overlap does not skip visible motion.
    if (!state->embedded->BeginPopupAnimation(m.handle, bottom, origin_x, true, from)) { Conceal(); return false; }
    m.composed = m.closing = true; m.started = GetTickCount64();
    std::thread([weak = std::weak_ptr<Motion>(motion_), generation] {
      const bool presented = SUCCEEDED(DwmFlush());
      CefPostTask(TID_UI, base::BindOnce(&PopupPresentation::ContinueDismiss, weak, generation, presented));
    }).detach();
    return true;
  }
  void Stage() {
    if (!motion_ || !motion_->window || !motion_->layered) return;
    // Notify CEF Views too: ShowWindow alone leaves Chromium's widget hidden
    // and throttles its renderer timers/rAF, delaying the reveal by ~a second.
    // Nonzero alpha keeps composition active without exposing the old frame.
    const auto handle = motion_->window->GetWindowHandle();
    SetOwner(*motion_, true);
    // Keep the full-size native host out of the opening scale animation.
    const BOOL cloak = TRUE;
    if (SUCCEEDED(DwmSetWindowAttribute(handle, DWMWA_CLOAK, &cloak, sizeof(cloak)))) motion_->cloaked = true;
    SetLayeredWindowAttributes(handle, 0, 1, LWA_ALPHA);
    motion_->window->Show();
  }
  // |composable|: false when the window opens over another owned popup; the composed proxy is drawn
  // in the browser window's tree, below that popup, so such a window fades in on its own instead.
  void Reveal(bool reduced_motion, bool bottom, float origin_x = .5f, bool composable = true) {
    if (!motion_ || !motion_->window) return;
    auto& m = *motion_; const auto generation = ++m.generation;
    SetOwner(m, true);
    m.reduced = reduced_motion; m.closing = false; m.growth_started = GetTickCount64(); m.composable = composable;
    if (m.layered) SetLayeredWindowAttributes(m.window->GetWindowHandle(), 0, reduced_motion ? 255 : 1, LWA_ALPHA);
    m.window->Show();
    if (!m.layered || reduced_motion || !NativeMotionEnabled()) {
      Uncloak(m);
      if (m.layered) SetLayeredWindowAttributes(m.handle, 0, 255, LWA_ALPHA);
      return;
    }
    const auto state = m.state.lock();
    const BOOL cloak = TRUE;
    if (composable && state && state->embedded && SUCCEEDED(DwmSetWindowAttribute(m.handle, DWMWA_CLOAK, &cloak, sizeof(cloak)))) {
      m.cloaked = true; SetLayeredWindowAttributes(m.handle, 0, 255, LWA_ALPHA);
      if (state->embedded->BeginPopupAnimation(m.handle, bottom, origin_x)) {
        m.composed = true;
        CefPostDelayedTask(TID_UI, base::BindOnce(&PopupPresentation::FinishProxy,
            std::weak_ptr<Motion>(motion_), generation), kPopupOpeningMs + 16);
        return;
      }
      Uncloak(m); SetLayeredWindowAttributes(m.handle, 0, 1, LWA_ALPHA);
    }
    Uncloak(m);  // Staged cloaked; this path fades the real window itself.
    m.started = GetTickCount64(); Tick(motion_, generation);
  }
  bool layered() const { return motion_ && motion_->layered; }
  bool composed() const { return motion_ && motion_->composed; }
  bool closing() const { return motion_ && motion_->closing; }
 private:
  struct Motion {
    CefRefPtr<CefWindow> window;
    std::weak_ptr<State> state;
    HWND handle = nullptr;
    unsigned long long generation = 0, started = 0, growth_started = 0;
    bool layered = false, composed = false, cloaked = false, closing = false, reduced = false;
    bool composable = true;  // False over another popup: opens and closes by its own alpha.
    int fade_ms = kPopupClosingMs;
  };
  static void SetOwner(Motion& motion, bool allow_sidebar) {
    if (const auto state = motion.state.lock(); state && state->embedded)
      state->embedded->ConfigurePopupOwner(motion.handle, allow_sidebar);
  }
  static void Uncloak(Motion& motion) {
    if (!motion.cloaked) return;
    const BOOL cloak = FALSE;
    DwmSetWindowAttribute(motion.handle, DWMWA_CLOAK, &cloak, sizeof(cloak)); motion.cloaked = false;
  }
  static void ClearProxy(Motion& motion) {
    if (!motion.composed) return;
    if (const auto state = motion.state.lock(); state && state->embedded) state->embedded->EndPopupAnimation(motion.handle);
    motion.composed = false;
  }
  static void ContinueDismiss(std::weak_ptr<Motion> weak, unsigned long long generation, bool presented) {
    const auto m = weak.lock();
    if (!m || !m->window || m->generation != generation || !m->closing) return;
    const BOOL cloak = TRUE;
    if (!presented || FAILED(DwmSetWindowAttribute(m->handle, DWMWA_CLOAK, &cloak, sizeof(cloak)))) {
      FinishDismiss(weak, generation); return;
    }
    m->cloaked = true;
    const auto elapsed = GetTickCount64() - m->started;
    const auto duration = static_cast<unsigned long long>(kPopupClosingMs + 16);
    const int remaining = elapsed < duration ? static_cast<int>(duration - elapsed) : 0;
    if (const auto state = m->state.lock()) {
      auto details = CefDictionaryValue::Create(); details->SetInt("overlap_ms", static_cast<int>(std::min(elapsed, duration)));
      state->Record("popup_exit_proxy_ready", details);
    }
    CefPostDelayedTask(TID_UI, base::BindOnce(&PopupPresentation::FinishDismiss, weak, generation), remaining);
  }
  static void FadeOut(std::weak_ptr<Motion> weak, unsigned long long generation) {
    const auto m = weak.lock();
    if (!m || !m->window || m->generation != generation || !m->closing) return;
    const double t = std::min(1.0, (GetTickCount64() - m->started) / static_cast<double>(m->fade_ms));
    if (t >= 1) { FinishDismiss(weak, generation); return; }
    SetLayeredWindowAttributes(m->handle, 0, static_cast<BYTE>(255 * (1 - t) * (1 - t)), LWA_ALPHA);
    CefPostDelayedTask(TID_UI, base::BindOnce(&PopupPresentation::FadeOut, weak, generation), 16);
  }
  static void FinishDismiss(std::weak_ptr<Motion> weak, unsigned long long generation) {
    const auto m = weak.lock();
    if (!m || !m->window || m->generation != generation || !m->closing) return;
    // Hide before clearing the cloak: never expose the full-size native host
    // after its composed version has shrunk to zero.
    m->window->Hide(); m->closing = false;
    ClearProxy(*m); Uncloak(*m);
    SetLayeredWindowAttributes(m->handle, 0, 0, LWA_ALPHA);
    SetOwner(*m, false);
  }
  static void FinishProxy(std::weak_ptr<Motion> weak, unsigned long long generation) {
    const auto m = weak.lock();
    if (!m || !m->window || m->generation != generation) return;
    // Keep the composed card visible while its native host (including DWM's
    // rounded contour/shadow) fades in. Never change its native corner policy:
    // region masks do not reliably clip Chromium's GPU surface on this build.
    SetLayeredWindowAttributes(m->handle, 0, 1, LWA_ALPHA);
    if (const auto state = m->state.lock(); state && state->embedded)
      state->embedded->FadePopupShadow(m->handle, kHandoffMs);
    Uncloak(*m);
    m->started = GetTickCount64(); Tick(weak, generation);
  }
  static void Tick(std::weak_ptr<Motion> weak, unsigned long long generation) {
    const auto m = weak.lock();
    if (!m || !m->window || m->generation != generation) return;
    const double t = std::min(1.0, (GetTickCount64() - m->started) / static_cast<double>(kHandoffMs));
    const auto alpha = static_cast<BYTE>(std::lround(1 + 254 * (1 - std::pow(1 - t, 3))));
    SetLayeredWindowAttributes(m->window->GetWindowHandle(), 0, alpha, LWA_ALPHA);
    if (t < 1) CefPostDelayedTask(TID_UI, base::BindOnce(&PopupPresentation::Tick, weak, generation), 16);
    else if (m->composed) {
      // Give DWM a frame with the host fully opaque before dropping the proxy.
      CefPostDelayedTask(TID_UI, base::BindOnce(&PopupPresentation::ClearAfterFade, weak, generation), 16);
    }
  }
  static void ClearAfterFade(std::weak_ptr<Motion> weak, unsigned long long generation) {
    const auto m = weak.lock();
    if (!m || !m->window || m->generation != generation) return;
    ClearProxy(*m);
  }
  static constexpr int kHandoffMs = 200;
  std::shared_ptr<Motion> motion_;
};

inline bool PopupOwnerIsForeground(CefRefPtr<CefWindow> owner) {
  if (!owner || owner->IsMinimized()) return false;
  for (auto handle = GetAncestor(GetForegroundWindow(), GA_ROOT); handle; handle = GetWindow(handle, GW_OWNER))
    if (handle == owner->GetWindowHandle()) return true;
  return false;
}
inline std::string PopupViewport(CefRefPtr<CefWindow> window, int request, bool composed = false,
                                 float origin_x = .5f, bool bottom = false) {
  const auto bounds = window->GetClientAreaBoundsInScreen();
  auto data = CefDictionaryValue::Create(); data->SetInt("request", request);
  data->SetInt("width", bounds.width); data->SetInt("height", bounds.height);
  data->SetBool("composed", composed);
  data->SetBool("motion", NativeMotionEnabled());
  data->SetDouble("originX", origin_x); data->SetBool("bottom", bottom);
  auto value = CefValue::Create(); value->SetDictionary(data);
  return CefWriteJSON(value, JSON_WRITER_DEFAULT);
}
}  // namespace red
