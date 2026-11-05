#include "windows/browser_icons.h"
#include <windows.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <map>
#include <string>
#include <vector>
#include "include/cef_parser.h"

namespace red {
namespace {
using Microsoft::WRL::ComPtr;
namespace fs = std::filesystem;

// Which import source a browser program is, by its file name.
std::string SourceFor(const fs::path& exe) {
  std::wstring name = exe.filename().wstring();
  std::transform(name.begin(), name.end(), name.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
  if (name == L"chrome.exe") return "chrome";
  if (name == L"msedge.exe") return "edge";
  if (name == L"arc.exe") return "arc";
  if (name == L"zen.exe") return "zen";
  return {};
}

// The program path in a registry command or icon value: "C:\a\b.exe" --x, or C:\a\b.exe,0.
fs::path ProgramIn(std::wstring value) {
  if (value.empty()) return {};
  if (value.front() == L'"') {
    const auto end = value.find(L'"', 1);
    return end == std::wstring::npos ? fs::path() : fs::path(value.substr(1, end - 1));
  }
  const auto exe = value.find(L".exe");
  return exe == std::wstring::npos ? fs::path() : fs::path(value.substr(0, exe + 4));
}

std::wstring ReadString(HKEY root, const std::wstring& key, REGSAM view) {
  wchar_t buffer[2048]{};
  DWORD bytes = sizeof(buffer);
  if (RegGetValueW(root, key.c_str(), nullptr, RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ | (view == KEY_WOW64_32KEY ? RRF_SUBKEY_WOW6432KEY : RRF_SUBKEY_WOW6464KEY),
                   nullptr, buffer, &bytes) != ERROR_SUCCESS) return {};
  return buffer;
}

// Browsers registered with Windows (Settings > Default apps), plus the usual folders.
std::map<std::string, fs::path> InstalledPrograms() {
  std::map<std::string, fs::path> found;
  const auto add = [&](const fs::path& exe) {
    std::error_code error;
    const auto source = SourceFor(exe);
    if (!source.empty() && !found.contains(source) && fs::is_regular_file(exe, error)) found[source] = exe;
  };
  for (HKEY root : {HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE}) for (REGSAM view : {KEY_WOW64_64KEY, KEY_WOW64_32KEY}) {
    HKEY clients = nullptr;
    if (RegOpenKeyExW(root, L"Software\\Clients\\StartMenuInternet", 0, KEY_READ | view, &clients) != ERROR_SUCCESS) continue;
    wchar_t name[256];
    for (DWORD i = 0; ; ++i) {
      DWORD length = 256;
      if (RegEnumKeyExW(clients, i, name, &length, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS) break;
      const std::wstring base = std::wstring(L"Software\\Clients\\StartMenuInternet\\") + name;
      add(ProgramIn(ReadString(root, base + L"\\shell\\open\\command", view)));
      add(ProgramIn(ReadString(root, base + L"\\DefaultIcon", view)));
    }
    RegCloseKey(clients);
  }
  const auto known = [](REFKNOWNFOLDERID id) {
    PWSTR path = nullptr;
    fs::path result;
    if (SUCCEEDED(SHGetKnownFolderPath(id, KF_FLAG_DEFAULT, nullptr, &path))) result = path;
    CoTaskMemFree(path);
    return result;
  };
  const auto local = known(FOLDERID_LocalAppData), programs = known(FOLDERID_ProgramFiles), programs86 = known(FOLDERID_ProgramFilesX86);
  for (const auto& exe : {programs / L"Google\\Chrome\\Application\\chrome.exe", local / L"Google\\Chrome\\Application\\chrome.exe",
                          programs86 / L"Microsoft\\Edge\\Application\\msedge.exe", programs / L"Microsoft\\Edge\\Application\\msedge.exe",
                          programs / L"Zen Browser\\zen.exe", local / L"Microsoft\\WindowsApps\\Arc.exe"})
    add(exe);
  return found;
}

// An item's icon as Windows draws it (a program file, or an app in the Start menu's list), encoded as PNG.
std::string IconPng(IShellItem* item, int size) {
  ComPtr<IShellItemImageFactory> factory;
  if (!item || FAILED(item->QueryInterface(IID_PPV_ARGS(&factory)))) return {};
  HBITMAP bitmap = nullptr;
  if (FAILED(factory->GetImage(SIZE{size, size}, SIIGBF_ICONONLY | SIIGBF_BIGGERSIZEOK, &bitmap)) || !bitmap) return {};
  std::string png;
  ComPtr<IWICImagingFactory> wic;
  ComPtr<IWICBitmap> source;
  ComPtr<IStream> stream;
  ComPtr<IWICBitmapEncoder> encoder;
  ComPtr<IWICBitmapFrameEncode> frame;
  if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic))) &&
      SUCCEEDED(wic->CreateBitmapFromHBITMAP(bitmap, nullptr, WICBitmapUsePremultipliedAlpha, &source)) &&
      SUCCEEDED(CreateStreamOnHGlobal(nullptr, TRUE, &stream)) &&
      SUCCEEDED(wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder)) &&
      SUCCEEDED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache)) &&
      SUCCEEDED(encoder->CreateNewFrame(&frame, nullptr)) && SUCCEEDED(frame->Initialize(nullptr)) &&
      SUCCEEDED(frame->WriteSource(source.Get(), nullptr)) && SUCCEEDED(frame->Commit()) && SUCCEEDED(encoder->Commit())) {
    STATSTG stat{};
    HGLOBAL memory = nullptr;
    if (SUCCEEDED(stream->Stat(&stat, STATFLAG_NONAME)) && SUCCEEDED(GetHGlobalFromStream(stream.Get(), &memory)) && stat.cbSize.QuadPart < (1 << 20)) {
      if (const void* bytes = GlobalLock(memory)) {
        png.assign(static_cast<const char*>(bytes), static_cast<size_t>(stat.cbSize.QuadPart));
        GlobalUnlock(memory);
      }
    }
  }
  DeleteObject(bitmap);
  return png;
}

std::string IconPng(const fs::path& exe, int size) {
  ComPtr<IShellItem> item;
  if (FAILED(SHCreateItemFromParsingName(exe.c_str(), nullptr, IID_PPV_ARGS(&item)))) return {};
  return IconPng(item.Get(), size);
}

// Apps from the Microsoft Store (Arc) live in a folder programs cannot read;
// Windows' own list of installed apps (shell:AppsFolder, as in the Start menu)
// still gives their icons. Matched by app name or package family.
std::string SourceForApp(const std::wstring& name, const std::wstring& id) {
  if (id.starts_with(L"TheBrowserCompany.Arc") || name == L"Arc") return "arc";
  if (name == L"Google Chrome") return "chrome";
  if (name == L"Microsoft Edge") return "edge";
  if (name == L"Zen Browser" || name == L"Zen") return "zen";
  return {};
}

void AddAppIcons(CefRefPtr<CefDictionaryValue> icons, int size) {
  ComPtr<IShellItem> apps;
  ComPtr<IEnumShellItems> items;
  if (FAILED(SHGetKnownFolderItem(FOLDERID_AppsFolder, KF_FLAG_DEFAULT, nullptr, IID_PPV_ARGS(&apps))) ||
      FAILED(apps->BindToHandler(nullptr, BHID_EnumItems, IID_PPV_ARGS(&items)))) return;
  ComPtr<IShellItem> item;
  while (items->Next(1, &item, nullptr) == S_OK) {
    PWSTR name = nullptr, id = nullptr;
    if (SUCCEEDED(item->GetDisplayName(SIGDN_NORMALDISPLAY, &name)) && SUCCEEDED(item->GetDisplayName(SIGDN_PARENTRELATIVEPARSING, &id))) {
      const auto source = SourceForApp(name, id);
      if (!source.empty() && !icons->HasKey(source)) {
        const auto png = IconPng(item.Get(), size);
        if (!png.empty()) icons->SetString(source, "data:image/png;base64," + CefBase64Encode(png.data(), png.size()).ToString());
      }
    }
    CoTaskMemFree(name); CoTaskMemFree(id);
    item.Reset();
  }
}
}  // namespace

CefRefPtr<CefDictionaryValue> InstalledBrowserIcons(int size) {
  auto icons = CefDictionaryValue::Create();
  for (const auto& [source, exe] : InstalledPrograms()) {
    const auto png = IconPng(exe, size);
    if (!png.empty()) icons->SetString(source, "data:image/png;base64," + CefBase64Encode(png.data(), png.size()).ToString());
  }
  if (icons->GetSize() < 4) AddAppIcons(icons, size);
  return icons;
}
}  // namespace red
