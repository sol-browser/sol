#pragma once
#include <windows.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <propkey.h>
#include <wrl/client.h>
#include <filesystem>
namespace red {
inline constexpr wchar_t kBrowserAppId[] = L"Sol.Browser";
// Give the installed browser one resolvable shell identity. Only our own
// Start-menu shortcut is touched; effective file/protocol defaults are not.
inline bool EnsureBrowserShortcut(const std::filesystem::path& executable) {
  const auto initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  const bool result = [&] {
    PWSTR programs = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_Programs, KF_FLAG_DEFAULT, nullptr, &programs))) return false;
    const auto shortcut = std::filesystem::path(programs) / L"Sol.lnk";
    CoTaskMemFree(programs);
    Microsoft::WRL::ComPtr<IShellLinkW> link;
    Microsoft::WRL::ComPtr<IPersistFile> file;
    Microsoft::WRL::ComPtr<IPropertyStore> properties;
    if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link))) ||
        FAILED(link.As(&file)) || FAILED(link.As(&properties))) return false;
    std::error_code error;
    const bool exists = std::filesystem::exists(shortcut, error);
    if (exists) {
      wchar_t previous[32768]{};
      if (FAILED(file->Load(shortcut.c_str(), STGM_READWRITE)) || FAILED(link->GetPath(previous, 32768, nullptr, SLGP_RAWPATH)) ||
          _wcsicmp(previous, executable.c_str()) != 0) return false;
    }
    PROPVARIANT id{}; id.vt = VT_LPWSTR; id.pwszVal = const_cast<PWSTR>(kBrowserAppId);
    if (!exists && (FAILED(link->SetPath(executable.c_str())) || FAILED(link->SetArguments(L"--workspace")) ||
        FAILED(link->SetWorkingDirectory(executable.parent_path().c_str())) || FAILED(link->SetDescription(L"Sol")) ||
        FAILED(link->SetIconLocation(executable.c_str(), 0)))) return false;
    // IShellLink's in-memory property store is persisted by IPersistFile::Save.
    if (FAILED(properties->SetValue(PKEY_AppUserModel_ID, id)) || FAILED(file->Save(shortcut.c_str(), TRUE))) return false;
    SHChangeNotify(SHCNE_UPDATEITEM, SHCNF_PATHW, shortcut.c_str(), nullptr);
    return true;
  }();
  if (SUCCEEDED(initialized)) CoUninitialize();
  return result;
}
}  // namespace red
