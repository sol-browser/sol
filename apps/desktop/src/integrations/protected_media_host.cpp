#include "integrations/protected_media_host.h"
#include "integrations/protected_media_layout.h"
#include "integrations/protected_media_release.h"

#include <objbase.h>
#include <windowsx.h>
#include <sddl.h>
#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <initializer_list>
#include <string_view>
#include <vector>
#include "navigation/shortcuts.h"
#include "include/base/cef_callback.h"
#include "include/cef_parser.h"
#include "include/wrapper/cef_closure_task.h"

namespace red {
namespace {
constexpr wchar_t kExecutable[] = L"SolProtectedMedia.exe";
// Hosts were read from each service's public pages in the browser pane on
// 2026-11-23 (docs/implementation-plan.md); Tidal's are unverified (HTTP 403 there).
constexpr char kAmazonSignIn[] =
    "www.amazon.com www.amazon.co.uk www.amazon.de www.amazon.fr www.amazon.it www.amazon.es www.amazon.nl "
    "www.amazon.se www.amazon.pl www.amazon.com.be www.amazon.ca www.amazon.com.mx www.amazon.com.br "
    "www.amazon.co.jp www.amazon.in www.amazon.com.au www.amazon.sg www.amazon.ae www.amazon.sa "
    "www.amazon.com.tr na.account.amazon.com eu.account.amazon.com fe.account.amazon.com";
constexpr char kAppleSignIn[] = "idmsa.apple.com appleid.apple.com";
constexpr ProtectedMediaService kServices[] = {
    {"spotify", "Spotify", "open.spotify.com", "", "accounts.spotify.com", "spotify.com", "",
     "open.spotifycdn.com", "/cdn/images/favicon", "audio", true,
     "toggle=control-button-playpause previous=control-button-skip-back next=control-button-skip-forward "
     "shuffle=control-button-shuffle repeat=control-button-repeat progress=playback-progressbar "
     "position=playback-position duration=playback-duration title=context-item-info-title"},
    {"netflix", "Netflix", "www.netflix.com", "", "", "netflix.com", "netflix.com",
     "assets.nflxext.com", "/us/ffe/siteui/common/icons/", "video", false, ""},
    {"disneyplus", "Disney+", "www.disneyplus.com", "", "", "disneyplus.com", "disneyplus.com",
     "static-assets.bamgrid.com", "/product/disneyplus/favicons/", "video", false, ""},
    {"hbomax", "HBO Max", "www.hbomax.com", "play.hbomax.com", "auth.hbomax.com", "hbomax.com", "hbomax.com",
     "www.hbomax.com", "/dotcom/img/hbomax/", "video", false, ""},
    {"primevideo", "Prime Video", "www.primevideo.com", "", kAmazonSignIn, "primevideo.com", "primevideo.com",
     "m.media-amazon.com", "/images/G/01/digital/video/DVUI/favicons/", "video", false, ""},
    {"appletv", "Apple TV", "tv.apple.com", "", kAppleSignIn, "", "",
     "tv.apple.com", "/assets/favicon/", "video", false, ""},
    {"applemusic", "Apple Music", "music.apple.com", "", kAppleSignIn, "", "",
     "music.apple.com", "/assets/favicon/", "audio, video", false, ""},
    {"paramountplus", "Paramount+", "www.paramountplus.com", "", "", "paramountplus.com", "paramountplus.com",
     "www.paramountplus.com", "/", "video", false, ""},
    {"crunchyroll", "Crunchyroll", "www.crunchyroll.com", "", "sso.crunchyroll.com", "crunchyroll.com", "crunchyroll.com",
     "static.crunchyroll.com", "/cr-acquisition/assets/img/favicons/", "video", false, ""},
    {"deezer", "Deezer", "www.deezer.com", "", "account.deezer.com", "deezer.com", "deezer.com",
     "cdn-files.dzcdn.net", "/cache/images/common/favicon/", "audio", false, ""},
    {"tidal", "TIDAL", "listen.tidal.com", "", "login.tidal.com", "tidal.com", "",
     "listen.tidal.com", "/", "audio", false, ""},
};

// The canonical host of an https URL without credentials on the default port, else empty.
std::string WebHost(const std::string& url) {
  CefURLParts p;
  if (url.size() > 4096 || !CefParseURL(url, p) || CefString(&p.scheme) != "https" ||
      !(CefString(&p.port).empty() || CefString(&p.port) == "443") || p.username.length || p.password.length) return {};
  return CefString(&p.host).ToString();
}
bool OnOrigin(const std::string& url, const char* host) {
  const auto found = WebHost(url);
  return !found.empty() && found == host;
}
// A page that opens in the helper for |service|.
bool PlayerHost(const ProtectedMediaService& service, std::string_view host) {
  return !host.empty() && (host == service.player_host || ProtectedMediaHostListed(host, service.other_hosts));
}
bool OnPlayer(const ProtectedMediaService& service, const std::string& url) { return PlayerHost(service, WebHost(url)); }
const ProtectedMediaService* ServiceForHost(std::string_view host) {
  for (const auto& service : kServices)
    if (PlayerHost(service, host) || (*service.alias && host == service.alias)) return &service;
  return nullptr;
}
CefRefPtr<CefListValue> HostList(std::initializer_list<const char*> lists) {
  auto result = CefListValue::Create();
  for (const char* list : lists) {
    std::string_view rest = list;
    while (!rest.empty()) {
      const auto space = rest.find(' ');
      result->SetString(result->GetSize(), std::string(rest.substr(0, space)));
      if (space == std::string_view::npos) break;
      rest.remove_prefix(space + 1);
    }
  }
  return result;
}
// What the helper needs to know about |service| (sol-browser/streaming-drm, app/main.cjs parseService).
CefRefPtr<CefDictionaryValue> Describe(const ProtectedMediaService& service) {
  auto value = CefDictionaryValue::Create();
  value->SetString("id", service.id);
  value->SetList("players", HostList({service.player_host, service.other_hosts}));
  value->SetList("signIn", HostList({service.sign_in_hosts}));
  value->SetString("iconHost", service.icon_host);
  value->SetString("iconPath", service.icon_path);
  value->SetString("media", service.media);
  value->SetBool("seek", service.seek);
  auto controls = CefDictionaryValue::Create();
  std::string_view rest = service.controls;
  while (!rest.empty()) {
    const auto space = rest.find(' ');
    const auto pair = rest.substr(0, space);
    if (const auto equals = pair.find('='); equals != std::string_view::npos)
      controls->SetString(std::string(pair.substr(0, equals)), std::string(pair.substr(equals + 1)));
    if (space == std::string_view::npos) break;
    rest.remove_prefix(space + 1);
  }
  value->SetDictionary("controls", controls);
  return value;
}

void ShapeSurface(ProtectedMediaHost::Tab& tab, const RECT& bounds) {
  if (bounds.right <= 0 || bounds.bottom <= 0) return;
  const int diameter = MulDiv(tab.corner_radius * 2, GetDpiForWindow(tab.parent), 96);
  if (tab.applied_diameter == diameter && (!diameter ||
      (tab.shaped_size.cx == bounds.right && tab.shaped_size.cy == bounds.bottom))) return;
  const HRGN region = diameter > 0 ? CreateRoundRectRgn(0, 0, bounds.right + 1, bounds.bottom + 1, diameter, diameter) : nullptr;
  if (diameter > 0 && !region) return;
  if (SetWindowRgn(tab.child, region, TRUE)) {
    tab.applied_diameter = diameter;
    tab.shaped_size = {bounds.right, bounds.bottom};
  } else if (region) DeleteObject(region);
}

std::wstring UserSid() {
  HANDLE token = nullptr;
  if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return {};
  DWORD size = 0;
  GetTokenInformation(token, TokenUser, nullptr, 0, &size);
  std::vector<BYTE> buffer(size);
  LPWSTR sid = nullptr;
  if (size && GetTokenInformation(token, TokenUser, buffer.data(), size, &size))
    ConvertSidToStringSidW(reinterpret_cast<const TOKEN_USER*>(buffer.data())->User.Sid, &sid);
  CloseHandle(token);
  std::wstring result = sid ? sid : L"";
  if (sid) LocalFree(sid);
  return result;
}

bool OwnsFocus(HWND child, HWND parent) {
  const HWND root = GetAncestor(parent, GA_ROOT);
  GUITHREADINFO info{sizeof(info)};
  return child && root && IsWindowVisible(child) && root == GetAncestor(GetForegroundWindow(), GA_ROOT) &&
      GetGUIThreadInfo(0, &info) &&
      (info.hwndFocus == child || IsChild(child, info.hwndFocus));
}

HWND ClickTarget(HWND child, HWND parent, DWORD pid) {
  const HWND root = GetAncestor(parent, GA_ROOT);
  if (!child || !root || GetParent(child) != parent || !IsWindowVisible(child) ||
      root != GetAncestor(GetForegroundWindow(), GA_ROOT)) return nullptr;
  POINT cursor{};
  if (!GetCursorPos(&cursor)) return nullptr;
  const HWND target = WindowFromPoint(cursor);
  DWORD owner = 0;
  GetWindowThreadProcessId(target, &owner);
  return owner == pid && (target == child || IsChild(child, target)) ? target : nullptr;
}

void FocusClickedSurface(HWND child, HWND parent, DWORD pid) {
  const HWND target = ClickTarget(child, parent, pid);
  if (!target) return;
  GUITHREADINFO info{sizeof(info)};
  if (GetGUIThreadInfo(0, &info) && info.hwndFocus == target) return;
  // Electron can receive a mouse event while the CEF mount still owns the
  // keyboard. Briefly join the verified target's input queue to hand it focus.
  const DWORD caller = GetCurrentThreadId(), thread = GetWindowThreadProcessId(target, nullptr);
  if (!thread || !AttachThreadInput(caller, thread, TRUE)) return;
  if (ClickTarget(child, parent, pid) == target) SetFocus(target);
  AttachThreadInput(caller, thread, FALSE);
}

// Gives Windows keyboard focus to the visible helper surface of a foreground
// Sol window. Electron's own focus() cannot activate a window that is a child of
// another process's window, and Sol's activation would otherwise refocus the
// empty CEF mount underneath.
void FocusSurface(HWND child, HWND parent, DWORD pid) {
  const HWND root = GetAncestor(parent, GA_ROOT);
  DWORD owner = 0;
  GetWindowThreadProcessId(child, &owner);
  if (!child || owner != pid || GetParent(child) != parent || !IsWindowVisible(child) ||
      root != GetAncestor(GetForegroundWindow(), GA_ROOT)) return;
  GUITHREADINFO info{sizeof(info)};
  if (GetGUIThreadInfo(0, &info) && (info.hwndFocus == child || IsChild(child, info.hwndFocus))) return;
  const DWORD caller = GetCurrentThreadId(), thread = GetWindowThreadProcessId(child, nullptr);
  if (!thread || !AttachThreadInput(caller, thread, TRUE)) return;
  SetFocus(child);
  AttachThreadInput(caller, thread, FALSE);
}

// Wheel input goes to the window with keyboard focus. While Sol's empty CEF
// mount has it, Sol's Chromium handles the wheel and never forwards it to a
// window of another process, so the page under the cursor would not scroll
// until a click moved focus into the helper. A message hook on Sol's UI thread
// posts wheel messages over a helper surface to that surface instead.
std::vector<const ProtectedMediaHost*>& Hosts() {
  static std::vector<const ProtectedMediaHost*> hosts;
  return hosts;
}
HHOOK& WheelHook() {
  static HHOOK hook = nullptr;
  return hook;
}
LRESULT CALLBACK ForwardWheel(int code, WPARAM removed, LPARAM data) {
  if (code == HC_ACTION && removed == PM_REMOVE) {
    auto* message = reinterpret_cast<MSG*>(data);
    if (message->message == WM_MOUSEWHEEL || message->message == WM_MOUSEHWHEEL) {
      const POINT point{GET_X_LPARAM(message->lParam), GET_Y_LPARAM(message->lParam)};
      for (const auto* host : Hosts()) {
        const HWND target = host->SurfaceAt(point);
        if (!target || target == message->hwnd) continue;
        PostMessageW(target, message->message, message->wParam, message->lParam);
        message->message = WM_NULL;
        break;
      }
    }
  }
  return CallNextHookEx(nullptr, code, removed, data);
}

double Number(CefRefPtr<CefDictionaryValue> data, const char* key) {
  return data->GetType(key) == VTYPE_INT ? data->GetInt(key) :
      data->GetType(key) == VTYPE_DOUBLE ? data->GetDouble(key) : -1;
}

// Window-level actions a protected tab hands back to Sol. Page actions (print,
// DevTools, picture-in-picture, capture, fullscreen, stop) stay with the player.
bool IsPlayerShortcut(const std::string& action) {
  const auto shortcut = ShortcutNamed(action);
  if (!shortcut) return false;
  switch (*shortcut) {
    case Shortcut::Address: case Shortcut::NewTab: case Shortcut::CloseTab:
    case Shortcut::RecentTab: case Shortcut::RecentTabBack: case Shortcut::NextTab: case Shortcut::PreviousTab:
    case Shortcut::Reload: case Shortcut::ReloadBypass: case Shortcut::Back: case Shortcut::Forward:
    case Shortcut::Sidebar: case Shortcut::Pin: case Shortcut::CopyUrl: case Shortcut::ReopenClosed:
    case Shortcut::NewWindow: case Shortcut::NewPrivateWindow: case Shortcut::Settings:
    case Shortcut::History: case Shortcut::HistoryPage: case Shortcut::Downloads:
    case Shortcut::Space1: case Shortcut::Space2: case Shortcut::Space3: case Shortcut::Space4: case Shortcut::Space5:
    case Shortcut::Space6: case Shortcut::Space7: case Shortcut::Space8: case Shortcut::Space9:
      return true;
    default:
      return false;
  }
}

// KeyboardEvent.code of a Windows key code that Settings accepts ("" for others).
std::string DomCode(int key) {
  if (key >= 'A' && key <= 'Z') return std::string("Key") + static_cast<char>(key);
  if (key >= '0' && key <= '9') return std::string("Digit") + static_cast<char>(key);
  if (key >= VK_F1 && key <= VK_F12) return "F" + std::to_string(key - VK_F1 + 1);
  switch (key) {
    case VK_TAB: return "Tab";
    case VK_PRIOR: return "PageUp";
    case VK_NEXT: return "PageDown";
    case VK_LEFT: return "ArrowLeft";
    case VK_UP: return "ArrowUp";
    case VK_RIGHT: return "ArrowRight";
    case VK_DOWN: return "ArrowDown";
    case VK_OEM_COMMA: return "Comma";
    default: return "";
  }
}
}  // namespace

const ProtectedMediaService* ProtectedMediaForUrl(const std::string& url) { return ServiceForHost(WebHost(url)); }
bool IsProtectedMediaPlayerUrl(const std::string& url) { return ProtectedMediaForUrl(url); }
std::string ProtectedMediaPlayerUrl(const std::string& url) {
  const auto host = WebHost(url);
  const auto* service = ServiceForHost(host);
  CefURLParts parts;
  if (!service || PlayerHost(*service, host) || !CefParseURL(url, parts)) return url;
  // CefCreateURL uses a non-empty spec as is, which kept the bare domain.
  CefString(&parts.spec).clear();
  CefString(&parts.host) = std::string(service->player_host);
  CefString rebuilt;
  return CefCreateURL(parts, rebuilt) && OnOrigin(rebuilt.ToString(), service->player_host) ? rebuilt.ToString() : url;
}
bool IsProtectedMediaSiteUrl(const std::string& url) {
  const auto host = WebHost(url);
  if (host.empty()) return false;
  for (const auto& service : kServices)
    if (PlayerHost(service, host) || ProtectedMediaHostListed(host, service.sign_in_hosts) ||
        ProtectedMediaHostInDomain(host, service.site) || (*service.alias && host == service.alias)) return true;
  return false;
}

ProtectedMediaHost::ProtectedMediaHost(std::filesystem::path base, std::filesystem::path storage)
    : base_(std::move(base)), storage_(std::move(storage)) {
  Hosts().push_back(this);
  if (!WheelHook()) WheelHook() = SetWindowsHookExW(WH_GETMESSAGE, ForwardWheel, nullptr, GetCurrentThreadId());
}
ProtectedMediaHost::~ProtectedMediaHost() {
  Reset();
  std::erase(Hosts(), this);
  if (Hosts().empty() && WheelHook()) { UnhookWindowsHookEx(WheelHook()); WheelHook() = nullptr; }
}

HWND ProtectedMediaHost::SurfaceAt(POINT point) const {
  if (!pid_) return nullptr;
  const HWND target = WindowFromPoint(point);
  DWORD owner = 0;
  GetWindowThreadProcessId(target, &owner);
  if (!target || owner != pid_) return nullptr;
  for (const auto& [id, tab] : tabs_)
    if (tab.child && IsWindowVisible(tab.child) && (target == tab.child || IsChild(tab.child, target))) return target;
  return nullptr;
}

void ProtectedMediaHost::RetryFailed() {
  std::vector<int> failed;
  for (const auto& [id, tab] : tabs_) if (tab.failed && !tab.child) failed.push_back(id);
  for (const int id : failed) {
    Control(id, "reload");
    if (const auto tab = tabs_.find(id); tab != tabs_.end() && tab->second.changed) {
      const auto changed = tab->second.changed;
      changed();
    }
  }
}

std::filesystem::path ProtectedMediaHost::Runtime() const {
  return ProtectedMediaRuntime(base_, protected_media_release::kId);
}
bool ProtectedMediaHost::Available() const { return !Runtime().empty(); }

HANDLE ProtectedMediaHost::Shutdown() {
  if (!process_) return nullptr;
  HANDLE process = nullptr;
  if (!DuplicateHandle(GetCurrentProcess(), process_, GetCurrentProcess(), &process, SYNCHRONIZE, FALSE, 0)) process = nullptr;
  Fail();
  return process;
}

void ProtectedMediaHost::Fail() {
  Reset();
  std::vector<std::function<void()>> callbacks;
  for (auto& [id, tab] : tabs_) {
    tab.loading = false; tab.failed = true; tab.child = nullptr; tab.media = nullptr;
    tab.committed = false; tab.back = false; tab.forward = false;
    tab.audible = tab.muted = false;
    callbacks.push_back(tab.changed);
  }
  for (const auto& callback : callbacks) if (callback) callback();
}

bool ProtectedMediaHost::Start() {
  if (process_) return true;
  const auto runtime = Runtime();
  if (runtime.empty()) return false;
  GUID guid{};
  const auto sid = UserSid();
  if (sid.empty() || FAILED(CoCreateGuid(&guid))) return false;
  wchar_t uuid[39]{};
  if (!StringFromGUID2(guid, uuid, 39)) return false;
  std::wstring name = LR"(\\.\pipe\sol-protected-media-)";
  for (const wchar_t c : std::wstring_view(uuid))
    if (c != L'{' && c != L'}' && c != L'-') name += c >= L'A' && c <= L'F' ? c + (L'a' - L'A') : c;
  PSECURITY_DESCRIPTOR descriptor = nullptr;
  const std::wstring sddl = L"D:P(A;;GA;;;" + sid + L")";
  if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1, &descriptor, nullptr)) return false;
  SECURITY_ATTRIBUTES security{sizeof(security), descriptor, FALSE};
  pipe_ = CreateNamedPipeW(name.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_FIRST_PIPE_INSTANCE,
      PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_NOWAIT | PIPE_REJECT_REMOTE_CLIENTS, 1, 65536, 65536, 0, &security);
  LocalFree(descriptor);
  if (pipe_ == INVALID_HANDLE_VALUE) return false;
  ConnectNamedPipe(pipe_, nullptr);
  const auto exe = runtime / kExecutable;
  std::wstring command = L"\"" + exe.wstring() + L"\" \"--sol-protected-media-pipe=" + name +
      L"\" \"--sol-protected-media-data=" + storage_.wstring() + L"\"";
  STARTUPINFOW startup{sizeof(startup)};
  PROCESS_INFORMATION process{};
  if (!CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr, runtime.c_str(), &startup, &process)) {
    Reset();
    return false;
  }
  CloseHandle(process.hThread);
  process_ = process.hProcess;
  pid_ = process.dwProcessId;
  connecting_since_ = GetTickCount64();
  CefPostTask(TID_UI, base::BindOnce([](std::weak_ptr<ProtectedMediaHost> weak) {
    if (const auto host = weak.lock()) host->Poll();
  }, weak_from_this()));
  return true;
}

void ProtectedMediaHost::Reset() {
  if (pipe_ != INVALID_HANDLE_VALUE) CloseHandle(pipe_);
  if (process_) CloseHandle(process_);
  pipe_ = INVALID_HANDLE_VALUE;
  process_ = nullptr;
  connected_ = false;
  pid_ = 0;
  input_.clear();
  output_.clear();
}

void ProtectedMediaHost::Open(int id, HWND parent, std::string profile, std::string url, std::function<void()> changed,
                       std::function<void(const std::string&)> shortcut, std::function<void(const std::string&)> external) {
  const auto* service = ProtectedMediaForUrl(url);
  if (!service) return;
  url = ProtectedMediaPlayerUrl(url);  // The helper accepts only the player host.
  auto& tab = tabs_[id];
  tab = {.parent = parent, .service = service, .profile = std::move(profile), .initial_url = url, .url = std::move(url),
      .changed = std::move(changed), .shortcut = std::move(shortcut), .external = std::move(external)};
  if (!Start()) { tab.failed = true; tab.loading = false; return; }
  if (connected_) OpenRemote(id, tab);
}

const ProtectedMediaHost::Tab* ProtectedMediaHost::Get(int id) const {
  const auto tab = tabs_.find(id);
  return tab == tabs_.end() ? nullptr : &tab->second;
}

bool ProtectedMediaHost::Queue(CefRefPtr<CefDictionaryValue> message) {
  if (!connected_ || output_.size() > 32768) return false;
  auto value = CefValue::Create(); value->SetDictionary(message);
  output_ += CefWriteJSON(value, JSON_WRITER_DEFAULT).ToString();
  output_ += '\n';
  return true;
}

void ProtectedMediaHost::OpenRemote(int id, const Tab& tab) {
  auto message = CefDictionaryValue::Create();
  message->SetInt("id", id);
  message->SetString("action", "open");
  message->SetString("profile", tab.profile);
  message->SetString("url", OnPlayer(*tab.service, tab.url) ? tab.url : tab.initial_url);
  message->SetDictionary("service", Describe(*tab.service));
  Queue(message);
}

void ProtectedMediaHost::SetShortcuts(std::vector<Key> keys) {
  std::erase_if(keys, [](const Key& key) { return !IsPlayerShortcut(key.action) || DomCode(key.key).empty(); });
  const auto same = [](const Key& a, const Key& b) {
    return a.action == b.action && a.key == b.key && a.ctrl == b.ctrl && a.shift == b.shift && a.alt == b.alt;
  };
  if (std::equal(keys.begin(), keys.end(), shortcuts_.begin(), shortcuts_.end(), same)) return;
  shortcuts_ = std::move(keys);
  SendShortcuts();
}

void ProtectedMediaHost::SendShortcuts() {
  auto list = CefListValue::Create();
  for (const auto& key : shortcuts_) {
    auto item = CefDictionaryValue::Create();
    item->SetString("action", key.action);
    item->SetString("code", DomCode(key.key));
    item->SetBool("control", key.ctrl);
    item->SetBool("shift", key.shift);
    item->SetBool("alt", key.alt);
    list->SetDictionary(list->GetSize(), item);
  }
  auto message = CefDictionaryValue::Create();
  message->SetString("action", "shortcuts");
  message->SetList("keys", list);
  Queue(message);  // Not connected yet: sent on connection, before the tabs open.
}

bool ProtectedMediaHost::Control(int id, const std::string& action, double value) {
  const auto tab = tabs_.find(id);
  if (tab == tabs_.end()) return false;
  if (action == "reload" && tab->second.failed && !tab->second.child) {
    tab->second.failed = false;
    tab->second.loading = true;
    if (!Start()) { tab->second.failed = true; tab->second.loading = false; return false; }
    if (connected_) OpenRemote(id, tab->second);
    return true;
  }
  if (action == "focus") FocusSurface(tab->second.child, tab->second.parent, pid_);
  auto message = CefDictionaryValue::Create();
  message->SetInt("id", id);
  message->SetString("action", action);
  if (action == "seek" || action == "set-muted") message->SetDouble("value", value);
  return Queue(message);
}

void ProtectedMediaHost::Close(int id) {
  if (tabs_.contains(id)) {
    Control(id, "close");
    tabs_.erase(id);
  }
}

void ProtectedMediaHost::SetCornerRadius(int id, int radius) {
  const auto it = tabs_.find(id);
  if (it != tabs_.end()) it->second.corner_radius = radius;
}

void ProtectedMediaHost::Poll() {
  if (!process_) return;
  const auto failed = [this] { Fail(); };
  if (WaitForSingleObject(process_, 0) != WAIT_TIMEOUT) { failed(); return; }
  if (!connected_) {
    const bool ready = ConnectNamedPipe(pipe_, nullptr) || GetLastError() == ERROR_PIPE_CONNECTED;
    if (ready) {
      ULONG peer = 0;
      if (!GetNamedPipeClientProcessId(pipe_, &peer) || peer != pid_) {
        DisconnectNamedPipe(pipe_);
      } else {
        connected_ = true;
        SendShortcuts();
        for (const auto& [id, tab] : tabs_) if (!tab.failed) OpenRemote(id, tab);
      }
    }
    if (!connected_ && GetTickCount64() - connecting_since_ > 15000) { failed(); return; }
  }
  if (connected_) {
    char buffer[8192];
    DWORD received = 0;
    const bool read = ReadFile(pipe_, buffer, sizeof(buffer), &received, nullptr);
    if (read && received) {
      input_.append(buffer, received);
      if (input_.size() > 32768) { failed(); return; }
      size_t newline = 0;
      while ((newline = input_.find('\n')) != std::string::npos) {
        auto line = input_.substr(0, newline);
        input_.erase(0, newline + 1);
        Message(line);
      }
    } else if (!read && (GetLastError() == ERROR_BROKEN_PIPE || GetLastError() == ERROR_PIPE_NOT_CONNECTED)) {
      failed(); return;
    }
    DWORD written = 0;
    if (!output_.empty() && WriteFile(pipe_, output_.data(), static_cast<DWORD>(output_.size()), &written, nullptr))
      output_.erase(0, written);
    for (auto& [id, tab] : tabs_) {
      if (!tab.child || !IsWindow(tab.child) || GetParent(tab.child) != tab.parent) continue;
      RECT parent{}, child{};
      if (!GetClientRect(tab.parent, &parent) || !GetClientRect(tab.child, &child)) continue;
      if (parent.right != child.right || parent.bottom != child.bottom)
        SetWindowPos(tab.child, HWND_TOP, 0, 0, parent.right, parent.bottom, SWP_NOACTIVATE);
      // After the Sol window was covered, Chromium shows the mount's own input
      // window (Chrome_RenderWidgetHostHWND) again on top of its siblings. It
      // paints nothing, so the helper stayed visible, but the mouse wheel and
      // clicks reached that empty window until the next click.
      if (GetWindow(tab.parent, GW_CHILD) != tab.child)
        SetWindowPos(tab.child, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
      ShapeSurface(tab, parent);
    }
  }
  CefPostDelayedTask(TID_UI, base::BindOnce([](std::weak_ptr<ProtectedMediaHost> weak) {
    if (const auto host = weak.lock()) host->Poll();
  }, weak_from_this()), tabs_.empty() ? 300 : 45);
}

void ProtectedMediaHost::Message(const std::string& line) {
  const auto value = CefParseJSON(line, JSON_PARSER_RFC);
  if (!value || value->GetType() != VTYPE_DICTIONARY) return;
  const auto message = value->GetDictionary();
  const auto id = message->GetInt("id");
  const auto item = tabs_.find(id);
  if (item == tabs_.end()) return;
  auto& tab = item->second;
  const auto event = message->GetString("event");
  if (event == "window") {
    if (tab.child) return;
    const auto text = message->GetString("handle").ToString();
    uintptr_t address = 0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), address);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size()) return;
    const HWND hwnd = reinterpret_cast<HWND>(address);
    DWORD owner = 0, parent_owner = 0;
    GetWindowThreadProcessId(hwnd, &owner);
    GetWindowThreadProcessId(tab.parent, &parent_owner);
    if (!owner || owner != pid_ || parent_owner != GetCurrentProcessId() || GetAncestor(hwnd, GA_ROOT) != hwnd) return;
    const LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
    SetWindowLongPtrW(hwnd, GWL_STYLE, (style & ~static_cast<LONG_PTR>(WS_POPUP | WS_CAPTION | WS_THICKFRAME)) | WS_CHILD);
    SetParent(hwnd, tab.parent);
    if (GetParent(hwnd) != tab.parent) { tab.failed = true; tab.loading = false; Control(id, "close"); }
    else {
      RECT bounds{}; GetClientRect(tab.parent, &bounds);
      tab.child = hwnd;
      tab.applied_diameter = -1;
      SetWindowPos(hwnd, HWND_TOP, 0, 0, bounds.right, bounds.bottom, SWP_NOACTIVATE | SWP_FRAMECHANGED);
      ShapeSurface(tab, bounds);
      Control(id, "attach");
    }
  } else if (event == "navigation") {
    const auto url = message->GetString("url").ToString();
    const auto host = WebHost(url);
    if (!PlayerHost(*tab.service, host) && !ProtectedMediaHostListed(host, tab.service->sign_in_hosts)) return;
    tab.url = url;
    tab.committed = true;
    tab.back = message->GetBool("back"); tab.forward = message->GetBool("forward");
    tab.failed = false;
  } else if (event == "favicon") {
    const auto url = message->GetString("url").ToString();
    CefURLParts parts;
    if (!OnPlayer(*tab.service, tab.url) || !OnOrigin(url, tab.service->icon_host) || !CefParseURL(url, parts)) return;
    const auto path = CefString(&parts.path).ToString();
    if (!path.starts_with(tab.service->icon_path) || !(path.ends_with(".png") || path.ends_with(".ico")) ||
        parts.query.length || parts.fragment.length || url == tab.favicon) return;
    tab.favicon = url;
  } else if (event == "audio") {
    const bool audible = message->GetBool("audible"), muted = message->GetBool("muted");
    if (audible == tab.audible && muted == tab.muted) return;
    tab.audible = audible;
    tab.muted = muted;
  } else if (event == "loading") tab.loading = message->GetBool("active");
  else if (event == "media") {
    const auto incoming = message->GetDictionary("state");
    if (incoming && std::isfinite(Number(incoming, "time")) && std::isfinite(Number(incoming, "duration")) &&
        Number(incoming, "time") >= 0 && Number(incoming, "time") <= 1e7 &&
        Number(incoming, "duration") >= 0 && Number(incoming, "duration") <= 1e7) {
      auto media = CefDictionaryValue::Create();
      media->SetString("title", incoming->GetString("title").ToString().substr(0, 720));
      media->SetString("artist", incoming->GetString("artist").ToString().substr(0, 720));
      for (const auto* key : {"playing", "muted", "seek"}) media->SetBool(key, incoming->GetBool(key));
      for (const auto* key : {"previous", "next", "shuffle"}) {
        if (incoming->GetType(key) == VTYPE_BOOL) media->SetBool(key, incoming->GetBool(key));
      }
      const auto repeat = incoming->GetString("repeat");
      if (repeat == "off" || repeat == "all" || repeat == "one") media->SetString("repeat", repeat);
      for (const auto* key : {"time", "duration"}) media->SetDouble(key, Number(incoming, key));
      if (tab.media && tab.media->IsEqual(media)) return;
      tab.media = media;
    } else {
      if (!tab.media) return;
      tab.media = nullptr;
    }
  } else if (event == "pointer_down") {
    const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    const auto age = static_cast<double>(now) - Number(message, "at");
    if (age >= 0 && age <= 250) FocusClickedSurface(tab.child, tab.parent, pid_);
    return;
  } else if (event == "external") {
    // A link or popup leaving the service: an ordinary Sol tab, at most one a second per tab.
    const auto url = message->GetString("url").ToString();
    CefURLParts parts;
    const auto now = GetTickCount64();
    if (!tab.external || url.size() > 4096 || !CefParseURL(url, parts) || parts.username.length || parts.password.length ||
        (CefString(&parts.scheme) != "https" && CefString(&parts.scheme) != "http") || now - tab.last_external < 1000) return;
    tab.last_external = now;
    const auto external = tab.external;
    external(url);
    return;
  } else if (event == "shortcut") {
    const auto action = message->GetString("action").ToString();
    if (tab.shortcut && IsPlayerShortcut(action) && OwnsFocus(tab.child, tab.parent)) {
      const auto shortcut = tab.shortcut;
      shortcut(action);
    }
    return;
  } else if (event == "failed" || event == "closed") {
    tab.loading = false; tab.failed = true; tab.media = nullptr;
    tab.audible = tab.muted = false;
    if (event == "closed") tab.child = nullptr;
  } else if (event != "refresh") return;
  const auto changed = tab.changed;
  if (changed) changed();
}
}  // namespace red
