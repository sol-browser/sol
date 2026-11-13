// sol-tray.exe: Sol in the notification area while no Sol window is open (docs/little-sol.md).
// When Sol's last window closes in background mode, Sol starts this program and exits, so the
// browser, GPU and extension processes stop. It keeps the tray icon and Ctrl+Alt+N and starts
// sol.exe when needed. A starting Sol closes it (WM_CLOSE to its window). No CEF, system DLLs only.
//
// Ctrl+Alt+N here opens this program's own command bar, drawn like Sol's (command-bar.css): it
// shows at once and owns the keyboard until Enter. Only then does Sol start, with the address
// (sol.exe --little-sol-url), and open it in Little Sol; the bar stays, marked "Opening", until
// that window is there and Sol closes this program. No window of Sol's takes part in typing.
// Below the typed row come Google's completions, as in Sol's bar, when Sol says they are on.
#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <shellscalingapi.h>
#include <shlobj.h>
#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>
#include <winhttp.h>
#include <objidl.h>
using std::min; using std::max;
#include <gdiplus.h>
#include "tray/tray_input.h"
#pragma comment(linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

namespace {
constexpr wchar_t kClass[] = L"SolTrayHelper";  // Sol finds this window by class (background_host.cpp).
constexpr wchar_t kBarClass[] = L"SolTrayCommandBar";
constexpr UINT kTrayMessage = WM_APP + 41;
constexpr UINT kTrayId = 1;
constexpr int kHotkeyId = 0x534C;
enum : UINT_PTR { kHotkeyTimer = 1, kDeactivateTimer, kOpeningTimeout, kSuggestTimer };
constexpr UINT kSuggestReply = WM_APP + 43;  // From the request thread: a Reply*.
enum : UINT { kMenuLittleSol = 1, kMenuOpen, kMenuQuit };
// Sol's command bar in DIP (command-bar.css; CommandBar::Reposition and kBaseHeight/kRowHeight).
constexpr int kBarWidth = 720, kBarAnchor = 66 + 6 * 48 + 34;
constexpr int kInputHeight = 1 + 54 + 1, kResultsPadding = 5, kRowHeight = 48, kMaxSuggestions = 3;
struct Rgb { BYTE r, g, b; };
constexpr Rgb kBackground{16, 16, 18}, kBorder{98, 96, 101}, kSeparator{45, 43, 47}, kText{246, 243, 246},
    kIcon{201, 196, 205}, kSelected{77, 53, 76}, kTitle{240, 236, 242}, kDetail{180, 175, 184}, kEnter{57, 55, 60};
constexpr COLORREF Ref(Rgb c) { return RGB(c.r, c.g, c.b); }
Gdiplus::Color Plus(Rgb c) { return Gdiplus::Color(255, c.r, c.g, c.b); }

struct Tray {
  std::wstring directory;  // The folder of sol.exe and this program.
  std::wstring profile;    // "--profile-dir=..." passed on to sol.exe, quoted; empty for the default.
  std::wstring engine = L"google";  // Sol's search engine, given by Sol (--search-engine).
  HWND window = nullptr, bar = nullptr, edit = nullptr;
  HICON icon = nullptr;
  HFONT input_font = nullptr, row_font = nullptr, hint_font = nullptr;
  HBRUSH background = nullptr;
  WNDPROC edit_proc = nullptr;
  HANDLE sol = nullptr;    // The sol.exe this program started last.
  DWORD sol_id = 0;
  UINT taskbar_created = 0;
  double scale = 1;
  bool suggest = false;    // Google completions: Sol's setting (--search-suggestions=1).
  struct Row { std::wstring title, detail; std::string url; bool search = true; };
  std::vector<Row> rows;   // The typed address or search first, then completions.
  int selected = 0;
  std::wstring typed;      // What was typed; arrow keys show a completion without changing it.
  struct Reply { std::wstring query; std::vector<std::wstring> values; };
  Reply suggestions;       // The latest completions and the query they answered.
  unsigned generation = 0; // Of the latest completion request; older replies are dropped.
  bool previewing = false; // The field shows a completion picked with the arrow keys.
  bool hotkey = false, icon_shown = false, opening = false;
  int hotkey_attempts = 0;
} tray;

HICON SolIcon() {
  const std::wstring exe = tray.directory + L"\\sol.exe";
  const int size = GetSystemMetrics(SM_CXSMICON);
  HICON small_icon = nullptr;
  if (SUCCEEDED(SHDefExtractIconW(exe.c_str(), 0, 0, nullptr, &small_icon, MAKELONG(size * 2, size))) && small_icon) return small_icon;
  return LoadIconW(nullptr, IDI_APPLICATION);
}

void ShowIcon() {
  if (!tray.icon) tray.icon = SolIcon();
  NOTIFYICONDATAW data{sizeof(data)};
  data.hWnd = tray.window; data.uID = kTrayId; data.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
  data.uCallbackMessage = kTrayMessage; data.hIcon = tray.icon;
  wcscpy_s(data.szTip, tray.hotkey ? L"Sol — Ctrl+Alt+N opens Little Sol" : L"Sol");
  const DWORD action = tray.icon_shown ? NIM_MODIFY : NIM_ADD;
  if (!Shell_NotifyIconW(action, &data)) return;
  if (!tray.icon_shown) { data.uVersion = NOTIFYICON_VERSION_4; Shell_NotifyIconW(NIM_SETVERSION, &data); }
  tray.icon_shown = true;
}

void HideIcon() {
  if (!tray.icon_shown) return;
  NOTIFYICONDATAW data{sizeof(data)};
  data.hWnd = tray.window; data.uID = kTrayId;
  Shell_NotifyIconW(NIM_DELETE, &data);
  tray.icon_shown = false;
}

// The Sol that just exited may still hold Ctrl+Alt+N for a moment: try again for a few seconds.
void TakeHotkey() {
  if (tray.hotkey) return;
  tray.hotkey = RegisterHotKey(tray.window, kHotkeyId, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, 'N') != FALSE;
  if (tray.hotkey) { KillTimer(tray.window, kHotkeyTimer); ShowIcon(); return; }
  if (++tray.hotkey_attempts < 40) SetTimer(tray.window, kHotkeyTimer, 250, nullptr);
  else KillTimer(tray.window, kHotkeyTimer);
}

bool SolStarting() { return tray.sol && WaitForSingleObject(tray.sol, 0) == WAIT_TIMEOUT; }

bool Launch(const std::wstring& switches) {
  // A Sol this program started is still running (starting up): it handles what was asked.
  if (SolStarting()) return false;
  if (tray.sol) { CloseHandle(tray.sol); tray.sol = nullptr; tray.sol_id = 0; }
  const std::wstring exe = tray.directory + L"\\sol.exe";
  std::wstring line = L"\"" + exe + L"\" " + switches;
  if (!tray.profile.empty()) line += L" " + tray.profile;
  STARTUPINFOW startup{sizeof(startup)};
  PROCESS_INFORMATION process{};
  if (!CreateProcessW(exe.c_str(), line.data(), nullptr, nullptr, FALSE, 0, nullptr, tray.directory.c_str(), &startup, &process)) return false;
  // The hotkey or the click made this program foreground-eligible; Sol's window may take it over.
  AllowSetForegroundWindow(process.dwProcessId);
  CloseHandle(process.hThread);
  tray.sol = process.hProcess; tray.sol_id = process.dwProcessId;
  // Sol closes this program once it starts; until then the icon stays, in case it cannot start.
  return true;
}

int Px(double dip) { return static_cast<int>(dip * tray.scale + .5); }

HFONT Font(int dip) {
  return CreateFontW(-Px(dip), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                     CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
}

// Where Sol's detached bar opens: centred on the work area of the display under the cursor.
RECT BarBounds() {
  POINT cursor{};
  GetCursorPos(&cursor);
  const HMONITOR monitor = MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST);
  MONITORINFO info{sizeof(info)};
  GetMonitorInfoW(monitor, &info);
  UINT dpi_x = 96, dpi_y = 96;
  if (FAILED(GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpi_x, &dpi_y))) dpi_x = 96;
  tray.scale = dpi_x / 96.0;
  const RECT& area = info.rcWork;
  const int area_width = static_cast<int>((area.right - area.left) / tray.scale);
  const int area_height = static_cast<int>((area.bottom - area.top) / tray.scale);
  const int width = std::max(1, std::min(kBarWidth, area_width - 32));
  const int anchor = std::max(1, std::min(kBarAnchor, area_height - 76));
  const int x = area.left + Px((area_width - width) / 2), y = area.top + Px((area_height - anchor) / 2);
  return {x, y, x + Px(width), y + Px(kInputHeight)};
}

void Fonts() {
  for (HFONT* font : {&tray.input_font, &tray.row_font, &tray.hint_font}) if (*font) DeleteObject(*font);
  tray.input_font = Font(14); tray.row_font = Font(13); tray.hint_font = Font(10);
  SendMessageW(tray.edit, WM_SETFONT, reinterpret_cast<WPARAM>(tray.input_font), TRUE);
}

// The input row's text field, after the search icon (row margin 8 + padding 8, icon 17, gap 10).
void PlaceEdit() {
  RECT client{};
  GetClientRect(tray.bar, &client);
  const int height = Px(20), left = Px(1 + 16 + 17 + 10);
  MoveWindow(tray.edit, left, (Px(kInputHeight) - height) / 2, client.right - left - Px(17), height, TRUE);
}

std::wstring Trimmed(const std::wstring& text) {
  const auto first = text.find_first_not_of(L' ');
  return first == std::wstring::npos ? std::wstring() : text.substr(first, text.find_last_not_of(L' ') - first + 1);
}

// The rows of Sol's bar for this text: the typed address or search, then up to three Google
// completions (command-model.ts) that differ from it. While the next reply is pending, the
// previous completions that still start with the text stay.
void BuildRows() {
  tray.rows.clear();
  const auto destination = sol_tray::ResolveInput(tray.typed, tray.engine);
  if (!destination) return;
  tray.rows.push_back({destination->title, destination->detail, destination->url, destination->search});
  if (!destination->search || !tray.suggest || tray.engine != L"google") return;
  const auto query = Trimmed(tray.typed);
  const auto lower = sol_tray::Lower(query);
  std::vector<std::wstring> seen{lower};
  for (const auto& value : tray.suggestions.values) {
    if (tray.rows.size() > static_cast<size_t>(kMaxSuggestions)) break;
    const auto normalized = sol_tray::Lower(Trimmed(value));
    if (normalized.empty() || std::find(seen.begin(), seen.end(), normalized) != seen.end()) continue;
    if (tray.suggestions.query != query && !normalized.starts_with(lower)) continue;
    seen.push_back(normalized);
    tray.rows.push_back({value, L"", "https://www.google.com/search?q=" + sol_tray::EncodeComponent(value), true});
  }
}

void Resize() {
  if (tray.selected >= static_cast<int>(tray.rows.size())) tray.selected = 0;
  RECT bounds{};
  GetWindowRect(tray.bar, &bounds);
  const int rows = static_cast<int>(tray.rows.size());
  const int height = Px(kInputHeight + (rows ? 2 * kResultsPadding + rows * kRowHeight : 0));
  if (bounds.bottom - bounds.top != height)
    SetWindowPos(tray.bar, nullptr, 0, 0, bounds.right - bounds.left, height, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
  InvalidateRect(tray.bar, nullptr, FALSE);
}

// Typing: the rows follow the text, and completions are asked for once typing pauses (180 ms).
void Typed() {
  wchar_t text[4097]{};
  GetWindowTextW(tray.edit, text, 4097);
  tray.typed = text;
  tray.selected = 0;
  BuildRows();
  Resize();
  KillTimer(tray.bar, kSuggestTimer);
  if (tray.rows.size() && tray.rows[0].search && tray.suggest && tray.engine == L"google" && sol_tray::ValidSuggestionQuery(Trimmed(tray.typed)))
    SetTimer(tray.bar, kSuggestTimer, 180, nullptr);
}

// suggestqueries.google.com, as navigation/search_suggestions.cpp: HTTPS only, no cookies,
// credentials or redirects, at most 32 KB, given up after 3 s. On its own thread.
void RequestSuggestions() {
  const auto query = Trimmed(tray.typed);
  if (!sol_tray::ValidSuggestionQuery(query)) return;
  const unsigned generation = ++tray.generation;
  std::thread([window = tray.bar, query, generation] {
    auto reply = std::make_unique<Tray::Reply>();
    reply->query = query;
    const HINTERNET session = WinHttpOpen(L"Sol", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (session) {
      WinHttpSetTimeouts(session, 3000, 3000, 3000, 3000);
      if (const HINTERNET connection = WinHttpConnect(session, L"suggestqueries.google.com", INTERNET_DEFAULT_HTTPS_PORT, 0)) {
        const std::wstring path = L"/complete/search?client=firefox&ie=utf-8&oe=utf-8&q=" + [&] {
          const auto encoded = sol_tray::EncodeComponent(query);
          return std::wstring(encoded.begin(), encoded.end());
        }();
        if (const HINTERNET request = WinHttpOpenRequest(connection, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER,
                                                         WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE)) {
          DWORD features = WINHTTP_DISABLE_COOKIES | WINHTTP_DISABLE_REDIRECTS | WINHTTP_DISABLE_AUTHENTICATION;
          WinHttpSetOption(request, WINHTTP_OPTION_DISABLE_FEATURE, &features, sizeof(features));
          DWORD status = 0, size = sizeof(status);
          if (WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
              WinHttpReceiveResponse(request, nullptr) &&
              WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX) &&
              status == 200) {
            std::string body;
            char buffer[4096];
            DWORD read = 0;
            while (WinHttpReadData(request, buffer, sizeof(buffer), &read) && read && body.size() <= 32768) body.append(buffer, read);
            reply->values = sol_tray::ParseSuggestions(body);
          }
          WinHttpCloseHandle(request);
        }
        WinHttpCloseHandle(connection);
      }
      WinHttpCloseHandle(session);
    }
    if (PostMessageW(window, kSuggestReply, generation, reinterpret_cast<LPARAM>(reply.get()))) reply.release();
  }).detach();
}

// Arrow keys: another row; the field shows a completion's text, the typed text for the first row.
void Select(int step) {
  if (tray.rows.empty() || tray.opening) return;
  const int count = static_cast<int>(tray.rows.size());
  tray.selected = (tray.selected + step + count) % count;
  const auto& shown = tray.selected == 0 ? tray.typed : tray.rows[tray.selected].title;
  tray.previewing = true;
  SetWindowTextW(tray.edit, shown.c_str());
  tray.previewing = false;
  SendMessageW(tray.edit, EM_SETSEL, shown.size(), shown.size());
  InvalidateRect(tray.bar, nullptr, FALSE);
}

void HideBar() {
  if (!tray.bar) return;
  KillTimer(tray.bar, kDeactivateTimer);
  KillTimer(tray.bar, kOpeningTimeout);
  tray.opening = false;
  ShowWindow(tray.bar, SW_HIDE);
}

// Ctrl+Alt+N: the bar at once. Sol starts only for what is entered.
void NewLittleSol() {
  if (!tray.bar) { Launch(L"--workspace"); return; }
  if (!IsWindowVisible(tray.bar)) {
    const RECT bounds = BarBounds();
    tray.opening = false;
    tray.rows.clear(); tray.typed.clear(); tray.selected = 0; tray.suggestions = {};
    Fonts();
    SetWindowPos(tray.bar, HWND_TOPMOST, bounds.left, bounds.top, bounds.right - bounds.left, bounds.bottom - bounds.top, SWP_NOACTIVATE);
    SetWindowTextW(tray.edit, L"");
    EnableWindow(tray.edit, TRUE);
    PlaceEdit();
  }
  ShowWindow(tray.bar, SW_SHOW);
  SetForegroundWindow(tray.bar);
  SetFocus(tray.edit);
}

void Submit(int index) {
  if (tray.opening || index < 0 || index >= static_cast<int>(tray.rows.size())) return;
  tray.selected = index;
  const auto& url = tray.rows[index].url;
  KillTimer(tray.bar, kSuggestTimer);
  // Quoted as one argument; the address has no quote (tray_input.h escapes it).
  if (!Launch(L"--workspace \"--little-sol-url=" + std::wstring(url.begin(), url.end()) + L"\"")) {
    if (SolStarting()) return;  // A Sol is already on its way; let it finish first.
    HideBar(); return;
  }
  tray.opening = true;
  EnableWindow(tray.edit, FALSE);
  InvalidateRect(tray.bar, nullptr, FALSE);
  SetTimer(tray.bar, kOpeningTimeout, 20000, nullptr);  // Sol never came: give up quietly.
}

void DrawText(HDC dc, HFONT font, Rgb color, const std::wstring& text, RECT box, UINT format) {
  SelectObject(dc, font);
  SetTextColor(dc, Ref(color));
  DrawTextW(dc, text.c_str(), static_cast<int>(text.size()), &box, format | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
}

void PaintBar(HDC target) {
  RECT client{};
  GetClientRect(tray.bar, &client);
  // Drawn off screen: no flicker while the row comes and goes.
  const HDC dc = CreateCompatibleDC(target);
  const HBITMAP bitmap = CreateCompatibleBitmap(target, client.right, client.bottom);
  const HGDIOBJ old_bitmap = SelectObject(dc, bitmap);
  FillRect(dc, &client, tray.background);
  SetBkMode(dc, TRANSPARENT);
  Gdiplus::Graphics graphics(dc);
  graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
  // Lucide icons: 24-unit boxes drawn 17 DIP wide with a 1.6-unit stroke (command-bar.css).
  const float unit = static_cast<float>(17 * tray.scale / 24);
  Gdiplus::Pen pen(Plus(kIcon), 1.6f * unit);
  pen.SetStartCap(Gdiplus::LineCapRound); pen.SetEndCap(Gdiplus::LineCapRound);
  const auto search_icon = [&](float left, float top) {
    graphics.DrawEllipse(&pen, left + 3 * unit, top + 3 * unit, 16 * unit, 16 * unit);
    graphics.DrawLine(&pen, left + 16.65f * unit, top + 16.65f * unit, left + 21 * unit, top + 21 * unit);
  };
  search_icon(static_cast<float>(Px(1 + 16)), (Px(kInputHeight) - static_cast<float>(17 * tray.scale)) / 2);
  // The separator under the input row.
  RECT line{Px(1 + 8), Px(kInputHeight - 1), client.right - Px(1 + 8), Px(kInputHeight)};
  const HBRUSH separator = CreateSolidBrush(Ref(kSeparator));
  if (!tray.rows.empty()) FillRect(dc, &line, separator);
  DeleteObject(separator);
  const auto rounded = [](Gdiplus::GraphicsPath& path, float left, float top, float width, float height, float radius) {
    path.AddArc(left, top, radius * 2, radius * 2, 180, 90);
    path.AddArc(left + width - radius * 2, top, radius * 2, radius * 2, 270, 90);
    path.AddArc(left + width - radius * 2, top + height - radius * 2, radius * 2, radius * 2, 0, 90);
    path.AddArc(left, top + height - radius * 2, radius * 2, radius * 2, 90, 90);
    path.CloseFigure();
  };
  for (int index = 0; index < static_cast<int>(tray.rows.size()); ++index) {
    const auto& item = tray.rows[index];
    const bool selected = index == tray.selected;
    const float top = static_cast<float>(Px(kInputHeight + kResultsPadding + index * kRowHeight)), left = static_cast<float>(Px(1 + 5));
    const float width = static_cast<float>(client.right - Px(1 + 5)) - left, height = static_cast<float>(Px(kRowHeight));
    if (selected) {
      Gdiplus::GraphicsPath row;
      rounded(row, left, top, width, height, static_cast<float>(Px(7)));
      Gdiplus::SolidBrush brush(Plus(kSelected));
      graphics.FillPath(&brush, &row);
    }
    const float icon_left = left + Px(14) + Px(3.5), icon_top = top + (height - static_cast<float>(17 * tray.scale)) / 2;
    if (item.search) search_icon(icon_left, icon_top);
    else {
      // Lucide "globe": a circle, a meridian and the equator.
      graphics.DrawEllipse(&pen, icon_left + 2 * unit, icon_top + 2 * unit, 20 * unit, 20 * unit);
      graphics.DrawEllipse(&pen, icon_left + 7.5f * unit, icon_top + 2 * unit, 9 * unit, 20 * unit);
      graphics.DrawLine(&pen, icon_left + 2 * unit, icon_top + 12 * unit, icon_left + 22 * unit, icon_top + 12 * unit);
    }
    const int text_left = static_cast<int>(left) + Px(14 + 24 + 12);
    const int hint = Px(21), hint_right = client.right - Px(1 + 5 + 14);
    RECT title{text_left, static_cast<int>(top), hint_right - hint - Px(12), static_cast<int>(top + height)};
    const std::wstring detail = selected && tray.opening ? std::wstring(L" — Opening in Little Sol…")
        : item.detail.empty() ? std::wstring() : L" — " + item.detail;
    SelectObject(dc, tray.row_font);
    SIZE detail_size{};
    if (!detail.empty()) GetTextExtentPoint32W(dc, detail.c_str(), static_cast<int>(detail.size()), &detail_size);
    RECT title_box = title;
    title_box.right = std::max(title.left, title.right - detail_size.cx);
    DrawText(dc, tray.row_font, kTitle, item.title, title_box, DT_LEFT | DT_END_ELLIPSIS);
    if (!detail.empty()) {
      RECT measured = title_box;
      SelectObject(dc, tray.row_font);
      DrawTextW(dc, item.title.c_str(), static_cast<int>(item.title.size()), &measured, DT_LEFT | DT_SINGLELINE | DT_CALCRECT | DT_NOPREFIX | DT_END_ELLIPSIS);
      RECT detail_box{std::min(measured.right, title_box.right), title.top, title.right, title.bottom};
      DrawText(dc, tray.row_font, kDetail, detail, detail_box, DT_LEFT | DT_END_ELLIPSIS);
    }
    if (!selected) continue;
    // The Enter key hint at the right end of the selected row.
    const float hint_top = top + (height - hint) / 2, key_left = static_cast<float>(hint_right - hint);
    Gdiplus::GraphicsPath key;
    rounded(key, key_left, hint_top, static_cast<float>(hint), static_cast<float>(hint), static_cast<float>(Px(3)));
    Gdiplus::SolidBrush key_brush(Plus(kEnter));
    graphics.FillPath(&key_brush, &key);
    Gdiplus::Pen arrow(Plus(kDetail), 1.4f * unit * 14 / 17);
    arrow.SetStartCap(Gdiplus::LineCapRound); arrow.SetEndCap(Gdiplus::LineCapRound);
    const float a = static_cast<float>(14 * tray.scale / 24), ax = key_left + (hint - 14 * static_cast<float>(tray.scale)) / 2,
        ay = hint_top + (hint - 14 * static_cast<float>(tray.scale)) / 2;
    // Lucide "corner-down-left": polyline 9,10 4,15 9,20 and the path from 20,4 down to 4,15.
    graphics.DrawLine(&arrow, ax + 9 * a, ay + 10 * a, ax + 4 * a, ay + 15 * a);
    graphics.DrawLine(&arrow, ax + 4 * a, ay + 15 * a, ax + 9 * a, ay + 20 * a);
    graphics.DrawLine(&arrow, ax + 20 * a, ay + 4 * a, ax + 20 * a, ay + 11 * a);
    graphics.DrawArc(&arrow, ax + 12 * a, ay + 7 * a, 8 * a, 8 * a, 0, 90);
    graphics.DrawLine(&arrow, ax + 16 * a, ay + 15 * a, ax + 4 * a, ay + 15 * a);
  }
  BitBlt(target, 0, 0, client.right, client.bottom, dc, 0, 0, SRCCOPY);
  SelectObject(dc, old_bitmap);
  DeleteObject(bitmap);
  DeleteDC(dc);
}

LRESULT CALLBACK EditProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
  if (message == WM_KEYDOWN && wparam == VK_ESCAPE) { HideBar(); return 0; }
  if (message == WM_KEYDOWN && wparam == VK_RETURN) { Submit(tray.selected); return 0; }
  if (message == WM_KEYDOWN && (wparam == VK_DOWN || wparam == VK_UP)) { Select(wparam == VK_DOWN ? 1 : -1); return 0; }
  if (message == WM_CHAR && (wparam == '\r' || wparam == 27 || wparam == '\t')) return 0;  // No beep.
  return CallWindowProcW(tray.edit_proc, window, message, wparam, lparam);
}

LRESULT CALLBACK BarProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
  switch (message) {
    case WM_PAINT: {
      PAINTSTRUCT paint;
      const HDC dc = BeginPaint(window, &paint);
      PaintBar(dc);
      EndPaint(window, &paint);
      return 0;
    }
    case WM_ERASEBKGND:
      return 1;
    case WM_COMMAND:
      if (HIWORD(wparam) == EN_CHANGE && !tray.opening && !tray.previewing) Typed();
      return 0;
    case WM_CTLCOLOREDIT: case WM_CTLCOLORSTATIC: {
      const HDC dc = reinterpret_cast<HDC>(wparam);
      SetTextColor(dc, Ref(kText));
      SetBkColor(dc, Ref(kBackground));
      return reinterpret_cast<LRESULT>(tray.background);
    }
    case WM_LBUTTONUP:
      // A click on a result row opens it, as in Sol's bar.
      if (const int y = GET_Y_LPARAM(lparam) - Px(kInputHeight + kResultsPadding); y >= 0) Submit(y / Px(kRowHeight));
      return 0;
    case kSuggestReply: {
      std::unique_ptr<Tray::Reply> reply(reinterpret_cast<Tray::Reply*>(lparam));
      if (static_cast<unsigned>(wparam) != tray.generation || tray.opening || !IsWindowVisible(window)) return 0;
      // Keep the chosen row when it is still there (as commandSelectionIndex).
      const auto chosen = tray.selected > 0 && tray.selected < static_cast<int>(tray.rows.size()) ? tray.rows[tray.selected].title : std::wstring();
      tray.suggestions = std::move(*reply);
      BuildRows();
      tray.selected = 0;
      for (int i = 1; i < static_cast<int>(tray.rows.size()); ++i) if (!chosen.empty() && tray.rows[i].title == chosen) tray.selected = i;
      Resize();
      return 0;
    }
    case WM_ACTIVATE:
      // Another app took the focus, like a click outside Sol's bar. While opening, Sol's own
      // windows come up and take it: that is expected.
      if (LOWORD(wparam) == WA_INACTIVE) { if (!tray.opening) SetTimer(window, kDeactivateTimer, 80, nullptr); }
      else if (!tray.opening) SetFocus(tray.edit);
      return 0;
    case WM_TIMER:
      KillTimer(window, wparam);
      if (wparam == kOpeningTimeout) HideBar();
      if (wparam == kSuggestTimer && !tray.opening) RequestSuggestions();
      if (wparam == kDeactivateTimer && !tray.opening && GetForegroundWindow() != window) HideBar();
      return 0;
    default:
      return DefWindowProcW(window, message, wparam, lparam);
  }
}

void CreateBar(HINSTANCE instance) {
  WNDCLASSEXW type{sizeof(type)};
  type.lpfnWndProc = BarProc; type.hInstance = instance; type.lpszClassName = kBarClass;
  type.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  if (!RegisterClassExW(&type)) return;
  tray.background = CreateSolidBrush(Ref(kBackground));
  tray.bar = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, kBarClass, L"Sol — Command bar", WS_POPUP | WS_CLIPCHILDREN,
                             0, 0, 1, 1, nullptr, nullptr, instance, nullptr);
  if (!tray.bar) return;
  // Rounded by Windows with one outline in Sol's colour, as Sol's bar (command_bar.cpp).
  const DWM_WINDOW_CORNER_PREFERENCE corners = DWMWCP_ROUND;
  DwmSetWindowAttribute(tray.bar, DWMWA_WINDOW_CORNER_PREFERENCE, &corners, sizeof(corners));
  const COLORREF border = Ref(kBorder);
  DwmSetWindowAttribute(tray.bar, DWMWA_BORDER_COLOR, &border, sizeof(border));
  tray.edit = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 0, 0, 1, 1, tray.bar, nullptr, instance, nullptr);
  if (!tray.edit) { DestroyWindow(tray.bar); tray.bar = nullptr; return; }
  SendMessageW(tray.edit, EM_SETLIMITTEXT, 4096, 0);
  SendMessageW(tray.edit, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"Search or Enter URL…"));
  tray.edit_proc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(tray.edit, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(EditProc)));
}

void ShowMenu() {
  HMENU menu = CreatePopupMenu();
  if (!menu) return;
  AppendMenuW(menu, MF_STRING, kMenuLittleSol, tray.hotkey ? L"New Little Sol\tCtrl+Alt+N" : L"New Little Sol");
  AppendMenuW(menu, MF_STRING, kMenuOpen, L"Open Sol");
  AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(menu, MF_STRING, kMenuQuit, L"Quit Sol");
  SetMenuDefaultItem(menu, kMenuOpen, FALSE);
  POINT point{};
  GetCursorPos(&point);
  SetForegroundWindow(tray.window);  // Without this the menu does not close on an outside click (KB135788).
  const UINT chosen = TrackPopupMenuEx(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY, point.x, point.y, tray.window, nullptr);
  PostMessageW(tray.window, WM_NULL, 0, 0);
  DestroyMenu(menu);
  if (chosen == kMenuLittleSol) NewLittleSol();
  else if (chosen == kMenuOpen) Launch(L"--workspace");
  else if (chosen == kMenuQuit) DestroyWindow(tray.window);
}

LRESULT CALLBACK Proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
  switch (message) {
    case WM_HOTKEY:
      if (wparam == kHotkeyId) NewLittleSol();
      return 0;
    case WM_TIMER:
      if (wparam == kHotkeyTimer) TakeHotkey();
      return 0;
    case kTrayMessage:
      switch (LOWORD(lparam)) {
        case NIN_SELECT: case NIN_KEYSELECT: Launch(L"--workspace"); break;
        case WM_CONTEXTMENU: ShowMenu(); break;
        default: break;
      }
      return 0;
    case WM_DESTROY:
      if (tray.hotkey) UnregisterHotKey(window, kHotkeyId);
      HideIcon();
      if (tray.bar) DestroyWindow(tray.bar);
      PostQuitMessage(0);
      return 0;
    default:
      if (message == tray.taskbar_created && tray.taskbar_created) { tray.icon_shown = false; ShowIcon(); return 0; }
      return DefWindowProcW(window, message, wparam, lparam);
  }
}

// --profile-dir (passed on to sol.exe), --search-engine and --search-suggestions (this bar's
// searches and Google completions, as in Sol's settings), from Sol.
void ReadSwitches() {
  int count = 0;
  wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &count);
  if (!argv) return;
  for (int i = 1; i < count; ++i) {
    const std::wstring arg(argv[i]);
    // Quoted as one argument; a path cannot contain a quote.
    if (arg.starts_with(L"--profile-dir=") && arg.find(L'"') == std::wstring::npos) tray.profile = L"\"" + arg + L"\"";
    if (arg.starts_with(L"--search-engine=")) tray.engine = arg.substr(16);
    if (arg == L"--search-suggestions=1") tray.suggest = true;
  }
  LocalFree(argv);
}
}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
  // One per signed-in user (the profile switch is not part of the name: Sol has one tray icon).
  const HANDLE single = CreateMutexW(nullptr, TRUE, L"Local\\SolTrayHelper");
  if (!single || GetLastError() == ERROR_ALREADY_EXISTS) return 0;
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);  // The bar matches Sol's in pixels.
  wchar_t path[MAX_PATH]{};
  const DWORD length = GetModuleFileNameW(nullptr, path, MAX_PATH);
  if (!length || length >= MAX_PATH) return 1;
  tray.directory.assign(path, length);
  tray.directory.resize(tray.directory.find_last_of(L'\\'));
  ReadSwitches();

  WNDCLASSEXW type{sizeof(type)};
  type.lpfnWndProc = Proc; type.hInstance = instance; type.lpszClassName = kClass;
  if (!RegisterClassExW(&type)) return 1;
  // A hidden top-level window (not message-only): the tray menu needs a window that can be foreground.
  tray.window = CreateWindowExW(WS_EX_TOOLWINDOW, kClass, L"Sol", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, instance, nullptr);
  if (!tray.window) return 1;
  tray.taskbar_created = RegisterWindowMessageW(L"TaskbarCreated");
  ShowIcon();
  if (!tray.icon_shown) { DestroyWindow(tray.window); return 1; }  // Without the icon nothing could quit it.
  TakeHotkey();
  Gdiplus::GdiplusStartupInput gdiplus_input;
  ULONG_PTR gdiplus = 0;
  Gdiplus::GdiplusStartup(&gdiplus, &gdiplus_input, nullptr);
  CreateBar(instance);  // Without it, Ctrl+Alt+N opens Sol instead.

  MSG message;
  while (GetMessageW(&message, nullptr, 0, 0) > 0) {
    TranslateMessage(&message);
    DispatchMessageW(&message);
  }
  if (tray.icon) DestroyIcon(tray.icon);
  for (HFONT font : {tray.input_font, tray.row_font, tray.hint_font}) if (font) DeleteObject(font);
  if (tray.background) DeleteObject(tray.background);
  if (tray.sol) CloseHandle(tray.sol);
  if (gdiplus) Gdiplus::GdiplusShutdown(gdiplus);
  ReleaseMutex(single);
  CloseHandle(single);
  return 0;
}
