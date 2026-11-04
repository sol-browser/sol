#pragma once
#include "app/state.h"
#include "include/wrapper/cef_message_router.h"
#include "workspace/context_menu_model.h"
#include "windows/menu_popup.h"
namespace red {
class SidebarMenu : public std::enable_shared_from_this<SidebarMenu> {
 public:
  explicit SidebarMenu(std::weak_ptr<State> state) : state_(std::move(state)) {}
  void Open(const std::string& id, int x, int y, int64_t query, CefRefPtr<CefMessageRouterBrowserSide::Callback> callback,
            CefRefPtr<CefBrowser> source = nullptr);
  void Cancel(int64_t query = -1, bool reply = true);
  void Execute(unsigned generation, int command);
  // A row chosen in Red's menu: remember it, then finish as a closing menu does.
  void Choose(unsigned generation, int command);
  void Closed(unsigned generation);
  void Shown(unsigned generation);
  bool ProbeSelect(const std::string& action = "rename");
  bool is_open() const { return shown_ || callback_ != nullptr; }
 private:
  void Complete(const std::string& action, const std::string& target = "");
  void Fill(MenuPopup::Builder& menu, const std::vector<SidebarMenuItem>& items);
  std::weak_ptr<State> state_;
  bool menu_open_ = false;
  CefRefPtr<CefMessageRouterBrowserSide::Callback> callback_;
  std::map<int, std::pair<std::string, std::string>> actions_;
  unsigned generation_ = 0;
  int64_t query_ = -1;
  bool shown_ = false;
  std::string selected_action_, selected_target_;
  CefRefPtr<CefBrowser> source_;
};
}  // namespace red
