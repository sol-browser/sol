#include "experiments/split_probe.h"
#include <windows.h>
#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdlib>
#include <string>
#include "include/base/cef_callback.h"
#include "include/cef_parser.h"
#include "include/views/cef_display.h"
#include "include/wrapper/cef_closure_task.h"
#include "windows/embedded_content.h"
#include "windows/sidebar_peek.h"
#include "workspace/controller.h"

namespace red {
namespace {
// The probe's pages, by the last path segment of their fixture URL.
std::string NodeFor(const std::shared_ptr<State>& state, const std::string& suffix) {
  const auto snapshot = state->workspace->Snapshot();
  const auto nodes = snapshot->GetList("nodes");
  for (size_t i = 0; nodes && i < nodes->GetSize(); ++i) {
    const auto node = nodes->GetDictionary(i);
    if (node->GetString("url").ToString().ends_with("/split/" + suffix)) return node->GetString("id");
  }
  return "";
}
CefRefPtr<CefDictionaryValue> SplitView(const std::shared_ptr<State>& state) {
  // A copy: a child value is only valid while its parent snapshot is held.
  const auto snapshot = state->workspace->Snapshot();
  return snapshot->HasKey("splitView") ? snapshot->GetDictionary("splitView")->Copy(false) : nullptr;
}
void Send(const std::shared_ptr<State>& state, CefRefPtr<CefDictionaryValue> request, const char* label, bool mutation = false) {
  if (mutation) request->SetDouble("expectedRevision", state->workspace->Snapshot()->GetDouble("treeRevision"));
  state->workspace->Handle(request, [weak = std::weak_ptr<State>(state), label = std::string(label)](int code, const std::string& error,
                                                                                                   CefRefPtr<CefDictionaryValue>) {
    if (const auto s = weak.lock()) {
      auto data = CefDictionaryValue::Create(); data->SetString("step", label); data->SetInt("code", code); data->SetString("error", error);
      s->Record("split_probe_command", data);
    }
  });
}
CefRefPtr<CefDictionaryValue> Rect(const RECT& r) {
  auto value = CefDictionaryValue::Create();
  value->SetInt("x", r.left); value->SetInt("y", r.top); value->SetInt("width", r.right - r.left); value->SetInt("height", r.bottom - r.top);
  return value;
}
// Every visible pane's actual native window (client pixels) must be where
// EmbeddedContent placed it, inside the workspace, disjoint from the others and
// (in a large window) at least the minimum pane size.
void RecordGeometry(const std::shared_ptr<State>& state, const char* label, int expected_visible) {
  auto data = CefDictionaryValue::Create(); data->SetString("label", label);
  const auto view = SplitView(state);
  data->SetBool("split", view != nullptr);
  bool ok = view != nullptr || expected_visible == 0;
  if (view) {
    const HWND root = state->window->GetWindowHandle();
    const double scale = state->window->GetDisplay()->GetDeviceScaleFactor();
    const auto area = state->embedded->SplitDropArea();
    data->SetString("layout", view->GetString("layout")); data->SetBool("linked", view->GetBool("linked"));
    data->SetString("expanded", view->GetString("expanded")); data->SetString("focused", view->GetString("focused"));
    data->SetInt("dividers", static_cast<int>(view->GetList("dividers")->GetSize()));
    data->SetDictionary("area", Rect({area.x, area.y, area.x + area.width, area.y + area.height}));
    const auto panes = view->GetList("panes");
    auto rows = CefListValue::Create(); std::vector<RECT> visible;
    for (size_t i = 0; i < panes->GetSize(); ++i) {
      const auto id = panes->GetDictionary(i)->GetString("id").ToString();
      const auto browser = state->workspace->LiveBrowser(id);
      auto row = CefDictionaryValue::Create(); row->SetString("id", id);
      if (!browser) { row->SetBool("live", false); rows->SetDictionary(rows->GetSize(), row); continue; }
      const HWND page = browser->GetHost()->GetWindowHandle();
      RECT actual{}; GetWindowRect(page, &actual);
      POINT a{actual.left, actual.top}, b{actual.right, actual.bottom}; ScreenToClient(root, &a); ScreenToClient(root, &b);
      const RECT client{a.x, a.y, b.x, b.y};
      const auto expected = state->embedded->ExpectedPageBounds(browser->GetIdentifier());
      const bool shown = IsWindowVisible(page) != FALSE;
      const bool placed = std::abs(client.left - expected.x) <= 1 && std::abs(client.top - expected.y) <= 1 &&
                          std::abs((client.right - client.left) - expected.width) <= 1 && std::abs((client.bottom - client.top) - expected.height) <= 1;
      row->SetBool("live", true); row->SetBool("visible", shown); row->SetBool("placed", placed);
      row->SetDictionary("actual", Rect(client));
      row->SetDictionary("expected", Rect({expected.x, expected.y, expected.x + expected.width, expected.y + expected.height}));
      if (shown) {
        visible.push_back(client);
        ok = ok && placed && client.left >= area.x && client.top >= area.y &&
             client.right <= area.x + area.width && client.bottom <= area.y + area.height;
        // A page sits below its 30-DIP header inside an 8-DIP-gapped card: at least 220x160 DIP cards.
        if (area.width >= std::lround(1100 * scale) && area.height >= std::lround(700 * scale) && view->GetString("expanded").empty())
          ok = ok && client.right - client.left >= std::lround(218 * scale) - 1 && client.bottom - client.top >= std::lround(129 * scale) - 1;
      }
      rows->SetDictionary(rows->GetSize(), row);
    }
    for (size_t i = 0; i < visible.size(); ++i) for (size_t j = i + 1; j < visible.size(); ++j) {
      RECT overlap{}; if (IntersectRect(&overlap, &visible[i], &visible[j])) ok = false;
    }
    ok = ok && static_cast<int>(visible.size()) == expected_visible;
    data->SetList("panes", rows); data->SetInt("visible", static_cast<int>(visible.size()));
    // The divider the experiment drags: the grid's horizontal line (screen pixels).
    const auto dividers = view->GetList("dividers");
    for (size_t i = 0; i < dividers->GetSize(); ++i) {
      const auto divider = dividers->GetDictionary(i);
      if (divider->GetBool("cross") || divider->GetList("path")->GetSize() != 0 || divider->GetString("axis") != "v") continue;
      const auto bounds = divider->GetDictionary("bounds");
      POINT center{static_cast<LONG>(std::lround((bounds->GetDouble("x") + bounds->GetDouble("width") / 2) * scale)),
                   static_cast<LONG>(std::lround((bounds->GetDouble("y") + bounds->GetDouble("height") / 2) * scale))};
      ClientToScreen(root, &center);
      data->SetInt("drag_x", center.x); data->SetInt("drag_y", center.y);
    }
  }
  data->SetBool("geometry_ok", ok);
  state->Record("split_geometry", data);
}
using Step = void (*)(const std::shared_ptr<State>&);
void Later(const std::shared_ptr<State>& state, int delay, Step step) {
  CefPostDelayedTask(TID_UI, base::BindOnce([](std::weak_ptr<State> weak, Step step) {
    if (const auto state = weak.lock(); state && state->workspace && !state->workspace->WindowClosing()) step(state);
  }, std::weak_ptr<State>(state), step), delay);
}
CefRefPtr<CefDictionaryValue> Request(const char* method) {
  auto request = CefDictionaryValue::Create(); request->SetString("method", method); return request;
}
std::string anchor_id, grid_layout;
// How often a dragged divider resizes the page windows: pane A's window size is
// sampled every 15 ms while the experiment drags (heavy pages suffer otherwise).
RECT sampled{}; int resizes = 0, samples = 0;
// Where the shell draws a sidebar row (Runtime.evaluate in the trusted shell,
// probe only), so the experiment can drag that tab with the real pointer.
// A JSON number, whether CEF parsed it as an integer or a double.
double Number(CefRefPtr<CefDictionaryValue> value, const char* key) {
  return value->GetType(key) == VTYPE_INT ? value->GetInt(key) : value->GetDouble(key);
}
class RowObserver final : public CefDevToolsMessageObserver {
 public:
  // |floating|: the row is in the floating sidebar (its own window), else in the shell.
  explicit RowObserver(std::weak_ptr<State> state, bool floating = false) : state_(std::move(state)), floating_(floating) {}
  void OnDevToolsMethodResult(CefRefPtr<CefBrowser>, int id, bool success, const void* data, size_t size) override {
    const auto state = state_.lock(); if (!state || id != 7701 || !state->window) return;
    auto details = CefDictionaryValue::Create(); details->SetBool("success", success);
    const auto reply = CefParseJSON(std::string(static_cast<const char*>(data), size), JSON_PARSER_RFC);
    // Child values are only valid while their parent is held.
    const auto body = reply && reply->GetType() == VTYPE_DICTIONARY ? reply->GetDictionary() : nullptr;
    const auto result = body ? body->GetDictionary("result") : nullptr;
    const auto rect = result ? CefParseJSON(result->GetString("value"), JSON_PARSER_RFC) : nullptr;
    if (result) details->SetString("raw", result->GetString("value"));
    if (rect && rect->GetType() == VTYPE_DICTIONARY) {
      const auto box = rect->GetDictionary(); const double scale = state->window->GetDisplay()->GetDeviceScaleFactor();
      POINT row{static_cast<LONG>(std::lround((Number(box, "x") + 60) * scale)), static_cast<LONG>(std::lround((Number(box, "y") + Number(box, "height") / 2) * scale))};
      if (floating_) { RECT host{}; GetWindowRect(state->sidebar_peek->host(), &host); row.x += host.left; row.y += host.top; }
      else ClientToScreen(state->window->GetWindowHandle(), &row);
      details->SetInt("row_x", row.x); details->SetInt("row_y", row.y);
      // Drop on the lower edge of the right pane: it splits into top and bottom.
      const auto snapshot = state->workspace->Snapshot();
      if (snapshot->HasKey("splitView")) {
        const auto view = snapshot->GetDictionary("splitView"); const auto panes = view->GetList("panes");
        (void)panes;
        const auto pane = panes->GetDictionary(panes->GetSize() - 1); const auto right = pane->GetDictionary("bounds");
        POINT drop{static_cast<LONG>(std::lround((Number(right, "x") + Number(right, "width") / 2) * scale)),
                   static_cast<LONG>(std::lround((Number(right, "y") + Number(right, "height") * .9) * scale))};
        ClientToScreen(state->window->GetWindowHandle(), &drop);
        details->SetInt("drop_x", drop.x); details->SetInt("drop_y", drop.y);
      }
    }
    if (floating_ && !details->HasKey("drop_x")) {
      // No split on screen: drop on the right edge of the page area to make a pair.
      const auto area = state->embedded->SplitDropArea();
      POINT drop{area.x + area.width * 92 / 100, area.y + area.height / 2};
      ClientToScreen(state->window->GetWindowHandle(), &drop);
      details->SetInt("drop_x", drop.x); details->SetInt("drop_y", drop.y);
    }
    state->Record(floating_ ? "split_peek_tab_ready" : "split_tab_drag_ready", details);
  }
 private:
  std::weak_ptr<State> state_;
  bool floating_ = false;
  IMPLEMENT_REFCOUNTING(RowObserver);
};
CefRefPtr<CefRegistration> row_observer;
// While a tab is carried out of the floating sidebar: how often it was still shown.
int peek_hidden_samples = 0, peek_drag_samples = 0;
void SamplePeek(const std::shared_ptr<State>& state) {
  if (state->tab_dragging && state->sidebar_peek) { ++peek_drag_samples; if (!state->sidebar_peek->window_visible() || !state->sidebar_peek->is_open()) ++peek_hidden_samples; }
  if (peek_drag_samples < 400 && !state->split_done) Later(state, 20, [](const std::shared_ptr<State>& s) { SamplePeek(s); });
}
void SampleResizes(const std::shared_ptr<State>& state) {
  const auto browser = state->workspace->LiveBrowser(anchor_id);
  if (browser) {
    RECT rect{}; GetWindowRect(browser->GetHost()->GetWindowHandle(), &rect);
    if (samples++ && (rect.right - rect.left != sampled.right - sampled.left || rect.bottom - rect.top != sampled.bottom - sampled.top)) ++resizes;
    sampled = rect;
  }
  if (samples < 330) Later(state, 15, [](const std::shared_ptr<State>& s) { SampleResizes(s); });
}
}  // namespace

void RunSplitProbe(const std::shared_ptr<State>& state);
// Waits until the workspace lists the four fixture pages (it may still be loading).
void WaitForPages(const std::shared_ptr<State>& state) {
  static int attempts = 0;
  const auto snapshot = state->workspace->Snapshot();
  const bool ready = snapshot->GetBool("ready") &&
      !NodeFor(state, "a").empty() && !NodeFor(state, "b").empty() && !NodeFor(state, "c").empty() && !NodeFor(state, "d").empty() && !NodeFor(state, "e").empty();
  if (ready || ++attempts > 40) {
    auto data = CefDictionaryValue::Create(); data->SetBool("ready", ready); data->SetInt("attempts", attempts);
    const auto nodes = snapshot->GetList("nodes");
    data->SetInt("nodes", nodes ? static_cast<int>(nodes->GetSize()) : -1);
    data->SetString("error", snapshot->GetString("error"));
    state->Record("split_probe_pages", data);
    RunSplitProbe(state); return;
  }
  Later(state, 500, [](const std::shared_ptr<State>& s) { WaitForPages(s); });
}

void OnSplitProbeLoad(const std::shared_ptr<State>& state, const std::string& url) {
  static bool started = false;
  if (!state->split_probe || started || !url.ends_with("/split/a") || !state->workspace) return;
  started = true;
  const auto base = url.substr(0, url.size() - 1);
  for (const char* page : {"b", "c", "d", "e"}) state->workspace->OpenLink(base + page, false);
  Later(state, 1000, [](const std::shared_ptr<State>& s) { WaitForPages(s); });
}

void RunSplitProbe(const std::shared_ptr<State>& state) {
  Later(state, 0, [](const std::shared_ptr<State>& s) {
    anchor_id = NodeFor(s, "a");
    auto request = Request("split.create"); request->SetString("id", anchor_id); request->SetString("otherId", NodeFor(s, "b"));
    request->SetBool("vertical", false); Send(s, request, "create", true);
  });
  Later(state, 2000, [](const std::shared_ptr<State>& s) { RecordGeometry(s, "two", 2); });
  Later(state, 2500, [](const std::shared_ptr<State>& s) {
    auto request = Request("split.add"); request->SetString("id", anchor_id); request->SetString("target", "");
    request->SetString("edge", "right"); request->SetString("page", NodeFor(s, "c")); Send(s, request, "add-third");
  });
  Later(state, 4500, [](const std::shared_ptr<State>& s) { RecordGeometry(s, "three", 3); });
  Later(state, 5000, [](const std::shared_ptr<State>& s) {
    auto request = Request("split.add"); request->SetString("id", anchor_id); request->SetString("target", NodeFor(s, "c"));
    request->SetString("edge", "bottom"); request->SetString("page", NodeFor(s, "d")); Send(s, request, "add-fourth");
  });
  Later(state, 7000, [](const std::shared_ptr<State>& s) {
    RecordGeometry(s, "four", 4);
    auto request = Request("split.add"); request->SetString("id", anchor_id); request->SetString("target", "");
    request->SetString("edge", "auto"); request->SetString("page", NodeFor(s, "e")); Send(s, request, "refuse-fifth");
  });
  Later(state, 7500, [](const std::shared_ptr<State>& s) {
    auto request = Request("split.preset"); request->SetString("id", anchor_id); request->SetString("preset", "grid"); Send(s, request, "grid");
  });
  // The experiment drags the divider with the real pointer after this record.
  Later(state, 9500, [](const std::shared_ptr<State>& s) {
    RecordGeometry(s, "grid", 4);
    if (const auto view = SplitView(s)) grid_layout = view->GetString("layout");
    s->Record("split_drag_ready");
    SampleResizes(s);
  });
  Later(state, 15500, [](const std::shared_ptr<State>& s) {
    RecordGeometry(s, "dragged", 4);
    auto data = CefDictionaryValue::Create(); const auto view = SplitView(s);
    data->SetBool("moved", view && view->GetString("layout").ToString() != grid_layout);
    data->SetBool("resizing", view && view->GetBool("resizing"));
    data->SetInt("page_resizes", resizes); data->SetInt("samples", samples);
    s->Record("split_drag_result", data);
  });
  Later(state, 16000, [](const std::shared_ptr<State>& s) {
    auto data = CefDictionaryValue::Create(); const auto before = s->workspace->ActiveNode();
    // In a 2x2 grid every pane has one horizontal and one vertical neighbour.
    const bool across = s->workspace->FocusSplitNeighbour(-1, 0) || s->workspace->FocusSplitNeighbour(1, 0);
    const bool along = s->workspace->FocusSplitNeighbour(0, -1) || s->workspace->FocusSplitNeighbour(0, 1);
    data->SetBool("moved", across && along && s->workspace->ActiveNode() != before);
    data->SetString("before", before); data->SetString("after", s->workspace->ActiveNode());
    s->Record("split_focus_result", data);
  });
  Later(state, 16500, [](const std::shared_ptr<State>& s) { s->workspace->ToggleSplitExpand(); });
  Later(state, 18000, [](const std::shared_ptr<State>& s) { RecordGeometry(s, "expanded", 1); s->workspace->ToggleSplitExpand(); });
  Later(state, 19000, [](const std::shared_ptr<State>& s) {
    RecordGeometry(s, "restored", 4);
    auto request = Request("split.removePane"); request->SetString("id", anchor_id); request->SetString("page", NodeFor(s, "b"));
    Send(s, request, "remove-pane", true);
  });
  Later(state, 21000, [](const std::shared_ptr<State>& s) {
    RecordGeometry(s, "removed", 3);
    auto request = Request("split.remove"); request->SetString("id", anchor_id); Send(s, request, "separate", true);
  });
  Later(state, 23000, [](const std::shared_ptr<State>& s) {
    RecordGeometry(s, "separated", 0);
    auto request = Request("split.create"); request->SetString("id", anchor_id); request->SetString("otherId", NodeFor(s, "b"));
    request->SetBool("vertical", false); Send(s, request, "recreate", true);
  });
  // A real tab drag from the sidebar onto the right pane's lower edge.
  Later(state, 25000, [](const std::shared_ptr<State>& s) {
    const auto shell = s->browsers.find(s->shell_id.load());
    if (shell == s->browsers.end()) { s->Record("split_tab_drag_ready"); return; }
    row_observer = shell->second->GetHost()->AddDevToolsMessageObserver(new RowObserver(s));
    auto params = CefDictionaryValue::Create(); params->SetBool("returnByValue", true);
    params->SetString("expression", "(()=>{const r=document.querySelector('[data-node-id=\"" + NodeFor(s, "e") +
        "\"] .tree-row')?.getBoundingClientRect();return r?JSON.stringify({x:r.x,y:r.y,width:r.width,height:r.height,inner:innerHeight,dpr:devicePixelRatio,url:location.pathname}):''})()");
    shell->second->GetHost()->ExecuteDevToolsMethod(7701, "Runtime.evaluate", params);
  });
  Later(state, 31000, [](const std::shared_ptr<State>& s) {
    RecordGeometry(s, "dropped", 3);
    auto data = CefDictionaryValue::Create(); const auto view = SplitView(s); bool joined = false;
    const auto panes = view ? view->GetList("panes") : nullptr;
    for (size_t i = 0; panes && i < panes->GetSize(); ++i) joined = joined || panes->GetDictionary(i)->GetString("id").ToString() == NodeFor(s, "e");
    data->SetBool("joined", joined); if (view) data->SetString("layout", view->GetString("layout"));
    s->Record("split_tab_drop_result", data);
    row_observer = nullptr;
  });
  // Drag pane A by an empty part of its header onto pane E's right edge.
  Later(state, 31500, [](const std::shared_ptr<State>& s) {
    auto data = CefDictionaryValue::Create(); const auto view = SplitView(s);
    const auto panes = view ? view->GetList("panes") : nullptr;
    const double scale = s->window->GetDisplay()->GetDeviceScaleFactor();
    for (size_t i = 0; panes && i < panes->GetSize(); ++i) {
      const auto pane = panes->GetDictionary(i); const auto bounds = pane->GetDictionary("bounds");
      const auto id = pane->GetString("id").ToString();
      POINT point{};
      if (id == anchor_id) point = {static_cast<LONG>(std::lround((Number(bounds, "x") + 104) * scale)), static_cast<LONG>(std::lround((Number(bounds, "y") + 15) * scale))};
      else if (id == NodeFor(s, "e")) point = {static_cast<LONG>(std::lround((Number(bounds, "x") + Number(bounds, "width") * .93) * scale)),
                                                static_cast<LONG>(std::lround((Number(bounds, "y") + Number(bounds, "height") / 2) * scale))};
      else continue;
      ClientToScreen(s->window->GetWindowHandle(), &point);
      data->SetInt(id == anchor_id ? "from_x" : "to_x", point.x); data->SetInt(id == anchor_id ? "from_y" : "to_y", point.y);
    }
    if (view) data->SetString("layout", view->GetString("layout"));
    s->Record("split_move_ready", data);
  });
  Later(state, 37500, [](const std::shared_ptr<State>& s) {
    RecordGeometry(s, "moved", 3);
    auto data = CefDictionaryValue::Create(); const auto view = SplitView(s);
    const auto a = s->workspace->LiveBrowser(anchor_id), e = s->workspace->LiveBrowser(NodeFor(s, "e"));
    RECT ra{}, re{};
    if (a && e) { GetWindowRect(a->GetHost()->GetWindowHandle(), &ra); GetWindowRect(e->GetHost()->GetWindowHandle(), &re); }
    data->SetBool("a_right_of_e", a && e && ra.left >= re.right);
    data->SetBool("focused_a", s->workspace->ActiveNode() == anchor_id);
    if (view) data->SetString("layout", view->GetString("layout"));
    s->Record("split_move_result", data);
  });
  // The group goes to Pinned first: E must land at the top of Today, not under the pin.
  Later(state, 37700, [](const std::shared_ptr<State>& s) {
    auto request = Request("tree.pin"); request->SetString("id", anchor_id); request->SetBool("value", true); Send(s, request, "pin-group", true);
  });
  // Drag pane E by its header onto the docked sidebar: it leaves the split.
  Later(state, 38000, [](const std::shared_ptr<State>& s) {
    auto data = CefDictionaryValue::Create(); const auto view = SplitView(s);
    const auto panes = view ? view->GetList("panes") : nullptr;
    const double scale = s->window->GetDisplay()->GetDeviceScaleFactor();
    const auto area = s->embedded->SplitDropArea();
    for (size_t i = 0; panes && i < panes->GetSize(); ++i) {
      const auto pane = panes->GetDictionary(i); if (pane->GetString("id").ToString() != NodeFor(s, "e")) continue;
      const auto bounds = pane->GetDictionary("bounds");
      POINT from{static_cast<LONG>(std::lround((Number(bounds, "x") + 104) * scale)), static_cast<LONG>(std::lround((Number(bounds, "y") + 15) * scale))};
      POINT to{area.x - static_cast<LONG>(std::lround(120 * scale)), area.y + area.height / 2};
      ClientToScreen(s->window->GetWindowHandle(), &from); ClientToScreen(s->window->GetWindowHandle(), &to);
      data->SetInt("from_x", from.x); data->SetInt("from_y", from.y); data->SetInt("to_x", to.x); data->SetInt("to_y", to.y);
    }
    s->Record("split_unsplit_ready", data);
  });
  Later(state, 44000, [](const std::shared_ptr<State>& s) {
    RecordGeometry(s, "unsplit", 2);
    auto data = CefDictionaryValue::Create(); const auto view = SplitView(s);
    const auto panes = view ? view->GetList("panes") : nullptr; bool member = false;
    for (size_t i = 0; panes && i < panes->GetSize(); ++i) member = member || panes->GetDictionary(i)->GetString("id").ToString() == NodeFor(s, "e");
    data->SetInt("panes", panes ? static_cast<int>(panes->GetSize()) : 0);
    data->SetBool("left_split", view && !member && !NodeFor(s, "e").empty());
    // Where E is now: the first Today tab, not pinned.
    const auto snapshot = s->workspace->Snapshot(); const auto nodes = snapshot->GetList("nodes");
    int top = INT_MAX, e_position = -1; bool e_pinned = true;
    for (size_t i = 0; nodes && i < nodes->GetSize(); ++i) {
      const auto n = nodes->GetDictionary(i);
      if (n->GetString("kind") != "tab" || n->GetBool("pinned") || n->GetBool("favorite") || !n->GetString("parentId").empty() ||
          (n->HasKey("splitHidden") && n->GetBool("splitHidden"))) continue;
      top = std::min(top, n->GetInt("position"));
      if (n->GetString("id").ToString() == NodeFor(s, "e")) { e_position = n->GetInt("position"); e_pinned = false; }
    }
    data->SetBool("e_at_today_top", !e_pinned && e_position == top);
    s->Record("split_unsplit_result", data);
    s->workspace->SetSidebarVisible(false);
  });
  // With the sidebar hidden, carry the left pane by its header to the window's
  // left edge: the floating sidebar must slide out and take the pane.
  Later(state, 46000, [](const std::shared_ptr<State>& s) {
    auto data = CefDictionaryValue::Create(); const auto view = SplitView(s);
    const auto panes = view ? view->GetList("panes") : nullptr;
    const double scale = s->window->GetDisplay()->GetDeviceScaleFactor();
    if (panes && panes->GetSize()) {
      const auto first = panes->GetDictionary(0); const auto bounds = first->GetDictionary("bounds");
      data->SetString("page", first->GetString("id"));
      POINT from{static_cast<LONG>(std::lround((Number(bounds, "x") + 104) * scale)), static_cast<LONG>(std::lround((Number(bounds, "y") + 15) * scale))};
      POINT edge{static_cast<LONG>(std::lround(3 * scale)), static_cast<LONG>(std::lround((Number(bounds, "y") + Number(bounds, "height") / 2) * scale))};
      POINT inside{static_cast<LONG>(std::lround(120 * scale)), edge.y};
      for (auto* point : {&from, &edge, &inside}) ClientToScreen(s->window->GetWindowHandle(), point);
      data->SetInt("from_x", from.x); data->SetInt("from_y", from.y); data->SetInt("edge_x", edge.x); data->SetInt("edge_y", edge.y);
      data->SetInt("to_x", inside.x); data->SetInt("to_y", inside.y);
    }
    data->SetBool("sidebar_hidden", !s->sidebar_visible);
    s->Record("split_peek_ready", data);
  });
  Later(state, 53000, [](const std::shared_ptr<State>& s) {
    auto data = CefDictionaryValue::Create();
    data->SetBool("split", SplitView(s) != nullptr);
    data->SetBool("sidebar_hidden", !s->sidebar_visible);
    s->Record("split_peek_result", data);
    if (s->sidebar_peek) s->sidebar_peek->Show();
  });
  // A tab dragged out of the floating sidebar onto the page's right edge: the
  // sidebar must slide away while the tab is over the pages, and the drop splits.
  Later(state, 55500, [](const std::shared_ptr<State>& s) {
    const auto peek = s->browsers.find(s->peek_id.load());
    if (peek == s->browsers.end() || !s->sidebar_peek || !s->sidebar_peek->window_visible()) { s->Record("split_peek_tab_ready"); return; }
    row_observer = peek->second->GetHost()->AddDevToolsMessageObserver(new RowObserver(s, true));
    auto params = CefDictionaryValue::Create(); params->SetBool("returnByValue", true);
    params->SetString("expression", "(()=>{const r=document.querySelector('[data-node-id=\"" + NodeFor(s, "c") +
        "\"] .tree-row')?.getBoundingClientRect();return r?JSON.stringify({x:r.x,y:r.y,width:r.width,height:r.height}):''})()");
    peek->second->GetHost()->ExecuteDevToolsMethod(7701, "Runtime.evaluate", params);
    peek_hidden_samples = peek_drag_samples = 0; SamplePeek(s);
  });
  Later(state, 63000, [](const std::shared_ptr<State>& s) {
    auto data = CefDictionaryValue::Create(); const auto view = SplitView(s);
    const auto panes = view ? view->GetList("panes") : nullptr; bool joined = false;
    for (size_t i = 0; panes && i < panes->GetSize(); ++i) joined = joined || panes->GetDictionary(i)->GetString("id").ToString() == NodeFor(s, "c");
    data->SetBool("joined", joined); data->SetInt("drag_samples", peek_drag_samples); data->SetInt("hidden_while_dragging", peek_hidden_samples);
    s->Record("split_peek_tab_result", data);
    row_observer = nullptr; s->workspace->SetSidebarVisible(true);
    s->split_done = true;
  });
}
}  // namespace red
