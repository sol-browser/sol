#include "windows/menu_popup.h"
#include "windows/menu_placement.h"
#include <algorithm>
#include <dwmapi.h>
#include "app/session.h"
#include "include/cef_parser.h"
#include "include/views/cef_display.h"
#include "include/views/cef_fill_layout.h"
#include "trust_policy.h"
#include "windows/focus.h"
#include "windows/ui_context_menu.h"
#include "windows/window_close.h"
#include "workspace/controller.h"
#include "app/theme.h"

namespace red {
namespace {
constexpr int kMinWidth = 180, kMaxWidth = 420, kMaxHeight = 640;
constexpr int kMenuClosingMs = 110;

class MenuClient final : public CefClient, public CefLifeSpanHandler, public CefRequestHandler, public CefKeyboardHandler, public CefDisplayHandler,
    public CefMessageRouterBrowserSide::Handler {
 public:
  MenuClient(std::weak_ptr<MenuPopup> owner, int level, std::weak_ptr<State> state) : owner_(owner), level_(level), state_(state) {
    router_ = CefMessageRouterBrowserSide::Create(CefMessageRouterConfig()); router_->AddHandler(this, false);
  }
  ~MenuClient() override { router_->RemoveHandler(this); }
  CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return this; }
  CefRefPtr<CefRequestHandler> GetRequestHandler() override { return this; }
  CefRefPtr<CefKeyboardHandler> GetKeyboardHandler() override { return this; }
  CefRefPtr<CefContextMenuHandler> GetContextMenuHandler() override { return new UiContextMenu(state_); }
  CefRefPtr<CefDisplayHandler> GetDisplayHandler() override { return this; }
  bool OnConsoleMessage(CefRefPtr<CefBrowser>, cef_log_severity_t, const CefString& message, const CefString&, int) override {
    // Probe evidence only: what the menu document logged (errors included).
    if (auto s = state_.lock(); s && s->probe) {
      auto details = CefDictionaryValue::Create(); details->SetInt("level", level_); details->SetString("message", message.ToString().substr(0, 300));
      s->Record("menu_console", details);
    }
    return false;
  }
  void OnAfterCreated(CefRefPtr<CefBrowser> b) override { if (auto o = owner_.lock()) o->Created(level_, b); }
  void OnBeforeClose(CefRefPtr<CefBrowser> b) override { router_->OnBeforeClose(b); if (auto o = owner_.lock()) o->Closed(level_, b->GetIdentifier()); }
  bool OnBeforePopup(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, int, const CefString&, const CefString&,
      cef_window_open_disposition_t, bool, const CefPopupFeatures&, CefWindowInfo&, CefRefPtr<CefClient>&,
      CefBrowserSettings&, CefRefPtr<CefDictionaryValue>&, bool*) override { return true; }
  bool OnBeforeBrowse(CefRefPtr<CefBrowser> b, CefRefPtr<CefFrame> f, CefRefPtr<CefRequest> r, bool, bool) override {
    router_->OnBeforeBrowse(b, f); return !f->IsMain() || r->GetURL().ToString() != kMenuUrl;
  }
  void OnRenderProcessTerminated(CefRefPtr<CefBrowser> b, TerminationStatus, int, const CefString&) override {
    router_->OnRenderProcessTerminated(b); if (auto o = owner_.lock()) o->Close(false);
  }
  bool OnPreKeyEvent(CefRefPtr<CefBrowser>, const CefKeyEvent& e, CefEventHandle, bool*) override {
    if (e.type == KEYEVENT_RAWKEYDOWN && e.windows_key_code == VK_ESCAPE) { if (auto o = owner_.lock()) o->Escape(level_); return true; }
    if (e.type == KEYEVENT_RAWKEYDOWN && e.windows_key_code == VK_F4 && (e.modifiers & EVENTFLAG_ALT_DOWN))
      if (auto s = state_.lock(); s && s->window) { s->window->Close(); return true; }
    return false;
  }
  bool OnProcessMessageReceived(CefRefPtr<CefBrowser> b, CefRefPtr<CefFrame> f, CefProcessId p, CefRefPtr<CefProcessMessage> m) override {
    return router_->OnProcessMessageReceived(b, f, p, m);
  }
  bool OnQuery(CefRefPtr<CefBrowser> b, CefRefPtr<CefFrame> f, int64_t, const CefString& r, bool persistent, CefRefPtr<Callback> cb) override {
    if (auto o = owner_.lock()) return o->Query(level_, b, f, r, persistent, cb);
    cb->Failure(410, "Menu closed"); return true;
  }
 private:
  std::weak_ptr<MenuPopup> owner_; int level_; std::weak_ptr<State> state_;
  CefRefPtr<CefMessageRouterBrowserSide> router_;
  IMPLEMENT_REFCOUNTING(MenuClient);
};
class MenuView final : public CefBrowserViewDelegate {
 public: cef_runtime_style_t GetBrowserRuntimeStyle() override { return CEF_RUNTIME_STYLE_ALLOY; }
 private: IMPLEMENT_REFCOUNTING(MenuView);
};
class MenuWindow final : public CefWindowDelegate {
 public:
  MenuWindow(std::weak_ptr<MenuPopup> owner, int level, CefRefPtr<CefBrowserView> view, cef_color_t background)
      : owner_(owner), level_(level), view_(view), background_(background) {}
  cef_runtime_style_t GetWindowRuntimeStyle() override { return CEF_RUNTIME_STYLE_ALLOY; }
  bool IsFrameless(CefRefPtr<CefWindow>) override { return true; }
  bool CanResize(CefRefPtr<CefWindow>) override { return false; }
  bool CanMaximize(CefRefPtr<CefWindow>) override { return false; }
  bool CanMinimize(CefRefPtr<CefWindow>) override { return false; }
  CefRefPtr<CefWindow> GetParentWindow(CefRefPtr<CefWindow>, bool* menu, bool*) override {
    *menu = false; if (auto o = owner_.lock()) return o->parent(); return nullptr;
  }
  CefSize GetPreferredSize(CefRefPtr<CefView>) override { return CefSize(240, 120); }
  void OnWindowCreated(CefRefPtr<CefWindow> w) override {
    w->SetTitle("Sol — Menu"); w->SetBackgroundColor(background_);
    const DWM_WINDOW_CORNER_PREFERENCE corners = DWMWCP_ROUND;
    DwmSetWindowAttribute(w->GetWindowHandle(), DWMWA_WINDOW_CORNER_PREFERENCE, &corners, sizeof(corners));
    const BOOL dark = TRUE;
    DwmSetWindowAttribute(w->GetWindowHandle(), DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
    w->SetToFillLayout(); w->AddChildView(view_);
    if (auto o = owner_.lock()) o->Attach(level_, w);
  }
  void OnWindowActivationChanged(CefRefPtr<CefWindow>, bool active) override { if (auto o = owner_.lock()) o->ActivationChanged(level_, active); }
  bool CanClose(CefRefPtr<CefWindow>) override {
    if (auto o = owner_.lock(); o && !o->shutting_down()) { o->Close(false); return false; } return true;
  }
  void OnWindowDestroyed(CefRefPtr<CefWindow>) override { view_ = nullptr; if (auto o = owner_.lock()) o->Destroyed(level_); }
 private:
  std::weak_ptr<MenuPopup> owner_; int level_; CefRefPtr<CefBrowserView> view_; cef_color_t background_;
  IMPLEMENT_REFCOUNTING(MenuWindow);
};
CefRefPtr<CefListValue> FindChildren(CefRefPtr<CefListValue> items, const std::string& id) {
  if (!items) return nullptr;
  for (size_t i = 0; i < items->GetSize(); ++i) {
    const auto item = items->GetDictionary(i);
    if (item && item->GetString("id") == id && item->GetType("children") == VTYPE_LIST && item->GetBool("enabled"))
      return item->GetList("children")->Copy();
  }
  return nullptr;
}
}  // namespace

MenuPopup::Builder::Builder() : actions_(std::make_shared<std::map<std::string, std::function<void()>>>()), next_(std::make_shared<int>(0)) {}
MenuPopup::Builder::Builder(std::shared_ptr<std::map<std::string, std::function<void()>>> actions, std::shared_ptr<int> next)
    : actions_(std::move(actions)), next_(std::move(next)) {}
void MenuPopup::Builder::Add(const std::string& label, const std::string& icon, std::function<void()> action, const std::string& shortcut,
                             bool enabled, bool checked, const std::string& image) {
  Entry entry; entry.id = "m" + std::to_string((*next_)++);
  entry.label = label; entry.icon = icon; entry.shortcut = shortcut; entry.enabled = enabled; entry.checked = checked; entry.image = image;
  if (enabled && action) (*actions_)[entry.id] = std::move(action);
  entries_.push_back(std::move(entry));
}
void MenuPopup::Builder::Separator() {
  if (entries_.empty() || entries_.back().separator) return;
  Entry entry; entry.separator = true; entries_.push_back(std::move(entry));
}
MenuPopup::Builder& MenuPopup::Builder::Submenu(const std::string& label, const std::string& icon, bool enabled) {
  Entry entry; entry.id = "m" + std::to_string((*next_)++); entry.label = label; entry.icon = icon; entry.enabled = enabled;
  entry.submenu.reset(new Builder(actions_, next_));
  entries_.push_back(std::move(entry));
  return *entries_.back().submenu;
}
CefRefPtr<CefListValue> MenuPopup::Builder::items() const {
  auto list = CefListValue::Create();
  size_t end = entries_.size();
  while (end && entries_[end - 1].separator) --end;  // No trailing separator.
  for (size_t i = 0; i < end; ++i) {
    const auto& entry = entries_[i];
    auto item = CefDictionaryValue::Create();
    if (entry.separator) item->SetBool("separator", true);
    else {
      item->SetString("id", entry.id); item->SetString("label", entry.label); item->SetBool("enabled", entry.enabled);
      if (!entry.icon.empty()) item->SetString("icon", entry.icon);
      if (!entry.image.empty()) item->SetString("image", entry.image);
      if (!entry.shortcut.empty()) item->SetString("shortcut", entry.shortcut);
      if (entry.checked) item->SetBool("checked", true);
      if (entry.submenu) {
        // SetList takes the list over (the reference is invalid afterwards): count first.
        const auto children = entry.submenu->items();
        if (!children->GetSize()) item->SetBool("enabled", false);
        item->SetList("children", children);
      }
    }
    list->SetDictionary(list->GetSize(), item);
  }
  return list;
}

CefRefPtr<CefWindow> MenuPopup::parent() const { const auto s = state_.lock(); return s ? s->window : nullptr; }

void MenuPopup::Open(Builder& builder, const CefPoint& at, const std::string& anchor_key, std::function<void()> dismissed,
                     CefRefPtr<CefWindow> return_to) {
  const auto s = state_.lock();
  if (!s || !s->window || !s->workspace || s->workspace->WindowClosing() || shutdown_) return;
  if (open_ && anchor_key == anchor_key_) { Close(true); return; }
  // The press on the opener that just dismissed this menu (by activating the window) is not a new opening.
  if (!open_ && anchor_key == closed_key_ && GetTickCount64() - closed_ms_ < 350) { closed_key_.clear(); return; }
  if (open_) Close(false);  // Another menu replaces this one: it was dismissed.
  CloseFrom(0, false);
  dismissed_ = std::move(dismissed); return_to_ = return_to;
  actions_ = builder.actions();
  anchor_key_ = anchor_key; open_ = true;
  // Kept windows were coloured with the theme of their creation: follow the current one.
  const auto themed = kPopupSurface;
  for (auto& level : levels_) if (level.window) level.window->SetBackgroundColor(CefColorSetARGB(255, themed.r, themed.g, themed.b));
  auto& root = levels_[0];
  root.items = builder.items(); root.at = at; root.source_left = at.x; root.focus = true;
  ++root.request; root.open = true; root.prepared = root.presented = false;
  Ensure(0); Publish(0);
  s->Record("menu_opened");
}

void MenuPopup::Ensure(int level) {
  auto& l = levels_[level];
  const auto s = state_.lock();
  if (l.window || l.creating || !s) return;
  const auto shell = s->browsers.find(s->shell_id);
  if (shell == s->browsers.end()) return;
  l.creating = true;
  CefBrowserSettings settings;
  const auto background = kPopupSurface;
  settings.background_color = CefColorSetARGB(255, background.r, background.g, background.b);
  auto extra = CefDictionaryValue::Create(); extra->SetBool("trusted_ui", true);
  auto view = CefBrowserView::CreateBrowserView(new MenuClient(weak_from_this(), level, state_), std::string(kMenuUrl), settings, extra,
      shell->second->GetHost()->GetRequestContext(), new MenuView());
  view->SetBackgroundColor(settings.background_color);
  CefWindow::CreateTopLevelWindow(new MenuWindow(weak_from_this(), level, view, settings.background_color));
}

void MenuPopup::Attach(int level, CefRefPtr<CefWindow> window) {
  auto& l = levels_[level];
  l.window = window; l.presentation.Attach(window, state_);
}
void MenuPopup::Created(int level, CefRefPtr<CefBrowser> browser) {
  auto& l = levels_[level];
  l.browser = browser; l.creating = false;
  if (const auto s = state_.lock()) {
    s->menu_id[level] = browser->GetIdentifier(); RegisterSessionUi(s, browser->GetIdentifier());
    s->browsers[browser->GetIdentifier()] = browser;
  }
}
void MenuPopup::Closed(int level, int id) {
  auto& l = levels_[level];
  l.observer = nullptr; l.browser = nullptr; l.creating = false;
  if (const auto s = state_.lock()) {
    UnregisterSessionUi(s, id); s->browsers.erase(id); s->menu_id[level] = -1;
    if (s->window_close) s->window_close->OnClosed(id);
    if (!s->window && s->browsers.empty()) QuitIfLastWindow(s);
  }
}
void MenuPopup::Destroyed(int level) {
  auto& l = levels_[level];
  l.presentation.Reset(); l.window = nullptr; l.open = l.prepared = l.presented = false;
}

std::string MenuPopup::Response(int level) const {
  const auto& l = levels_[level];
  auto d = CefDictionaryValue::Create();
  d->SetInt("request", l.request); d->SetInt("level", level); d->SetBool("open", l.open);
  d->SetList("items", l.open && l.items ? l.items->Copy() : CefListValue::Create());
  // The current theme: the menu document is kept between openings and only read theme.css once.
  if (const auto s = state_.lock()) {
    auto theme = CefDictionaryValue::Create();
    theme->SetInt("hue", s->theme.hue); theme->SetInt("intensity", s->theme.intensity); theme->SetInt("noise", s->theme.noise);
    theme->SetString("mode", s->theme.mode == ThemeMode::Dark ? "dark" : s->theme.mode == ThemeMode::System ? "system" : "light");
    theme->SetBool("dark", s->theme_dark); d->SetDictionary("theme", theme);
  }
  auto value = CefValue::Create(); value->SetDictionary(d);
  return CefWriteJSON(value, JSON_WRITER_DEFAULT);
}
void MenuPopup::Publish(int level) { if (auto& l = levels_[level]; l.observer) l.observer->Success(Response(level)); }

void MenuPopup::Place(int level, int width, int height) {
  auto& l = levels_[level];
  if (!l.window) return;
  width = std::clamp(width, kMinWidth, kMaxWidth); height = std::clamp(height, 40, kMaxHeight);
  // A flyout belongs to its parent's monitor, even when its preferred right
  // edge crosses into the neighboring display.
  auto display_point=l.at;
  if(level>0 && levels_[level-1].window) {
    const auto parent_bounds=levels_[level-1].window->GetBounds();
    display_point=CefPoint(parent_bounds.x+parent_bounds.width/2,parent_bounds.y+parent_bounds.height/2);
  }
  const auto display = CefDisplay::GetDisplayNearestPoint(display_point, false);
  const auto work = display ? display->GetWorkArea() : CefRect(l.at.x, l.at.y, width, height);
  const auto placed=PlaceMenu({work.x,work.y,work.width,work.height},l.at.x,l.at.y,width,height,level>0,l.source_left);
  const CefRect target(placed.x,placed.y,placed.width,placed.height);
  l.opens_up=target.y<l.at.y;
  l.origin_x=level>0 ? (target.x<l.source_left ? 1.f : 0.f) :
    std::clamp(static_cast<float>(l.at.x-target.x)/target.width,0.f,1.f);
  if (l.window->GetBounds() != target) { if (l.presented) l.presentation.Settle(); l.window->SetBounds(target); }
  if(const auto s=state_.lock();s && s->menu_probe) {
    auto d=CefDictionaryValue::Create();d->SetString("anchor",anchor_key_);d->SetInt("level",level);
    d->SetInt("atX",l.at.x);d->SetInt("atY",l.at.y);d->SetInt("parentLeft",l.source_left);
    const auto actual=l.window->GetBounds();
    d->SetInt("x",actual.x);d->SetInt("y",actual.y);d->SetInt("width",actual.width);d->SetInt("height",actual.height);
    d->SetInt("workX",work.x);d->SetInt("workY",work.y);d->SetInt("workWidth",work.width);d->SetInt("workHeight",work.height);
    s->Record("menu_placement",d);
  }
}

void MenuPopup::CloseFrom(int level, bool animate) {
  for (int i = kLevels - 1; i >= level; --i) {
    auto& l = levels_[i];
    if (!l.open && !l.presented) continue;
    const bool presented = l.presented;
    l.open = l.prepared = l.presented = false; ++l.request;
    // Menus fade out quickly by themselves (a popover's 280 ms shrink felt slow for a menu).
    if (!(animate && presented && l.presentation.Dismiss(false, 0.f, kMenuClosingMs))) l.presentation.Conceal();
    Publish(i);
  }
}

void MenuPopup::Close(bool animate) {
  if (!open_) return;
  open_ = false; closed_ms_ = GetTickCount64(); closed_key_ = anchor_key_; anchor_key_.clear();
  // Give activation back to the browser window as a native menu does, so what runs next (an inline
  // rename, a focused field) starts in an active window; a click elsewhere already moved it.
  bool focused = false;
  for (const auto& l : levels_) if (l.open && l.window && l.window->IsActive()) focused = true;
  CloseFrom(0, animate);
  if (const auto s = state_.lock(); focused && s) {
    const auto target = return_to_ && return_to_->IsVisible() ? return_to_ : s->window;
    if (target) target->Activate();
  }
  return_to_ = nullptr;
  if (const auto s = state_.lock()) s->Record("menu_closed");
  if (auto dismissed = std::move(dismissed_)) { dismissed_ = nullptr; dismissed(); }
}

void MenuPopup::Shutdown() {
  shutdown_ = true; Close(false);
  for (auto& l : levels_) { l.observer = nullptr; if (l.window) l.window->Close(); }
}

void MenuPopup::ProbeRun(int level, const std::string& script) {
  const auto s = state_.lock();
  if (s && s->probe && level >= 0 && level < kLevels && levels_[level].browser)
    levels_[level].browser->GetMainFrame()->ExecuteJavaScript(script, std::string(kMenuUrl), 0);
}

void MenuPopup::Escape(int level) {
  if (level == 0) {
    Close(true);
    if (const auto s = state_.lock(); s && s->window) s->window->Activate();
    return;
  }
  CloseFrom(level, false);
  auto& parent_level = levels_[level - 1];
  if (parent_level.window) parent_level.window->Activate();
  if (parent_level.browser) parent_level.browser->GetHost()->SetFocus(true);
}

void MenuPopup::ActivationChanged(int, bool active) {
  if (active || !open_ || checking_) return;
  // Activation moves between the menu's own windows when a submenu takes focus; decide once it settles.
  checking_ = true;
  CefPostTask(TID_UI, base::BindOnce([](std::weak_ptr<MenuPopup> weak) { if (auto self = weak.lock()) self->CheckFocus(); }, weak_from_this()));
}
void MenuPopup::CheckFocus() {
  checking_ = false;
  if (!open_) return;
  for (const auto& l : levels_) if (l.open && l.window && l.window->IsActive()) return;
  Close(true);
}

bool MenuPopup::Query(int level, CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, const CefString& raw, bool persistent,
                      CefRefPtr<CefMessageRouterBrowserSide::Callback> cb) {
  const auto s = state_.lock();
  if (!s || level < 0 || level >= kLevels || !IsMenuUi(browser->GetIdentifier(), s->menu_id[level], frame->IsMain(), frame->GetURL().ToString())) {
    cb->Failure(403, "Untrusted menu document"); return true;
  }
  if (raw.length() > 1024) { cb->Failure(400, "Invalid menu request"); return true; }
  const auto value = CefParseJSON(raw, JSON_PARSER_RFC);
  if (!value || value->GetType() != VTYPE_DICTIONARY || value->GetDictionary()->GetType("method") != VTYPE_STRING) { cb->Failure(400, "Invalid menu request"); return true; }
  const auto d = value->GetDictionary(); const auto method = d->GetString("method").ToString();
  if (HandleUiFocus(s, browser, method, d->GetSize(), persistent, cb)) return true;
  if (s->probe) { auto details = CefDictionaryValue::Create(); details->SetInt("level", level); details->SetString("method", method); s->Record("menu_query", details); }
  auto& l = levels_[level];
  if (s->probe && method.starts_with("menu.probe:") && l.browser) {
    // Probe-only input: "menu.probe:<move|click>:<x>:<y>" in this document's CSS pixels.
    const auto rest = method.substr(11); const auto a = rest.find(':'), b = rest.find(':', a + 1);
    if (a != std::string::npos && b != std::string::npos) {
      const auto kind = rest.substr(0, a); const int x = std::atoi(rest.substr(a + 1, b - a - 1).c_str()), y = std::atoi(rest.substr(b + 1).c_str());
      std::vector<std::string> types{"mouseMoved"};
      if (kind == "click") { types.push_back("mousePressed"); types.push_back("mouseReleased"); }
      for (const auto& type : types) {
        auto input = CefDictionaryValue::Create(); input->SetString("type", type); input->SetDouble("x", x); input->SetDouble("y", y);
        if (type != "mouseMoved") { input->SetString("button", "left"); input->SetInt("clickCount", 1); }
        l.browser->GetHost()->ExecuteDevToolsMethod(0, "Input.dispatchMouseEvent", input);
      }
    }
    cb->Success("{}"); return true;
  }
  if (method == "menu.observe" && persistent && d->GetSize() == 1) {
    l.observer = cb; Publish(level); return true;
  }
  if (persistent) { cb->Failure(400, "Invalid menu request"); return true; }
  // Everything else belongs to the opening the document is showing.
  if (d->GetType("request") != VTYPE_INT || d->GetInt("request") != l.request || !l.open || !l.window) { cb->Failure(409, "Menu changed"); return true; }
  if (method == "menu.resize" && d->GetSize() == 4 && d->GetType("width") == VTYPE_INT && d->GetType("height") == VTYPE_INT) {
    Place(level, d->GetInt("width"), d->GetInt("height"));
    l.prepared = true; if (!l.presented) l.presentation.Stage();
    // A submenu's staging shows its window; give activation back to the menu row that opened it.
    if (level > 0 && !l.focus && levels_[level - 1].window) levels_[level - 1].window->Activate();
    cb->Success(PopupViewport(l.window, l.request)); return true;
  }
  if (method == "menu.present" && d->GetSize() == 3 && d->GetType("reducedMotion") == VTYPE_BOOL && l.prepared) {
    if (!l.presented) {
      l.presented = true;
      l.presentation.Reveal(d->GetBool("reducedMotion"), l.opens_up, l.origin_x, !return_to_);
      auto details = CefDictionaryValue::Create(); details->SetInt("level", level); s->Record("menu_presented", details);
      if (level == 0 || l.focus) { l.window->Activate(); if (l.browser) l.browser->GetHost()->SetFocus(true); }
      else if (levels_[level - 1].window) levels_[level - 1].window->Activate();
    }
    cb->Success(PopupViewport(l.window, l.request, l.presentation.composed(), l.origin_x, l.opens_up)); return true;
  }
  if (method == "menu.open" && d->GetSize() == 5 && d->GetType("id") == VTYPE_STRING && d->GetType("top") == VTYPE_INT &&
      d->GetType("focus") == VTYPE_BOOL && level + 1 < kLevels) {
    const auto children = FindChildren(l.items, d->GetString("id"));
    if (!children) { cb->Failure(404, "No submenu"); return true; }
    CloseFrom(level + 1, false);
    auto& sub = levels_[level + 1];
    const auto bounds = l.window->GetBounds();
    const int top = std::clamp(d->GetInt("top"), 0, bounds.height);
    sub.items = children; sub.at = CefPoint(bounds.x + bounds.width + 2, bounds.y + top - 6); sub.source_left = bounds.x;
    sub.focus = d->GetBool("focus"); ++sub.request; sub.open = true; sub.prepared = sub.presented = false;
    Ensure(level + 1); Publish(level + 1);
    cb->Success("{}"); return true;
  }
  if (method == "menu.closeSub" && d->GetSize() == 3 && d->GetType("focus") == VTYPE_BOOL) {
    CloseFrom(level + 1, false);
    if (d->GetBool("focus")) { l.window->Activate(); if (l.browser) l.browser->GetHost()->SetFocus(true); }
    cb->Success("{}"); return true;
  }
  if (method == "menu.choose" && d->GetSize() == 3 && d->GetType("id") == VTYPE_STRING) {
    const auto found = actions_.find(d->GetString("id"));
    if (found == actions_.end()) { cb->Failure(404, "No such menu item"); return true; }
    auto action = found->second;
    cb->Success("{}");
    dismissed_ = nullptr;  // Chosen, not dismissed.
    Close(true);
    // Run after the menu has let go of focus, as a native menu's command would.
    CefPostTask(TID_UI, base::BindOnce([](std::weak_ptr<State> weak, std::function<void()> run) {
      run(); if (const auto s = weak.lock()) s->Record("menu_action_ran");
    }, state_, std::move(action)));
    s->Record("menu_chosen");
    return true;
  }
  if (method == "menu.close" && d->GetSize() == 2) { cb->Success("{}"); Escape(level); return true; }
  cb->Failure(400, "Unsupported menu command"); return true;
}
}  // namespace red
