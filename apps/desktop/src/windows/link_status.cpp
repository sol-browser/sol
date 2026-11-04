#include "windows/embedded_content.h"
#include "windows/link_status_text.h"
#include "include/views/cef_display.h"

namespace red {
void EmbeddedContent::SetLinkStatus(CefRefPtr<CefBrowser> browser, const CefString& value) {
  if (!browser || !link_status_available_ || !browsers_.contains(browser->GetIdentifier())) return;
  const int id = browser->GetIdentifier();
  if (value.empty()) {
    if (id == link_status_browser_) HideLinkStatus();
    return;
  }
  const auto handle = browser->GetHost()->GetWindowHandle();
  if (!IsWindowVisible(handle) || !IsWindowEnabled(handle) || (HasLinkPeek() && !IsLinkPeek(id))) {
    if (id == link_status_browser_) HideLinkStatus();
    return;
  }
  // Bound native text/layout work for arbitrary website-provided link targets.
  // Do not truncate inside a possibly enormous username/password authority.
  const auto text = value.length() > 8192 ? L"Link address is too long" : LinkStatusText(value.ToWString());
  if (text.empty()) { if (id == link_status_browser_) HideLinkStatus(); return; }
  const bool changed = id != link_status_browser_ || text != link_status_text_;
  const HWND focus = GetFocus();
  link_status_browser_ = id; link_status_text_ = text;
  UpdateLinkStatus();
  if (changed) if (auto state = state_.lock(); state && state->probe) {
    auto data = CefDictionaryValue::Create(); const auto bounds = corners_.link_status_bounds();
    data->SetInt("browser_id", id); data->SetString("text", text);
    data->SetBool("painted", corners_.link_status_visible());
    data->SetInt("left", bounds.left); data->SetInt("top", bounds.top);
    data->SetInt("width", bounds.right - bounds.left); data->SetInt("height", bounds.bottom - bounds.top);
    data->SetBool("peek", IsLinkPeek(id)); data->SetBool("fullscreen", IsFullscreen());
    const auto area = corners_.link_status_area();
    data->SetInt("page_left", area.left); data->SetInt("page_top", area.top);
    data->SetInt("page_right", area.right); data->SetInt("page_bottom", area.bottom);
    data->SetDouble("scale", state->window->GetDisplay()->GetDeviceScaleFactor());
    data->SetString("page_handle", std::to_string(reinterpret_cast<uintptr_t>(handle)));
    data->SetBool("focus_preserved", focus == GetFocus());
    state->Record("link_status_shown", data);
  }
}
void EmbeddedContent::HideLinkStatus() {
  const bool visible = corners_.link_status_visible();
  link_status_browser_ = -1; link_status_text_.clear(); corners_.HideLinkStatus();
  if (visible) if (auto state = state_.lock(); state && state->probe) state->Record("link_status_hidden");
}
void EmbeddedContent::UpdateLinkStatus() {
  if (link_status_browser_ == -1) return;
  const auto state = state_.lock();
  const auto browser = browsers_.find(link_status_browser_);
  if (!state || !state->window || browser == browsers_.end() || sidebar_overlay_visible_ ||
      (HasLinkPeek() && !IsLinkPeek(link_status_browser_))) { HideLinkStatus(); return; }
  const HWND handle = browser->second->GetHost()->GetWindowHandle();
  const HWND root = state->window->GetWindowHandle();
  if (!IsWindowVisible(handle) || !IsWindowEnabled(handle) || IsIconic(root)) { HideLinkStatus(); return; }
  RECT area{}; POINT origin{};
  if (!GetClientRect(handle, &area) || !ClientToScreen(handle, &origin) || !ScreenToClient(root, &origin)) {
    HideLinkStatus(); return;
  }
  OffsetRect(&area, origin.x, origin.y);
  if (!corners_.ShowLinkStatus(root, area, state->window->GetDisplay()->GetDeviceScaleFactor(),
                              link_status_text_, state->theme)) HideLinkStatus();
}
}  // namespace red
