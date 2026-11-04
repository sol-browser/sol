#pragma once
#include <array>
#include <functional>
#include <map>
#include <string>
#include "app/state.h"
#include "include/views/cef_browser_view.h"
#include "include/wrapper/cef_message_router.h"
#include "windows/popup_presentation.h"
namespace red {
// Red's own menus (sol://ui/menu.html): icons, shortcuts, an opening animation and submenus that
// open beside their row, as in Arc. Each level is an owned frameless Alloy window, kept warm after
// its first use. Native code owns the items and their actions; a menu document only reports
// geometry, which row opens a submenu and which row was chosen (by the id native gave it).
class MenuPopup : public std::enable_shared_from_this<MenuPopup> {
 public:
  static constexpr int kLevels = 3;
  // Item: {id, label, icon?, image?, shortcut?, enabled, checked?, children?} or {separator: true}.
  class Builder {
   public:
    Builder();
    // A fresh list each time (a CefListValue added to another value is copied or moved, so the
    // tree is only made once it is complete).
    CefRefPtr<CefListValue> items() const;
    // Adds a row running |action|; returns its id.
    void Add(const std::string& label, const std::string& icon, std::function<void()> action, const std::string& shortcut = "",
             bool enabled = true, bool checked = false, const std::string& image = "");
    void Separator();
    // Rows added to the returned builder form the submenu.
    Builder& Submenu(const std::string& label, const std::string& icon, bool enabled = true);
    std::map<std::string, std::function<void()>>& actions() { return *actions_; }
   private:
    Builder(std::shared_ptr<std::map<std::string, std::function<void()>>> actions, std::shared_ptr<int> next);
    struct Entry {
      std::string id, label, icon, shortcut, image;
      bool enabled = true, checked = false, separator = false;
      std::unique_ptr<Builder> submenu;
    };
    std::vector<Entry> entries_;
    std::shared_ptr<std::map<std::string, std::function<void()>>> actions_;
    std::shared_ptr<int> next_;
  };

  explicit MenuPopup(std::weak_ptr<State> state) : state_(std::move(state)) {}
  // Opens |builder|'s menu at |at| (screen DIP), flipping up/left at work-area edges; an open menu from the same
  // anchor closes instead (the opener toggles it). |dismissed| runs once if the menu closes without
  // a choice (including when another menu replaces it). |return_to| gets activation back when the
  // menu closes while it has it (default: the browser window), e.g. the popover that opened it.
  void Open(Builder& builder, const CefPoint& at, const std::string& anchor_key, std::function<void()> dismissed = nullptr,
            CefRefPtr<CefWindow> return_to = nullptr);
  void Close(bool animate = true);
  void Shutdown();
  bool is_open() const { return open_; }
  // Window/browser lifetime (from the level's delegates).
  void Attach(int level, CefRefPtr<CefWindow> window);
  void Created(int level, CefRefPtr<CefBrowser> browser);
  void Closed(int level, int id);
  void Destroyed(int level);
  void ActivationChanged(int level, bool active);
  CefRefPtr<CefWindow> parent() const;
  bool shutting_down() const { return shutdown_; }
  bool Query(int level, CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, const CefString& request, bool persistent,
             CefRefPtr<CefMessageRouterBrowserSide::Callback> callback);
  void Escape(int level);
  // Probe-only: runs |script| in the document of |level|.
  void ProbeRun(int level, const std::string& script);
 private:
  struct Level {
    CefRefPtr<CefWindow> window;
    CefRefPtr<CefBrowser> browser;
    CefRefPtr<CefMessageRouterBrowserSide::Callback> observer;
    CefRefPtr<CefListValue> items;
    PopupPresentation presentation;
    CefPoint at;          // Top-left wanted (screen DIP).
    int request = 0;
    int source_left = 0;  // Parent's left edge, to flip a submenu that does not fit on the right.
    bool opens_up = false;
    float origin_x = 0;
    bool creating = false, open = false, prepared = false, presented = false, focus = false;
  };
  void Ensure(int level);
  void Publish(int level);
  void Place(int level, int width, int height);
  void CloseFrom(int level, bool animate);
  void CheckFocus();
  std::string Response(int level) const;
  std::weak_ptr<State> state_;
  std::array<Level, kLevels> levels_;
  std::map<std::string, std::function<void()>> actions_;
  std::function<void()> dismissed_;
  CefRefPtr<CefWindow> return_to_;
  std::string anchor_key_;
  unsigned long long closed_ms_ = 0;
  std::string closed_key_;
  bool open_ = false, shutdown_ = false, checking_ = false;
};
}  // namespace red
