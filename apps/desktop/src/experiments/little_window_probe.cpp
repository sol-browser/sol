#include "experiments/little_window_probe.h"
#include "app/session.h"
#include "app/preferences.h"
#include "windows/command_bar.h"
#include "include/base/cef_callback.h"
#include "include/wrapper/cef_closure_task.h"
#include "workspace/controller.h"
namespace red {
namespace {
std::string Origin(const std::string& url) {
  const auto scheme = url.find("://");
  const auto slash = scheme == std::string::npos ? std::string::npos : url.find('/', scheme + 3);
  return slash == std::string::npos ? url : url.substr(0, slash);
}
// Records go to the probing (first) window's evidence.
std::shared_ptr<State> Prober(const std::shared_ptr<State>& state) {
  if (state->little_probe) return state;
  if (!state->session) return nullptr;
  for (const auto& window : state->session->Windows()) if (window->little_probe) return window;
  return nullptr;
}
std::shared_ptr<State> LittleWindow(const std::shared_ptr<State>& prober) {
  for (const auto& window : prober->session->Windows()) if (window->little_window && window->window) return window;
  return nullptr;
}
// Open pages in a window's tree, and whether one of them has |suffix| as its address.
int Pages(const std::shared_ptr<State>& state) {
  if (!state || !state->workspace) return -1;
  // The list belongs to the snapshot: keep the snapshot alive while reading it.
  const auto snapshot = state->workspace->Snapshot();
  const auto nodes = snapshot->GetList("nodes");
  int pages = 0;
  for (size_t i = 0; nodes && i < nodes->GetSize(); ++i) {
    const auto node = nodes->GetDictionary(i);
    if (node->GetString("kind") == "tab" && !node->GetBool("archived")) ++pages;
  }
  return pages;
}
bool HasPage(const std::shared_ptr<State>& state, const std::string& suffix, bool live = false) {
  if (!state || !state->workspace) return false;
  // The list belongs to the snapshot: keep the snapshot alive while reading it.
  const auto snapshot = state->workspace->Snapshot();
  const auto nodes = snapshot->GetList("nodes");
  for (size_t i = 0; nodes && i < nodes->GetSize(); ++i) {
    const auto node = nodes->GetDictionary(i);
    const auto url = node->HasKey("currentUrl") ? node->GetString("currentUrl").ToString() : node->GetString("url").ToString();
    if (url.ends_with(suffix) && !node->GetBool("archived") && (!live || node->GetBool("live"))) return true;
  }
  return false;
}
void Later(const std::shared_ptr<State>& state, int delay, void (*step)(const std::shared_ptr<State>&)) {
  CefPostDelayedTask(TID_UI, base::BindOnce([](std::weak_ptr<State> weak, void (*step)(const std::shared_ptr<State>&)) {
    if (const auto state = weak.lock()) step(state);
  }, std::weak_ptr<State>(state), step), delay);
}
void SendToSpace(const std::shared_ptr<State>& prober) {
  const auto little = LittleWindow(prober);
  if (!little || !little->workspace) { prober->Record("little_sent", nullptr); prober->little_done = true; return; }
  auto links = CefDictionaryValue::Create();
  links->SetInt("little_pages", Pages(little));
  links->SetBool("little_kept_page", HasPage(little, "/little"));
  links->SetBool("foreground_in_workspace", HasPage(prober, "/blank"));
  links->SetBool("background_in_workspace", HasPage(prober, "/background"));
  links->SetString("little_notice", little->workspace->Snapshot()->GetString("notice"));
  prober->Record("little_links", links);
  little->workspace->SendLittlePage(little->initial_space,
      [weak = std::weak_ptr<State>(prober)](int code, const std::string& error, CefRefPtr<CefDictionaryValue>) {
    const auto prober = weak.lock(); if (!prober) return;
    auto details = CefDictionaryValue::Create(); details->SetInt("code", code); details->SetString("error", error);
    prober->Record("little_sent", details);
    Later(prober, 2500, [](const std::shared_ptr<State>& prober) {
      auto details = CefDictionaryValue::Create();
      details->SetBool("little_closed", LittleWindow(prober) == nullptr);
      details->SetBool("page_in_workspace", HasPage(prober, "/little", true));
      prober->Record("little_closed", details);
      prober->little_done = true;
    });
  });
}
int LittleWindows(const std::shared_ptr<State>& prober) {
  int count = 0;
  for (const auto& window : prober->session->Windows()) if (window->little_window && window->window) ++count;
  return count;
}
// --probe-little-window=external: the driver launches sol.exe again with a link, as a chat app does.
// /external must open in Little Sol; with the setting off, /external-tab must open as a tab.
void OnExternalLoad(const std::shared_ptr<State>& prober, const std::shared_ptr<State>& state, const std::string& url) {
  if (state == prober && url.ends_with("/source") && !prober->little_started) {
    prober->little_started = true;
    prober->Record("little_external_ready");
    return;
  }
  if (state->little_window && url.ends_with("/external") && !prober->little_links_started) {
    prober->little_links_started = true;
    auto details = CefDictionaryValue::Create();
    details->SetInt("pages", Pages(state));
    details->SetBool("in_workspace", HasPage(prober, "/external"));
    details->SetInt("little_windows", LittleWindows(prober));
    prober->Record("little_external_loaded", details);
    if (!prober->session || !prober->session->preferences) { prober->little_done = true; return; }
    auto next = prober->session->preferences->Get();
    next.little_sol_links = false;
    prober->session->preferences->Save(next, [weak = std::weak_ptr<State>(prober)](const std::string& error) {
      const auto prober = weak.lock(); if (!prober) return;
      auto details = CefDictionaryValue::Create(); details->SetString("error", error);
      prober->Record("little_pref_off", details);
    });
    return;
  }
  if (state == prober && url.ends_with("/external-tab")) {
    Later(prober, 1000, [](const std::shared_ptr<State>& prober) {
      auto details = CefDictionaryValue::Create();
      details->SetBool("in_workspace", HasPage(prober, "/external-tab"));
      details->SetInt("little_windows", LittleWindows(prober));
      prober->Record("little_external_tab", details);
      // The probe's own Little Sol: closed so Sol can shut down when the probe finishes.
      for (const auto& window : prober->session->Windows()) if (window->little_window && window->window) window->window->Close();
      prober->little_done = true;
    });
  }
}
// --probe-little-window=launcher: what Ctrl+Alt+N does. The bar must show on its own while its
// Little Sol stays hidden; a submitted address shows that window with the page. A second bar,
// closed without input, must leave no window behind.
std::shared_ptr<State> Launcher(const std::shared_ptr<State>& prober) {
  for (const auto& window : prober->session->Windows()) if (window->little_launcher) return window;
  return nullptr;
}
void WaitForBar(std::weak_ptr<State> weak, int attempts, bool submit) {
  const auto prober = weak.lock(); if (!prober || prober->little_done) return;
  const auto launcher = Launcher(prober);
  const auto bar = launcher ? launcher->command_bar : nullptr;
  if (bar && bar->is_presented() && bar->detached()) {
    auto details = CefDictionaryValue::Create();
    details->SetBool("little_visible", launcher->window && launcher->window->IsVisible());
    prober->Record(submit ? "little_launcher_bar" : "little_launcher_bar_again", details);
    const auto origin = Origin(prober->workspace->Snapshot()->GetString("url").ToString());
    bar->ProbeLauncher(prober, submit ? origin + "/typed" : "");
    if (!submit) Later(prober, 1500, [](const std::shared_ptr<State>& prober) {
      auto details = CefDictionaryValue::Create();
      details->SetBool("launcher_gone", Launcher(prober) == nullptr);
      details->SetInt("little_windows", LittleWindows(prober));
      prober->Record("little_launcher_cancelled", details);
      for (const auto& window : prober->session->Windows()) if (window->little_window && window->window) window->window->Close();
      prober->little_done = true;
    });
    return;
  }
  if (attempts <= 0) {
    // Which step never came: the window, its shell page, the workspace or the bar.
    auto details = CefDictionaryValue::Create();
    details->SetBool("launcher", launcher != nullptr);
    if (launcher) {
      details->SetBool("window", launcher->window != nullptr);
      details->SetBool("shell", launcher->browsers.contains(launcher->shell_id));
      details->SetBool("ready", launcher->workspace && launcher->workspace->Snapshot()->GetBool("ready"));
      details->SetBool("bar", bar != nullptr);
      details->SetBool("bar_open", bar && bar->is_open());
      details->SetBool("bar_presented", bar && bar->is_presented());
      details->SetBool("bar_detached", bar && bar->detached());
    }
    prober->Record("little_launcher_timeout", details);
    for (const auto& window : prober->session->Windows()) if (window->little_window && window->window) window->window->Close();
    prober->little_done = true;
    return;
  }
  CefPostDelayedTask(TID_UI, base::BindOnce([](std::weak_ptr<State> weak, int attempts, bool submit) {
    WaitForBar(weak, attempts - 1, submit);
  }, weak, attempts, submit), 100);
}
void StartLauncher(const std::shared_ptr<State>& prober, bool submit) {
  const bool created = prober->session && prober->session->new_little_sol_launcher && prober->session->new_little_sol_launcher(prober);
  auto details = CefDictionaryValue::Create(); details->SetBool("created", created);
  prober->Record(submit ? "little_launcher_created" : "little_launcher_created_again", details);
  if (!created) { prober->little_done = true; return; }
  WaitForBar(prober, 80, submit);
}
void OnLauncherLoad(const std::shared_ptr<State>& prober, const std::shared_ptr<State>& state, const std::string& url) {
  if (state == prober && url.ends_with("/source") && !prober->little_started) {
    prober->little_started = true;
    Later(prober, 800, [](const std::shared_ptr<State>& prober) { StartLauncher(prober, true); });
    return;
  }
  if (state->little_window && url.ends_with("/typed") && !prober->little_links_started) {
    prober->little_links_started = true;
    auto details = CefDictionaryValue::Create();
    details->SetBool("visible", state->window && state->window->IsVisible());
    details->SetInt("pages", Pages(state));
    details->SetBool("still_launcher", state->little_launcher);
    prober->Record("little_launcher_page", details);
    Later(prober, 1000, [](const std::shared_ptr<State>& prober) {
      // The finished launcher replaced its detached bar with an ordinary one.
      for (const auto& window : prober->session->Windows())
        if (window->little_window && window->window && window->command_bar) {
          auto details = CefDictionaryValue::Create(); details->SetBool("detached", window->command_bar->detached());
          prober->Record("little_launcher_bar_replaced", details);
        }
      StartLauncher(prober, false);
    });
  }
}
// --probe-little-window=click: Ctrl+Alt+click on a link (the page is one big link). The modifiers
// go in with the click, as Windows reports them; Blink calls this "open in split view".
void OnClickLoad(const std::shared_ptr<State>& prober, const std::shared_ptr<State>& state, const std::string& url) {
  if (state == prober && url.ends_with("/click-source") && !prober->little_started) {
    prober->little_started = true;
    Later(prober, 800, [](const std::shared_ptr<State>& prober) {
      if (!prober->content) { prober->Record("little_click_sent", nullptr); prober->little_done = true; return; }
      const auto host = prober->content->GetHost();
      CefMouseEvent event; event.x = 120; event.y = 120;
      event.modifiers = EVENTFLAG_CONTROL_DOWN | EVENTFLAG_ALT_DOWN;
      host->SendMouseMoveEvent(event, false);
      event.modifiers |= EVENTFLAG_LEFT_MOUSE_BUTTON;
      host->SendMouseClickEvent(event, MBT_LEFT, false, 1);
      event.modifiers &= ~EVENTFLAG_LEFT_MOUSE_BUTTON;
      host->SendMouseClickEvent(event, MBT_LEFT, true, 1);
      prober->Record("little_click_sent");
      // Nothing opened: report what is there and finish.
      Later(prober, 6000, [](const std::shared_ptr<State>& prober) {
        if (prober->little_done) return;
        auto details = CefDictionaryValue::Create();
        details->SetInt("little_windows", LittleWindows(prober));
        details->SetBool("in_workspace", HasPage(prober, "/clicked"));
        prober->Record("little_click_timeout", details);
        prober->little_done = true;
      });
    });
    return;
  }
  if (state->little_window && url.ends_with("/clicked") && !prober->little_links_started) {
    prober->little_links_started = true;
    auto details = CefDictionaryValue::Create();
    details->SetInt("pages", Pages(state));
    details->SetInt("little_windows", LittleWindows(prober));
    details->SetBool("in_workspace", HasPage(prober, "/clicked"));
    details->SetBool("source_kept", HasPage(prober, "/click-source"));
    prober->Record("little_click_opened", details);
    Later(prober, 500, [](const std::shared_ptr<State>& prober) {
      for (const auto& window : prober->session->Windows()) if (window->little_window && window->window) window->window->Close();
      prober->little_done = true;
    });
  }
}
}  // namespace
void OnLittleWindowProbeLoad(const std::shared_ptr<State>& state, const std::string& url) {
  const auto prober = Prober(state);
  if (!prober || prober->little_done || !url.starts_with("http://localhost:")) return;
  const auto origin = Origin(url);
  if (prober->little_launcher_probe) {
    OnLauncherLoad(prober, state, url);
    return;
  }
  if (prober->little_external) {
    OnExternalLoad(prober, state, url);
    return;
  }
  if (prober->little_click_probe) {
    OnClickLoad(prober, state, url);
    return;
  }
  if (state->little_window && url.ends_with("/little") && !prober->little_links_started) {
    prober->little_links_started = true;
    auto details = CefDictionaryValue::Create();
    details->SetInt("pages", Pages(state));
    details->SetBool("sidebar_visible", state->sidebar_visible);
    details->SetBool("standalone", state->standalone_window);
    details->SetString("initial_space", state->initial_space);
    if (state->window) details->SetInt("width", state->window->GetBounds().width);
    prober->Record("little_loaded", details);
    // What OnBeforePopup does for target=_blank links: a foreground one, then a background one.
    const auto page = state->workspace->LiveBrowser(state->workspace->Snapshot()->GetString("activeId").ToString());
    state->workspace->OpenLink(origin + "/blank", true, {}, url, page);
    state->workspace->OpenLink(origin + "/background", false, {}, url, page);
    Later(prober, 3000, SendToSpace);
    return;
  }
  if (state == prober && url.ends_with("/source") && !prober->little_started) {
    prober->little_started = true;
    Later(prober, 800, [](const std::shared_ptr<State>& state) {
      const auto url = state->workspace->Snapshot()->GetString("url").ToString();
      const bool created = state->session && state->session->new_little_window &&
          state->session->new_little_window(state, Origin(url) + "/little");
      auto details = CefDictionaryValue::Create(); details->SetBool("created", created);
      state->Record("little_opened", details);
      if (!created) state->little_done = true;
    });
  }
}
}  // namespace red
