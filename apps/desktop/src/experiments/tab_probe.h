#pragma once
#include <memory>
#include <string>
#include "app/state.h"

namespace red {
// A bounded feasibility experiment, deliberately separate from production IPC.
// No JavaScript requested by a website or IPC payload is ever evaluated here.
class TabProbe : public std::enable_shared_from_this<TabProbe> {
 public:
  TabProbe(std::weak_ptr<State> state, std::string first_url);
  void OnCreated(CefRefPtr<CefBrowser> browser);
  void OnLoad(CefRefPtr<CefBrowser> browser, int status);
  void OnClosed(int browser_id);
  void OnConsole(CefRefPtr<CefBrowser> browser, const std::string& message);
  void OnFocus(int browser_id);
  bool passed() const;
  void Summarize();
 private:
  enum class Stage { FirstLoad, FirstMarker, SecondCreate, SecondLoad, SecondMarker,
                     FirstReturn, SecondReturn, ClosingSecond, ShellCheck, Done, Failed };
  void Later(int delay_ms, void (TabProbe::*method)());
  void CreateSecond();
  void ReturnFirst();
  void ReturnSecond();
  void CloseSecond();
  void CheckShell();
  void Fail(const std::string& reason);
  bool Command(CefRefPtr<CefBrowser> browser, const char* name);
  void ReadMarker(CefRefPtr<CefBrowser> browser, const char* phase);
  void WriteMarker(CefRefPtr<CefBrowser> browser, const char* phase);
  void Record(const std::string& event, CefRefPtr<CefDictionaryValue> details = nullptr);
  std::weak_ptr<State> state_;
  std::string second_url_;
  CefRefPtr<CefBrowser> first_, second_;
  Stage stage_ = Stage::FirstLoad;
  int shell_id_ = -1;
  int first_loads_ = 0, second_loads_ = 0;
  int first_id_ = -1, second_id_ = -1;
  std::string first_marker_, second_marker_;
  bool same_window_ = false, first_preserved_ = false, second_preserved_ = false;
  bool first_focused_ = false, second_focused_ = false;
  bool second_closed_ = false, shell_retained_ = false;
};
}  // namespace red
