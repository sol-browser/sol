// Sol's small installer (docs/installer.md): a frameless window that finds the
// latest Sol release on GitHub, downloads its Velopack setup, checks the
// SHA-256 from the release's checksum file, installs it silently and opens Sol.
//
//   sol_installer.exe                          install
//   sol_installer.exe --render-preview <dir>   draw each state to PNG files
//
// Test feed: SOL_INSTALLER_FEED=http://127.0.0.1:<port> serves
// releases.win.json, Sol-<v>.sha256 and the setup from one folder. Sol is
// then opened only when SOL_INSTALLER_TEST_PROFILE names a profile folder.
#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <shellscalingapi.h>
#include <shlobj.h>
#include <wincodec.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <sstream>
#include <thread>
#include "feed.h"
#include "net.h"
#include "view.h"

namespace sol_installer {
namespace {
using Microsoft::WRL::ComPtr;
namespace fs = std::filesystem;
constexpr wchar_t kClass[] = L"SolInstaller";
constexpr UINT kModelChanged = WM_APP + 1, kOpened = WM_APP + 2;
constexpr wchar_t kRepository[] = L"https://github.com/sol-browser/sol/releases";
constexpr char kProductPackage[] = "SolBrowser";

std::wstring Wide(const std::string& text) {
  if (text.empty()) return {};
  const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), int(text.size()), nullptr, 0);
  std::wstring out(size, L'\0');
  MultiByteToWideChar(CP_UTF8, 0, text.data(), int(text.size()), out.data(), size);
  return out;
}

std::wstring Environment(const wchar_t* name) {
  wchar_t value[1024]{};
  const DWORD length = GetEnvironmentVariableW(name, value, 1024);
  return length && length < 1024 ? std::wstring(value, length) : std::wstring();
}

fs::path LocalAppData() {
  PWSTR path = nullptr;
  if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DEFAULT, nullptr, &path))) return {};
  fs::path result(path);
  CoTaskMemFree(path);
  return result;
}

// Where release files come from: GitHub, or a loopback test feed.
struct Source {
  std::wstring base;  // Empty for GitHub.
  bool test() const { return !base.empty(); }
  std::wstring Feed() const { return test() ? base + L"/releases.win.json" : std::wstring(kRepository) + L"/latest/download/releases.win.json"; }
  std::wstring File(const Version& version, const std::string& name) const {
    return test() ? base + L"/" + Wide(name) : std::wstring(kRepository) + L"/download/v" + Wide(ToString(version)) + L"/" + Wide(name);
  }
};

Source ChooseSource() {
  std::wstring base = Environment(L"SOL_INSTALLER_FEED");
  while (!base.empty() && base.back() == L'/') base.pop_back();
  if (base.rfind(L"http://127.0.0.1:", 0) == 0 || base.rfind(L"http://localhost:", 0) == 0) return {base};
  return {};
}

// The version a Velopack install of |package| has (<root>\current\sq.version).
std::optional<Version> InstalledVersion(const fs::path& root) {
  std::ifstream file(root / L"current" / L"sq.version", std::ios::binary);
  if (!file) return std::nullopt;
  std::stringstream text;
  text << file.rdbuf();
  const std::string xml = text.str();
  const size_t start = xml.find("<version>"), end = xml.find("</version>");
  if (start == std::string::npos || end == std::string::npos || end <= start + 9) return std::nullopt;
  return ParseVersion(std::string_view(xml).substr(start + 9, end - start - 9));
}

struct App {
  HWND window = nullptr;
  UINT dpi = 96;
  ComPtr<ID2D1Factory> d2d;
  ComPtr<IDWriteFactory> dwrite;
  std::unique_ptr<View> view;
  ComPtr<ID2D1HwndRenderTarget> target;

  std::mutex mutex;
  Model model;   // Written by the worker under |mutex|.
  Model shown;   // The UI thread's copy.
  Motion motion;
  Hit hover = Hit::None;
  bool pressed = false, tracking = false;
  double close_at = -1;
  std::chrono::steady_clock::time_point last = std::chrono::steady_clock::now();

  std::atomic<bool> cancel{false}, working{false};
  std::thread worker;
};

void Publish(App* app, const std::function<void(Model&)>& change) {
  { std::lock_guard lock(app->mutex); change(app->model); }
  PostMessageW(app->window, kModelChanged, 0, 0);
}

bool Launch(const fs::path& exe, const std::wstring& arguments) {
  std::wstring command = L"\"" + exe.wstring() + L"\"" + (arguments.empty() ? L"" : L" " + arguments);
  STARTUPINFOW startup{sizeof(startup)};
  PROCESS_INFORMATION process{};
  if (!CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr, exe.parent_path().c_str(), &startup, &process)) return false;
  WaitForInputIdle(process.hProcess, 4000);
  CloseHandle(process.hThread);
  CloseHandle(process.hProcess);
  return true;
}

// Opens the installed Sol. A test install opens only with its own profile.
void OpenSol(App* app, const Source& source, const fs::path& exe) {
  if (!source.test()) { Launch(exe, L""); }
  else if (const auto profile = Environment(L"SOL_INSTALLER_TEST_PROFILE"); !profile.empty()) {
    Launch(exe, L"--workspace \"--profile-dir=" + profile + L"\"");
  }
  PostMessageW(app->window, kOpened, 0, 0);
}

void Install(App* app) {
  const auto fail = [&](std::wstring message) { Publish(app, [&](Model& m) { m.phase = Phase::Failed; m.error = std::move(message); }); };
  const Source source = ChooseSource();
  Http http(L"Sol-Web-Installer/" SOL_INSTALLER_VERSION);
  std::wstring error;

  const auto feed = http.Fetch(source.Feed(), 1 << 20, &error);
  if (!feed) return fail(L"Couldn’t reach GitHub. " + error + L".");
  const auto release = LatestFullRelease(*feed);
  if (!release || (!source.test() && release->package_id != kProductPackage)) return fail(L"The latest release has no Sol package.");
  const std::wstring version = Wide(ToString(release->version));
  Publish(app, [&](Model& m) { m.version = version; });

  const fs::path root = LocalAppData() / Wide(release->package_id);
  const fs::path exe = root / L"current" / L"sol.exe";
  if (const auto installed = InstalledVersion(root); installed && *installed >= release->version && fs::exists(exe)) {
    Publish(app, [&](Model& m) { m.phase = Phase::UpToDate; m.version = Wide(ToString(*installed)); });
    return OpenSol(app, source, exe);
  }

  const auto sums = http.Fetch(source.File(release->version, "Sol-" + ToString(release->version) + ".sha256"), 1 << 16, &error);
  if (!sums) return fail(L"Couldn’t download the release checksums. " + error + L".");
  const auto checksum = SetupChecksum(*sums, release->version);
  if (!checksum) return fail(L"The release has no Sol setup.");

  wchar_t temp[MAX_PATH]{};
  GetTempPathW(MAX_PATH, temp);
  const fs::path folder = fs::path(temp) / (L"SolInstaller-" + std::to_wstring(GetCurrentProcessId()));
  std::error_code io;
  fs::create_directories(folder, io);
  const fs::path setup = folder / Wide(checksum->file);
  struct Cleanup { fs::path folder; ~Cleanup() { std::error_code ignored; fs::remove_all(folder, ignored); } } cleanup{folder};

  Publish(app, [&](Model& m) { m.phase = Phase::Downloading; m.received = m.total = 0; });
  auto last = std::chrono::steady_clock::now() - std::chrono::seconds(1);
  const auto progress = [&](uint64_t received, uint64_t total) {
    const auto now = std::chrono::steady_clock::now();
    if (now - last < std::chrono::milliseconds(40) && received != total) return;
    last = now;
    Publish(app, [&](Model& m) { m.received = received; m.total = total; });
  };
  if (!http.Download(source.File(release->version, checksum->file), setup, 2ull << 30, progress, app->cancel, &error)) {
    if (app->cancel) return;
    return fail(L"The download stopped. " + error + L".");
  }
  if (Sha256File(setup) != checksum->sha256) return fail(L"The download is damaged (checksum mismatch). Try again.");

  // Velopack's setup installs to %LOCALAPPDATA%\<package>. Sol's own install
  // hook moves an older Sol (Inno setup) and keeps the profile.
  Publish(app, [&](Model& m) { m.phase = Phase::Installing; });
  std::wstring command = L"\"" + setup.wstring() + L"\" --silent";
  STARTUPINFOW startup{sizeof(startup)};
  PROCESS_INFORMATION process{};
  if (!CreateProcessW(setup.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, folder.c_str(), &startup, &process))
    return fail(L"Couldn’t start the setup.");
  WaitForSingleObject(process.hProcess, INFINITE);
  DWORD code = 1;
  GetExitCodeProcess(process.hProcess, &code);
  CloseHandle(process.hThread);
  CloseHandle(process.hProcess);
  if (code != 0 || !fs::exists(exe)) return fail(L"The setup didn’t finish (code " + std::to_wstring(code) + L").");

  Publish(app, [&](Model& m) { m.phase = Phase::Opening; });
  OpenSol(app, source, exe);
}

void Start(App* app) {
  if (app->worker.joinable()) app->worker.join();
  app->cancel = false;
  app->working = true;
  Publish(app, [](Model& m) { m = Model{}; });
  app->worker = std::thread([app] { Install(app); app->working = false; });
}

float ToDip(const App* app, int pixels) { return pixels * 96.f / float(app->dpi); }

void Render(App* app) {
  const auto now = std::chrono::steady_clock::now();
  const double dt = std::min(.05, std::chrono::duration<double>(now - app->last).count());
  app->last = now;
  Animate(app->motion, app->shown, app->hover, app->pressed, dt);
  if (!app->target) {
    RECT client{};
    GetClientRect(app->window, &client);
    const auto properties = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT, D2D1::PixelFormat(), float(app->dpi), float(app->dpi));
    app->d2d->CreateHwndRenderTarget(properties, D2D1::HwndRenderTargetProperties(app->window, D2D1::SizeU(client.right, client.bottom)), &app->target);
    if (!app->target) return;
  }
  app->target->BeginDraw();
  app->view->Draw(app->target.Get(), app->shown, app->motion);
  if (app->target->EndDraw() == D2DERR_RECREATE_TARGET) app->target.Reset();
  if (app->close_at >= 0 && app->motion.time >= app->close_at) DestroyWindow(app->window);
}

void Click(App* app, Hit hit) {
  if (hit == Hit::Close) PostMessageW(app->window, WM_CLOSE, 0, 0);
  if (hit == Hit::Primary && app->shown.phase == Phase::Failed && !app->working) Start(app);
}

LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
  auto* app = reinterpret_cast<App*>(GetWindowLongPtrW(window, GWLP_USERDATA));
  switch (message) {
    case WM_NCCREATE:
      SetWindowLongPtrW(window, GWLP_USERDATA, LONG_PTR(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams));
      break;
    case WM_NCCALCSIZE:
      if (wparam) return 0;  // The whole window is client area; DWM keeps the shadow and corners.
      break;
    case WM_NCHITTEST: {
      POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
      ScreenToClient(window, &point);
      return HitTest(app->shown, ToDip(app, point.x), ToDip(app, point.y)) == Hit::Drag ? HTCAPTION : HTCLIENT;
    }
    case WM_SETCURSOR:
      if (LOWORD(lparam) == HTCLIENT && (app->hover == Hit::Close || app->hover == Hit::Primary)) {
        SetCursor(LoadCursorW(nullptr, IDC_HAND));
        return TRUE;
      }
      break;
    case WM_MOUSEMOVE:
      app->hover = HitTest(app->shown, ToDip(app, GET_X_LPARAM(lparam)), ToDip(app, GET_Y_LPARAM(lparam)));
      if (!app->tracking) {
        TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, window, 0};
        app->tracking = TrackMouseEvent(&track);
      }
      return 0;
    case WM_MOUSELEAVE:
      app->tracking = false;
      if (!app->pressed) app->hover = Hit::None;
      return 0;
    case WM_LBUTTONDOWN:
      app->pressed = true;
      SetCapture(window);
      return 0;
    case WM_LBUTTONUP: {
      const bool was = app->pressed;
      app->pressed = false;
      ReleaseCapture();
      const Hit hit = HitTest(app->shown, ToDip(app, GET_X_LPARAM(lparam)), ToDip(app, GET_Y_LPARAM(lparam)));
      if (was && hit == app->hover) Click(app, hit);
      return 0;
    }
    case WM_KEYDOWN:
      if (wparam == VK_ESCAPE) PostMessageW(window, WM_CLOSE, 0, 0);
      if (wparam == VK_RETURN) Click(app, Hit::Primary);
      return 0;
    case WM_CLOSE:
      // Velopack is installing: closing now would leave a half-done install.
      if (app->shown.phase == Phase::Installing) return 0;
      app->cancel = true;
      DestroyWindow(window);
      return 0;
    case WM_DESTROY:
      PostQuitMessage(0);
      return 0;
    case WM_ERASEBKGND:
      return 1;
    case WM_PAINT:
      Render(app);
      ValidateRect(window, nullptr);
      if (!IsIconic(window) && IsWindow(window)) InvalidateRect(window, nullptr, FALSE);  // Next frame, paced by vsync.
      return 0;
    case WM_SIZE:
      if (app->target) app->target->Resize(D2D1::SizeU(LOWORD(lparam), HIWORD(lparam)));
      InvalidateRect(window, nullptr, FALSE);
      return 0;
    case WM_DPICHANGED: {
      app->dpi = HIWORD(wparam);
      if (app->target) app->target->SetDpi(float(app->dpi), float(app->dpi));
      const auto* rect = reinterpret_cast<const RECT*>(lparam);
      SetWindowPos(window, nullptr, rect->left, rect->top, rect->right - rect->left, rect->bottom - rect->top, SWP_NOZORDER | SWP_NOACTIVATE);
      return 0;
    }
    case kModelChanged: {
      std::lock_guard lock(app->mutex);
      app->shown = app->model;
      return 0;
    }
    case kOpened:
      app->close_at = app->motion.time + 1.9;  // Let the mark settle, then go.
      return 0;
  }
  return DefWindowProcW(window, message, wparam, lparam);
}

int RunWindow(HINSTANCE instance, int show) {
  HANDLE single = CreateMutexW(nullptr, TRUE, L"Local\\SolInstaller");
  if (GetLastError() == ERROR_ALREADY_EXISTS) {
    if (HWND other = FindWindowW(kClass, nullptr)) { ShowWindow(other, SW_RESTORE); SetForegroundWindow(other); }
    return 0;
  }
  App app;
  if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, app.d2d.GetAddressOf())) ||
      FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(app.dwrite.GetAddressOf()))))
    return 1;
  app.view = std::make_unique<View>(app.d2d.Get(), app.dwrite.Get());

  WNDCLASSEXW type{sizeof(type)};
  type.lpfnWndProc = WindowProc;
  type.hInstance = instance;
  type.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  type.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(1));
  type.hIconSm = type.hIcon;
  type.lpszClassName = kClass;
  RegisterClassExW(&type);

  // Centered on the monitor under the pointer, at that monitor's scale.
  POINT cursor{};
  GetCursorPos(&cursor);
  HMONITOR monitor = MonitorFromPoint(cursor, MONITOR_DEFAULTTOPRIMARY);
  MONITORINFO info{sizeof(info)};
  GetMonitorInfoW(monitor, &info);
  UINT dpi_x = 96, dpi_y = 96;
  GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpi_x, &dpi_y);
  app.dpi = dpi_x;
  const int width = MulDiv(int(kWidth), dpi_x, 96), height = MulDiv(int(kHeight), dpi_x, 96);
  const RECT& work = info.rcWork;
  const int x = work.left + (work.right - work.left - width) / 2, y = work.top + (work.bottom - work.top - height) / 2;
  app.window = CreateWindowExW(WS_EX_APPWINDOW, kClass, L"Sol Web Installer", WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                               x, y, width, height, nullptr, nullptr, instance, &app);
  if (!app.window) return 1;
  const DWM_WINDOW_CORNER_PREFERENCE corners = DWMWCP_ROUND;
  DwmSetWindowAttribute(app.window, DWMWA_WINDOW_CORNER_PREFERENCE, &corners, sizeof(corners));
  const BOOL dark = TRUE;
  DwmSetWindowAttribute(app.window, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
  const COLORREF border = RGB(0x33, 0x2D, 0x28);
  DwmSetWindowAttribute(app.window, DWMWA_BORDER_COLOR, &border, sizeof(border));
  const MARGINS margins{0, 0, 0, 1};
  DwmExtendFrameIntoClientArea(app.window, &margins);
  SetWindowPos(app.window, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
  ShowWindow(app.window, show);
  UpdateWindow(app.window);
  Start(&app);

  MSG message;
  while (GetMessageW(&message, nullptr, 0, 0) > 0) {
    TranslateMessage(&message);
    DispatchMessageW(&message);
  }
  // A cancelled download stops within one read; never wait on a running setup forever.
  app.cancel = true;
  for (int i = 0; i < 100 && app.working; ++i) Sleep(50);
  if (app.worker.joinable()) { if (app.working) app.worker.detach(); else app.worker.join(); }
  if (single) CloseHandle(single);
  return 0;
}

// --render-preview <folder>: each state as a PNG at 2x, for review without a window.
int DrawPreviews(const fs::path& folder) {
  ComPtr<ID2D1Factory> d2d;
  ComPtr<IDWriteFactory> dwrite;
  ComPtr<IWICImagingFactory> wic;
  if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, d2d.GetAddressOf())) ||
      FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(dwrite.GetAddressOf()))) ||
      FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic))))
    return 1;
  View view(d2d.Get(), dwrite.Get());
  std::error_code io;
  fs::create_directories(folder, io);
  struct Shot { const wchar_t* name; Model model; float settle; Hit hover; };
  const Shot shots[] = {
      {L"1-checking", {Phase::Checking}, 0, Hit::None},
      {L"2-downloading", {Phase::Downloading, L"0.1.1", 84ull << 20, 208ull << 20}, 0, Hit::None},
      {L"2b-downloading-start", {Phase::Downloading, L"0.1.1", 12ull << 20, 208ull << 20}, 0, Hit::None},
      {L"3-downloading-late", {Phase::Downloading, L"0.1.1", 190ull << 20, 208ull << 20}, 0, Hit::Close},
      {L"4-installing", {Phase::Installing, L"0.1.1"}, 0, Hit::None},
      {L"5-ready", {Phase::Opening, L"0.1.1"}, 1, Hit::None},
      {L"6-failed", {Phase::Failed, L"", 0, 0, L"Couldn’t reach GitHub. The server name or address could not be resolved."}, 0, Hit::Primary},
  };
  int failures = 0;
  for (const auto& shot : shots) {
    ComPtr<IWICBitmap> bitmap;
    ComPtr<ID2D1RenderTarget> target;
    const UINT w = UINT(kWidth * 2), h = UINT(kHeight * 2);
    const auto properties = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 192, 192);
    if (FAILED(wic->CreateBitmap(w, h, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, &bitmap)) ||
        FAILED(d2d->CreateWicBitmapRenderTarget(bitmap.Get(), properties, &target))) { ++failures; continue; }
    Motion motion;
    motion.time = 2.4;
    motion.fill = TargetFill(shot.model);
    motion.morph = TargetMorph(shot.model);
    motion.settle = shot.settle;
    motion.close_hover = shot.hover == Hit::Close ? 1.f : 0.f;
    motion.primary_hover = shot.hover == Hit::Primary ? 1.f : 0.f;
    target->BeginDraw();
    view.Draw(target.Get(), shot.model, motion);
    if (FAILED(target->EndDraw())) { ++failures; continue; }
    ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapEncoder> encoder;
    ComPtr<IWICBitmapFrameEncode> frame;
    const auto file = (folder / (std::wstring(shot.name) + L".png")).wstring();
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppPBGRA;
    if (FAILED(wic->CreateStream(&stream)) || FAILED(stream->InitializeFromFilename(file.c_str(), GENERIC_WRITE)) ||
        FAILED(wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder)) || FAILED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache)) ||
        FAILED(encoder->CreateNewFrame(&frame, nullptr)) || FAILED(frame->Initialize(nullptr)) || FAILED(frame->SetSize(w, h)) ||
        FAILED(frame->SetPixelFormat(&format)) || FAILED(frame->WriteSource(bitmap.Get(), nullptr)) || FAILED(frame->Commit()) ||
        FAILED(encoder->Commit())) { ++failures; continue; }
  }
  return failures ? 1 : 0;
}

int RenderPreview(const fs::path& folder) {
  if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) return 1;
  const int result = DrawPreviews(folder);  // Its COM objects are gone before CoUninitialize.
  CoUninitialize();
  return result;
}
}  // namespace
}  // namespace sol_installer

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
  int count = 0;
  wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &count);
  int result = 0;
  if (argv && count == 3 && std::wstring_view(argv[1]) == L"--render-preview") result = sol_installer::RenderPreview(argv[2]);
  else result = sol_installer::RunWindow(instance, show);
  if (argv) LocalFree(argv);
  return result;
}
