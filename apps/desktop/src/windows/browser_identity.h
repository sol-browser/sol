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
    bool replace = false;
    if (exists) {
      wchar_t previous[32768]{};
      if (FAILED(file->Load(shortcut.c_str(), STGM_READWRITE)) || FAILED(link->GetPath(previous, 32768, nullptr, SLGP_RAWPATH))) return false;
      if (_wcsicmp(previous, executable.c_str()) != 0) {
        // Another program's "Sol" shortcut stays. Ours from an older install
        // (the Inno folder, or a target that is gone) moves to this one.
        const std::wstring old(previous);
        replace = old.ends_with(L"\\Sol\\Application\\sol.exe") || !std::filesystem::exists(old, error);
        if (!replace) return false;
      }
    }
    PROPVARIANT id{}; id.vt = VT_LPWSTR; id.pwszVal = const_cast<PWSTR>(kBrowserAppId);
    if ((!exists || replace) && (FAILED(link->SetPath(executable.c_str())) || FAILED(link->SetArguments(L"--workspace")) ||
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
// Uninstall: removes the Start menu shortcut if it is ours (it starts a sol.exe).
inline void RemoveBrowserShortcut() {
  PWSTR programs = nullptr;
  if (FAILED(SHGetKnownFolderPath(FOLDERID_Programs, KF_FLAG_DEFAULT, nullptr, &programs))) return;
  const auto shortcut = std::filesystem::path(programs) / L"Sol.lnk";
  CoTaskMemFree(programs);
  const auto initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  bool ours = false;
  {
    Microsoft::WRL::ComPtr<IShellLinkW> link; Microsoft::WRL::ComPtr<IPersistFile> file;
    wchar_t target[32768]{};
    if (SUCCEEDED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link))) && SUCCEEDED(link.As(&file)) &&
        SUCCEEDED(file->Load(shortcut.c_str(), STGM_READ)) && SUCCEEDED(link->GetPath(target, 32768, nullptr, SLGP_RAWPATH)))
      ours = std::filesystem::path(target).filename() == L"sol.exe";
  }
  if (SUCCEEDED(initialized)) CoUninitialize();
  std::error_code error;
  if (ours) std::filesystem::remove(shortcut, error);
}
}  // namespace red
