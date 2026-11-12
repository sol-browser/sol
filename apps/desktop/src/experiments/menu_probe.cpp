#include "experiments/menu_probe.h"
#include "include/cef_devtools_message_observer.h"
#include <algorithm>
#include "include/base/cef_callback.h"
#include "include/wrapper/cef_closure_task.h"
#include "windows/main_menu.h"
#include "windows/settings_window.h"
#include "windows/menu_popup.h"
#include "app/updates.h"
#include "downloads/download_manager.h"
#include "windows/page_capture.h"
#include "windows/capture_window.h"
#include "experiments/shell_capture.h"
#include "navigation/shortcuts.h"
#include <windows.h>
#include "app/session.h"
#include "workspace/controller.h"
#include "include/views/cef_display.h"
namespace red {
namespace {
// Finds the row and reports its centre; native input then moves to it (and clicks), as a person would.
std::string Row(const char* label, const char* action) {
  return std::string("(() => { const row = [...document.querySelectorAll('.menu-row')].find(e => e.textContent.startsWith('") + label + "'));"
         " const r = row ? row.getBoundingClientRect() : { left: -1, top: -1, width: 0, height: 0 };"
         " window.cefQuery({ request: JSON.stringify({ method: 'menu.probe:" + action + ":' + Math.round(r.left + r.width / 2) + ':' + Math.round(r.top + r.height / 2) }),"
         " persistent: false, onSuccess() {}, onFailure() {} }); })()";
}
// Geometry acceptance uses the real frontend's click handler for warm
// levels, independent of CDP's cached hover state in a moved native widget.
std::string ActivateRow(const char* label) {
  return std::string("[...document.querySelectorAll('.menu-row')].find(e=>e.textContent.startsWith('")+label+"'))?.click()";
}
void Later(const std::shared_ptr<State>& state, int delay_ms, void (*step)(const std::shared_ptr<State>&)) {
  CefPostDelayedTask(TID_UI, base::BindOnce([](std::weak_ptr<State> weak, void (*step)(const std::shared_ptr<State>&)) {
    if (const auto state = weak.lock(); state && state->menu_popup) step(state);
  }, std::weak_ptr<State>(state), step), delay_ms);
}
std::string ClipboardText() {
  std::string text;
  if (!OpenClipboard(nullptr)) return text;
  if (const HANDLE handle = GetClipboardData(CF_UNICODETEXT)) {
    if (const auto* wide = static_cast<const wchar_t*>(GlobalLock(handle))) { text = CefString(std::wstring(wide)).ToString(); GlobalUnlock(handle); }
  }
  CloseClipboard();
  return text;
}
// Input on the page as a person gives it (CDP input is trusted, like the real thing).
// While the capture window is open it lies over the page and gets the input, as it would from a person.
CefRefPtr<CefBrowser> InputTarget(const std::shared_ptr<State>& s) {
  if (s->capture_window && s->capture_window->browser()) return s->capture_window->browser();
  return s->content;
}
void Mouse(const std::shared_ptr<State>& s, const char* type, double x, double y, bool down) {
  const auto target = InputTarget(s);
  if (!target) return;
  auto input = CefDictionaryValue::Create();
  input->SetString("type", type); input->SetDouble("x", x); input->SetDouble("y", y);
  input->SetString("button", down || std::string(type) == "mouseReleased" ? "left" : "none");
  input->SetInt("buttons", down ? 1 : 0); input->SetInt("clickCount", std::string(type) == "mouseMoved" ? 0 : 1);
  target->GetHost()->ExecuteDevToolsMethod(0, "Input.dispatchMouseEvent", input);
}
void Wheel(const std::shared_ptr<State>& s, double x, double y, double delta_y) {
  const auto target = InputTarget(s);
  if (!target) return;
  auto input = CefDictionaryValue::Create();
  input->SetString("type", "mouseWheel"); input->SetDouble("x", x); input->SetDouble("y", y);
  input->SetDouble("deltaX", 0); input->SetDouble("deltaY", delta_y);
  target->GetHost()->ExecuteDevToolsMethod(0, "Input.dispatchMouseEvent", input);
}
void Key(const std::shared_ptr<State>& s, const char* key, const char* code, int vk, int modifiers) {
  const auto target = InputTarget(s);
  if (!target) return;
  for (const char* type : {"rawKeyDown", "keyUp"}) {
    auto input = CefDictionaryValue::Create();
    input->SetString("type", type); input->SetString("key", key); input->SetString("code", code);
    input->SetInt("windowsVirtualKeyCode", vk); input->SetInt("modifiers", modifiers);
    target->GetHost()->ExecuteDevToolsMethod(0, "Input.dispatchKeyEvent", input);
  }
}
// A picture of the capture window as it is now (its own document, not the page under it).
void OverlayPicture(const std::shared_ptr<State>& s, const std::string& name) {
  static CefRefPtr<CefRegistration> capture;
  if (s->capture_window && s->capture_window->browser())
    capture = CaptureShell(s->capture_window->browser(), s->evidence_directory / (name + ".png"));
}
// What a copied picture looks like to other apps: "PNG" and CF_DIB (most apps paste only the bitmap).
void RecordClipboardPicture(const std::shared_ptr<State>& s) {
  auto data = CefDictionaryValue::Create();
  data->SetBool("png", IsClipboardFormatAvailable(RegisterClipboardFormatW(L"PNG")) != FALSE);
  data->SetBool("dib", IsClipboardFormatAvailable(CF_DIB) != FALSE);
  if (OpenClipboard(nullptr)) {
    if (const HANDLE handle = GetClipboardData(CF_DIB)) {
      if (const auto* header = static_cast<const BITMAPINFOHEADER*>(GlobalLock(handle))) {
        data->SetInt("width", header->biWidth); data->SetInt("height", header->biHeight); data->SetInt("bits", header->biBitCount);
        // Two pixels as the picture shows them (rows are stored bottom-up): 40 px down and 140 px down, 20 px in.
        if (header->biBitCount == 32 && header->biWidth > 20 && header->biHeight > 140) {
          const auto* pixels = reinterpret_cast<const unsigned char*>(header) + header->biSize;
          const auto at = [&](int x, int y) {
            const auto* p = pixels + (static_cast<size_t>(header->biHeight - 1 - y) * header->biWidth + x) * 4;
            return (p[2] << 16) | (p[1] << 8) | p[0];
          };
          data->SetInt("rgb_at_20_40", at(20, 40)); data->SetInt("rgb_at_20_140", at(20, 140));
        }
        GlobalUnlock(handle);
      }
    }
    CloseClipboard();
  }
  s->Record("capture_clipboard", data);
}
// The download list as the toolbar's Downloads button shows it: names and states.
void RecordDownloads(const std::shared_ptr<State>& s) {
  auto data = CefDictionaryValue::Create();
  auto names = CefListValue::Create();
  if (s->downloads) {
    const auto list = s->downloads->List();
    for (size_t i = 0; i < list->GetSize(); ++i)
      if (const auto row = list->GetDictionary(i)) names->SetString(i, row->GetString("name").ToString() + "|" + row->GetString("state").ToString());
    data->SetInt("started", s->downloads->started());
  }
  data->SetList("names", names);
  s->Record("downloads_list", data);
}
// Presses one of the capture window's top-right tiles (0: Save full page, 1: Save visible) as a click would.
void Tile(const std::shared_ptr<State>& s, int index) {
  if (s->capture_window && s->capture_window->browser())
    s->capture_window->browser()->GetMainFrame()->ExecuteJavaScript(
        "document.querySelectorAll('.capture-tile')[" + std::to_string(index) + "]?.click()", "", 0);
}
void RecordActive(const std::shared_ptr<State>& s, const char* step) {
  auto data = CefDictionaryValue::Create();
  data->SetString("step", step); data->SetString("active", s->workspace->Snapshot()->GetString("activeId"));
  s->Record("recent_probe", data);
}
// Polls the status every 250 ms: records each change, stops when the check ends or after two minutes.
void WatchUpdate(const std::shared_ptr<State>& state, int polls, int phase, int percent, bool restarted) {
  const auto status = CurrentUpdateStatus();
  const int now = static_cast<int>(status.phase);
  if (now != phase || status.percent != percent) {
    auto details = CefDictionaryValue::Create();
    details->SetInt("phase", now); details->SetInt("percent", status.percent); details->SetString("version", status.version);
    details->SetBool("window", state->update_id.load() > 0);
    state->Record("update_probe_status", details);
  }
  const bool ended = status.phase == UpdatePhase::Ready || status.phase == UpdatePhase::UpToDate ||
                     status.phase == UpdatePhase::Failed || status.phase == UpdatePhase::NotInstalled;
  if (ended && state->menu_update_restart && status.phase == UpdatePhase::Ready && !restarted) {
    state->Record("update_probe_restart");
    RequestUpdateRestart(state);  // The window's "Restart to update" does exactly this.
    restarted = true;
  }
  if ((ended && !(state->menu_update_restart && status.phase == UpdatePhase::Ready)) || polls > 480) {
    auto data = CefDictionaryValue::Create();
    data->SetBool("closed", !state->menu_popup->is_open()); data->SetBool("window", state->update_id.load() > 0);
    state->Record("menu_probe_result", data); state->menu_done = true;
    return;
  }
  CefPostDelayedTask(TID_UI, base::BindOnce([](std::weak_ptr<State> weak, int polls, int phase, int percent, bool restarted) {
    if (const auto owner = weak.lock()) WatchUpdate(owner, polls + 1, phase, percent, restarted);
  }, std::weak_ptr<State>(state), polls + 1, now, status.percent, restarted), 250);
}
}  // namespace
void OnMenuProbeLoad(const std::shared_ptr<State>& state, const std::string& url) {
  if (!state->menu_probe || state->menu_probe_started || !url.starts_with("http")) return;
  state->menu_probe_started = true;
  if(state->menu_edges) {
    for(int i=0;i<4;++i) CefPostDelayedTask(TID_UI,base::BindOnce([](std::weak_ptr<State> weak,int corner) {
      const auto s=weak.lock();if(!s || !s->window)return;
      if(!s->menu_popup)s->menu_popup=std::make_shared<MenuPopup>(s);
      const auto work=s->window->GetDisplay()->GetWorkArea();
      MenuPopup::Builder menu;
      menu.Add("First","copy",[] {});
      auto& sub=menu.Submenu("Branch","folder");
      sub.Add("Child","copy",[] {});
      sub.Submenu("Nested","folder").Add("Pick","copy",[] {});
      menu.Add("Last","copy",[] {});
      s->menu_popup->Open(menu,CefPoint(work.x+(corner%2 ? work.width-1 : 1),work.y+(corner/2 ? work.height-1 : 1)),"edge:"+std::to_string(corner));
      Later(s,1300,[](const std::shared_ptr<State>& s) {s->menu_popup->ProbeRun(0,ActivateRow("Branch"));});
      Later(s,2200,[](const std::shared_ptr<State>& s) {s->menu_popup->ProbeRun(1,ActivateRow("Nested"));});
      Later(s,3000,[](const std::shared_ptr<State>& s) {s->menu_popup->ProbeRun(2,ActivateRow("Pick"));});
    },std::weak_ptr<State>(state),i),1200+i*4000);
    Later(state,17500,[](const std::shared_ptr<State>& s) {
      auto d=CefDictionaryValue::Create();d->SetBool("closed",!s->menu_popup->is_open());s->Record("menu_probe_result",d);s->menu_done=true;
    });
    return;
  }
  if (state->menu_page) {
    // A right click on the page (CDP input, trusted); then "Reload" in Red's menu must reach Chrome.
    CefPostDelayedTask(TID_UI, base::BindOnce([](std::weak_ptr<State> weak) {
      const auto state = weak.lock(); if (!state || !state->content) return;
      if (!state->menu_popup) state->menu_popup = std::make_shared<MenuPopup>(state);
      state->menu_loads = state->content_loads;
      for (const char* type : {"mousePressed", "mouseReleased"}) {
        auto input = CefDictionaryValue::Create(); input->SetString("type", type); input->SetDouble("x", 200); input->SetDouble("y", 200);
        input->SetString("button", "right"); input->SetInt("clickCount", 1);
        state->content->GetHost()->ExecuteDevToolsMethod(0, "Input.dispatchMouseEvent", input);
      }
      Later(state, 1500, [](const std::shared_ptr<State>& s) { s->menu_popup->ProbeRun(0, Row("Reload", "click")); });
      Later(state, 4000, [](const std::shared_ptr<State>& s) {
        auto data = CefDictionaryValue::Create(); data->SetBool("closed", !s->menu_popup->is_open());
        data->SetInt("loads_before", s->menu_loads); data->SetInt("loads_after", s->content_loads);
        s->Record("menu_probe_result", data); s->menu_done = true;
      });
    }, std::weak_ptr<State>(state)), 1200);
    return;
  }
  if (state->menu_settings) {
    // Settings on this profile: does the window come up, and what does its page get (settings-console.txt)?
    if (!state->menu_popup) state->menu_popup = std::make_shared<MenuPopup>(state);  // Later() runs steps only with one.
    Later(state, 1500, [](const std::shared_ptr<State>& s) { ShowSettings(s); });
    Later(state, 7000, [](const std::shared_ptr<State>& s) {
      auto data = CefDictionaryValue::Create();
      const auto window = s->settings_window ? s->settings_window->window() : nullptr;
      data->SetBool("exists", window != nullptr); data->SetBool("visible", window && window->IsVisible());
      s->Record("settings_probe", data);
      if (const auto browser = s->settings_window ? s->settings_window->browser() : nullptr)
        browser->GetMainFrame()->ExecuteJavaScript(
            "(() => { const t = performance.now(); window.cefQuery({ request: JSON.stringify({ method: 'settings.get' }), persistent: false,"
            " onSuccess: r => console.log('SETTINGS_GET ' + Math.round(performance.now() - t) + 'ms ' + r),"
            " onFailure: (c, m) => console.log('SETTINGS_FAIL ' + Math.round(performance.now() - t) + 'ms ' + c + ' ' + m) }); })()", "", 0);
    });
    Later(state, 16000, [](const std::shared_ptr<State>& s) {
      auto done = CefDictionaryValue::Create(); done->SetBool("closed", true);
      s->Record("menu_probe_result", done); s->menu_done = true;
    });
    return;
  }
  if (state->menu_capture_tiles) {
    // The capture window's top-right tiles (scripts/capture_picker_experiment.py --tiles): Save visible after the
    // whole-page picture is there, then Save full page; pictures of the window before each.
    if (!state->menu_popup) state->menu_popup = std::make_shared<MenuPopup>(state);  // Later() runs steps only with one.
    Later(state, 1500, [](const std::shared_ptr<State>& s) { StartCapturePicker(s); });
    Later(state, 5000, [](const std::shared_ptr<State>& s) { OverlayPicture(s, "tiles-visible"); });
    Later(state, 5400, [](const std::shared_ptr<State>& s) { Tile(s, 1); });
    Later(state, 8000, [](const std::shared_ptr<State>& s) { StartCapturePicker(s); });
    Later(state, 11500, [](const std::shared_ptr<State>& s) { OverlayPicture(s, "tiles-full"); });
    Later(state, 11900, [](const std::shared_ptr<State>& s) { Tile(s, 0); });
    Later(state, 17000, [](const std::shared_ptr<State>& s) {
      RecordDownloads(s);
      auto done = CefDictionaryValue::Create(); done->SetBool("closed", true);
      s->Record("menu_probe_result", done); s->menu_done = true;
    });
    return;
  }
  if (state->menu_capture) {
    // The capture window (scripts/capture_picker_experiment.py reads page_screenshot, the saved pictures and the
    // window's own pictures overlay.png / glide.png / adjust.png in the report's folder):
    //  1. hover the element: a picture of the overlay; click it; a picture of the adjust state; Ctrl+S downloads it
    //  2. drag an area, resize it by its corner handle, Enter copies it (clipboard only, no file)
    //  3. Escape cancels
    if (!state->menu_popup) state->menu_popup = std::make_shared<MenuPopup>(state);  // Later() runs steps only with one.
    Later(state, 1500, [](const std::shared_ptr<State>& s) { StartCapturePicker(s); });
    Later(state, 2600, [](const std::shared_ptr<State>& s) { Mouse(s, "mouseMoved", 500, 260, false); });
    Later(state, 2900, [](const std::shared_ptr<State>& s) { OverlayPicture(s, "overlay"); });
    // Moving to another element: the highlight glides, so a picture taken 60 ms into the move shows it between the two.
    Later(state, 3200, [](const std::shared_ptr<State>& s) { Mouse(s, "mouseMoved", 150, 470, false); });
    Later(state, 3260, [](const std::shared_ptr<State>& s) { OverlayPicture(s, "glide"); });
    Later(state, 3600, [](const std::shared_ptr<State>& s) { Mouse(s, "mouseMoved", 500, 260, false); });
    Later(state, 3900, [](const std::shared_ptr<State>& s) { Mouse(s, "mousePressed", 500, 260, true); Mouse(s, "mouseReleased", 500, 260, false); });
    Later(state, 4800, [](const std::shared_ptr<State>& s) { OverlayPicture(s, "adjust"); });
    Later(state, 6000, [](const std::shared_ptr<State>& s) { Key(s, "s", "KeyS", 83, 2); });
    Later(state, 8500, [](const std::shared_ptr<State>& s) { StartCapturePicker(s); });
    Later(state, 9300, [](const std::shared_ptr<State>& s) { Mouse(s, "mouseMoved", 60, 80, false); Mouse(s, "mousePressed", 60, 80, true); });
    Later(state, 9700, [](const std::shared_ptr<State>& s) { Mouse(s, "mouseMoved", 160, 150, true); Mouse(s, "mouseMoved", 260, 200, true); });
    Later(state, 10200, [](const std::shared_ptr<State>& s) { Mouse(s, "mouseReleased", 260, 200, false); });
    Later(state, 10900, [](const std::shared_ptr<State>& s) { Mouse(s, "mouseMoved", 260, 200, false); Mouse(s, "mousePressed", 260, 200, true); });
    Later(state, 11300, [](const std::shared_ptr<State>& s) { Mouse(s, "mouseMoved", 300, 240, true); });
    Later(state, 11700, [](const std::shared_ptr<State>& s) { Mouse(s, "mouseReleased", 300, 240, false); });
    Later(state, 12500, [](const std::shared_ptr<State>& s) { Key(s, "Enter", "Enter", 13, 0); });
    Later(state, 13600, [](const std::shared_ptr<State>& s) { RecordClipboardPicture(s); });
    // A drag that starts low in the window, is carried by the wheel to the bottom of the page and then goes
    // far past the page's end: the area must end exactly at the page's edge, not beyond it.
    Later(state, 14500, [](const std::shared_ptr<State>& s) { StartCapturePicker(s); });
    Later(state, 15300, [](const std::shared_ptr<State>& s) { Mouse(s, "mouseMoved", 100, 600, false); Mouse(s, "mousePressed", 100, 600, true); });
    Later(state, 15700, [](const std::shared_ptr<State>& s) { Wheel(s, 100, 600, 5000); });
    Later(state, 15790, [](const std::shared_ptr<State>& s) { OverlayPicture(s, "scrolling"); });
    Later(state, 16500, [](const std::shared_ptr<State>& s) { Mouse(s, "mouseMoved", 400, 770, true); Mouse(s, "mouseMoved", 900000, 770, true); });
    Later(state, 16800, [](const std::shared_ptr<State>& s) { OverlayPicture(s, "long-drag"); });
    Later(state, 17000, [](const std::shared_ptr<State>& s) { Mouse(s, "mouseReleased", 400, 770, false); });
    Later(state, 17500, [](const std::shared_ptr<State>& s) { OverlayPicture(s, "long"); });
    Later(state, 17800, [](const std::shared_ptr<State>& s) { Key(s, "s", "KeyS", 83, 2); });
    Later(state, 20500, [](const std::shared_ptr<State>& s) { StartCapturePicker(s); });
    Later(state, 21500, [](const std::shared_ptr<State>& s) { Key(s, "Escape", "Escape", 27, 0); });
    Later(state, 23500, [](const std::shared_ptr<State>& s) {
      RecordDownloads(s);
      auto done = CefDictionaryValue::Create(); done->SetBool("closed", true);
      s->Record("menu_probe_result", done); s->menu_done = true;
    });
    return;
  }
  if (state->menu_switcher_again) {
    // After a restart: which tabs does the strip offer? (A tab closed from the strip must not come back.)
    if (!state->menu_popup) state->menu_popup = std::make_shared<MenuPopup>(state);  // Later() runs steps only with one.
    Later(state, 3000, [](const std::shared_ptr<State>& s) { s->probe_ctrl_held = true; ExecuteShortcut(s, Shortcut::RecentTab); });
    Later(state, 4200, [](const std::shared_ptr<State>& s) { s->probe_ctrl_held = false; });
    Later(state, 6000, [](const std::shared_ptr<State>& s) {
      auto done = CefDictionaryValue::Create(); done->SetBool("closed", true);
      s->Record("menu_probe_result", done); s->menu_done = true;
    });
    return;
  }
  if (state->menu_switcher_empty) {
    // An empty tab (no page in front): the strip is drawn over the empty page area all the same. Ctrl is
    // "held", two presses, then Ctrl is let go (scripts/tab_switcher_experiment.py --empty).
    if (!state->menu_popup) state->menu_popup = std::make_shared<MenuPopup>(state);  // Later() runs steps only with one.
    Later(state, 1200, [](const std::shared_ptr<State>& s) { s->workspace->CloseActive(); });  // The only open page: the tab is empty after it.
    Later(state, 2500, [](const std::shared_ptr<State>& s) {
      auto data = CefDictionaryValue::Create(); data->SetBool("content", s->content != nullptr); s->Record("switcher_empty_start", data);
      s->probe_ctrl_held = true; ExecuteShortcut(s, Shortcut::RecentTab);
    });
    Later(state, 3200, [](const std::shared_ptr<State>& s) { ExecuteShortcut(s, Shortcut::RecentTab); });
    Later(state, 5200, [](const std::shared_ptr<State>& s) { s->probe_ctrl_held = false; });
    Later(state, 7000, [](const std::shared_ptr<State>& s) {
      RecordActive(s, "after_release");
      auto done = CefDictionaryValue::Create(); done->SetBool("closed", true);
      s->Record("menu_probe_result", done); s->menu_done = true;
    });
    return;
  }
  if (state->menu_switcher) {
    // The Ctrl+Tab strip (scripts/tab_switcher_experiment.py): a second tab is visited so it has a picture, then
    // Ctrl is "held" (probe_ctrl_held), the strip shows, the selection moves to the third tab, Ctrl+W closes that
    // tab from the strip, and letting go switches to the tab that is then selected. The script photographs the
    // window while the strip is up.
    if (!state->menu_popup) state->menu_popup = std::make_shared<MenuPopup>(state);  // Later() runs steps only with one.
    Later(state, 3500, [](const std::shared_ptr<State>& s) { RecordActive(s, "start"); ExecuteShortcut(s, Shortcut::RecentTab); });
    Later(state, 6500, [](const std::shared_ptr<State>& s) { RecordActive(s, "visited_b"); ExecuteShortcut(s, Shortcut::RecentTab); });
    Later(state, 9500, [](const std::shared_ptr<State>& s) { RecordActive(s, "back_on_a"); s->probe_ctrl_held = true; ExecuteShortcut(s, Shortcut::RecentTab); });
    Later(state, 11000, [](const std::shared_ptr<State>& s) { ExecuteShortcut(s, Shortcut::RecentTab); });
    Later(state, 12500, [](const std::shared_ptr<State>& s) { ExecuteShortcut(s, Shortcut::CloseTab); });  // Ctrl+W: closes it from the strip.
    Later(state, 14000, [](const std::shared_ptr<State>& s) { s->probe_ctrl_held = false; });
    Later(state, 16500, [](const std::shared_ptr<State>& s) {
      RecordActive(s, "after_release");
      auto done = CefDictionaryValue::Create(); done->SetBool("closed", true);
      s->Record("menu_probe_result", done); s->menu_done = true;
    });
    return;
  }
  if (state->menu_copy) {
    // Right click on the copy-link button opens this menu; "Super Copy" must leave the address without trackers on the clipboard.
    CefPostDelayedTask(TID_UI, base::BindOnce([](std::weak_ptr<State> weak) {
      const auto state = weak.lock(); if (!state) return;
      ShowCopyMenu(state, kMainMenuAnchor);
      Later(state, 1500, [](const std::shared_ptr<State>& s) { s->menu_popup->ProbeRun(0, ActivateRow("Super Copy")); });
      Later(state, 3000, [](const std::shared_ptr<State>& s) {
        auto data = CefDictionaryValue::Create(); data->SetString("clipboard", ClipboardText()); s->Record("copy_menu_probe", data);
        auto done = CefDictionaryValue::Create(); done->SetBool("closed", !s->menu_popup->is_open());
        s->Record("menu_probe_result", done); s->menu_done = true;
      });
    }, std::weak_ptr<State>(state)), 1200);
    return;
  }
  if (state->menu_recent) {
    // Ctrl+Tab by recent use and Super Copy, through the entry the keys use (ExecuteShortcut).
    // No key is held, so every press is its own cycle: A -> the tab used before -> back to A.
    if (!state->menu_popup) state->menu_popup = std::make_shared<MenuPopup>(state);  // Later() runs steps only with one.
    Later(state, 3500, [](const std::shared_ptr<State>& s) { RecordActive(s, "start"); ExecuteShortcut(s, Shortcut::RecentTab); });
    Later(state, 5500, [](const std::shared_ptr<State>& s) { RecordActive(s, "after_first"); ExecuteShortcut(s, Shortcut::RecentTab); });
    Later(state, 7500, [](const std::shared_ptr<State>& s) { RecordActive(s, "after_second"); ExecuteShortcut(s, Shortcut::CopyUrl); });
    Later(state, 8500, [](const std::shared_ptr<State>& s) {
      auto data = CefDictionaryValue::Create(); data->SetString("clipboard", ClipboardText()); s->Record("recent_probe_copy", data);
      auto done = CefDictionaryValue::Create(); done->SetBool("closed", true);
      s->Record("menu_probe_result", done); s->menu_done = true;
    });
    return;
  }
  if (state->menu_screenshot) {
    // The visible area, then the whole page (scripts/cheap_parity_experiment.py reads page_screenshot).
    if (!state->menu_popup) state->menu_popup = std::make_shared<MenuPopup>(state);  // Later() runs steps only with one.
    Later(state, 1500, [](const std::shared_ptr<State>& s) { CapturePage(s, false); });
    Later(state, 5000, [](const std::shared_ptr<State>& s) { CapturePage(s, true); });
    Later(state, 9000, [](const std::shared_ptr<State>& s) {
      auto data = CefDictionaryValue::Create(); data->SetBool("closed", true);
      s->Record("menu_probe_result", data); s->menu_done = true;
    });
    return;
  }
  if (state->menu_update) {
    // "Check for Updates" in the main menu opens the update window. The probe then records every
    // status the window would show until the check ends (scripts/update_window_e2e.py); with
    // --probe-menu=update-restart it presses "Restart to update" once a version waits.
    CefPostDelayedTask(TID_UI, base::BindOnce([](std::weak_ptr<State> weak) {
      const auto state = weak.lock(); if (!state) return;
      ShowMainMenu(state, kMainMenuAnchor);
      // The menu's own click handler, as the edge probe uses: a native mouse click can miss when someone moves the pointer.
      Later(state, 1500, [](const std::shared_ptr<State>& s) { s->menu_popup->ProbeRun(0, ActivateRow("Check for Updates")); });
      Later(state, 2500, [](const std::shared_ptr<State>& s) { WatchUpdate(s, 0, -1, -1, false); });
    }, std::weak_ptr<State>(state)), 1200);
    return;
  }
  CefPostDelayedTask(TID_UI, base::BindOnce([](std::weak_ptr<State> weak) {
    const auto state = weak.lock(); if (!state) return;
    ShowMainMenu(state, kMainMenuAnchor);
    Later(state, 1500, [](const std::shared_ptr<State>& s) { s->menu_popup->ProbeRun(0, Row("Tabs", "move")); });
    Later(state, 3000, [](const std::shared_ptr<State>& s) { s->menu_popup->ProbeRun(1, Row("Next Tab", "click")); });
    Later(state, 4500, [](const std::shared_ptr<State>& s) {
      auto data = CefDictionaryValue::Create(); data->SetBool("closed", !s->menu_popup->is_open());
      s->Record("menu_probe_result", data); s->menu_done = true;
    });
  }, std::weak_ptr<State>(state)), 1200);
}
}  // namespace red
