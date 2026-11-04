#pragma once
#include "app/state.h"
#include "app/preferences.h"
#include "include/views/cef_browser_view.h"
#include "include/wrapper/cef_message_router.h"
namespace red {
class SettingsWindow : public std::enable_shared_from_this<SettingsWindow> {
 public:
  explicit SettingsWindow(std::weak_ptr<State> state) : state_(std::move(state)) {}
  void Show();
  void Present();
  CefRefPtr<CefWindow> parent() const;
  void Created(CefRefPtr<CefWindow> window) { window_ = window; }
  void BrowserCreated(CefRefPtr<CefBrowser> browser);
  void BrowserClosed(int id);
  void Destroyed();
  void Close();
  // Opens Settings on one section ("import").
  void ShowSection(const std::string& section);
  bool Query(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, const CefString& request,
             bool persistent, CefRefPtr<CefMessageRouterBrowserSide::Callback> callback);
 private:
  void SavePreferences(Preferences next, CefRefPtr<CefMessageRouterBrowserSide::Callback> callback);
  std::string Response(bool refresh_default = false) const;
  mutable CefRefPtr<CefDictionaryValue> default_status_;
  std::weak_ptr<State> state_;
  CefRefPtr<CefWindow> window_;
  CefRefPtr<CefBrowser> browser_;
  std::shared_ptr<class BrowserImport> import_;
  std::string section_;
  bool creating_ = false, ready_ = false;
};
void ShowSettings(const std::shared_ptr<State>& state);
}  // namespace red
