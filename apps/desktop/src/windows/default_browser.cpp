#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <wrl/client.h>
#include "windows/default_browser.h"
#include "windows/browser_identity.h"

namespace red {
namespace {
constexpr wchar_t kClient[] = L"Software\\Clients\\StartMenuInternet\\Sol";
constexpr wchar_t kCapabilities[] = L"Software\\Clients\\StartMenuInternet\\Sol\\Capabilities";
constexpr wchar_t kUrlId[] = L"SolBrowserURL";
constexpr wchar_t kHtmlId[] = L"SolBrowserHTML";
std::wstring Read(const std::wstring& key, const wchar_t* name = nullptr) {
  DWORD bytes = 0;
  if (RegGetValueW(HKEY_CURRENT_USER, key.c_str(), name, RRF_RT_REG_SZ, nullptr, nullptr, &bytes) != ERROR_SUCCESS ||
      !bytes || bytes > 32768) return {};
  std::wstring value(bytes / sizeof(wchar_t), L'\0');
  if (RegGetValueW(HKEY_CURRENT_USER, key.c_str(), name, RRF_RT_REG_SZ, nullptr, value.data(), &bytes) != ERROR_SUCCESS) return {};
  if (!value.empty() && value.back() == L'\0') value.pop_back();
  return value;
}
bool Write(const std::wstring& key, const wchar_t* name, const std::wstring& value) {
  HKEY handle = nullptr;
  if (RegCreateKeyExW(HKEY_CURRENT_USER, key.c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &handle, nullptr) != ERROR_SUCCESS) return false;
  const auto result = RegSetValueExW(handle, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()),
      static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
  RegCloseKey(handle); return result == ERROR_SUCCESS;
}
bool WriteDword(const std::wstring& key, const wchar_t* name, DWORD value) {
  HKEY handle = nullptr;
  if (RegCreateKeyExW(HKEY_CURRENT_USER, key.c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &handle, nullptr) != ERROR_SUCCESS) return false;
  const auto status = RegSetValueExW(handle, name, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&value), sizeof(value));
  RegCloseKey(handle); return status == ERROR_SUCCESS;
}
std::wstring OpenCommand(const std::filesystem::path& path) {
  return L"\"" + path.wstring() + L"\" --workspace -- \"%1\"";
}
bool Registered(const std::filesystem::path& path) {
  return !path.empty() && Read(L"Software\\RegisteredApplications", L"Sol") == kCapabilities &&
      Read(L"Software\\Classes\\SolBrowserURL\\shell\\open\\command") == OpenCommand(path) &&
      Read(std::wstring(kCapabilities) + L"\\URLAssociations", L"http") == kUrlId &&
      Read(std::wstring(kCapabilities) + L"\\URLAssociations", L"https") == kUrlId;
}
bool Register(const std::filesystem::path& path) {
  const auto icon = L"\"" + path.wstring() + L"\",0";
  const auto client = std::wstring(kClient), capabilities = std::wstring(kCapabilities);
  // Claims are offers in Windows Settings, never writes to effective defaults.
  const std::wstring app_paths = L"Software\\Microsoft\\Windows\\CurrentVersion\\App Paths\\sol.exe";
  const std::wstring application = L"Software\\Classes\\Applications\\sol.exe";
  bool ok = EnsureBrowserShortcut(path) && Write(app_paths, nullptr, path.wstring()) && Write(app_paths, L"Path", path.parent_path().wstring()) &&
      Write(application, L"FriendlyAppName", L"Sol") &&
      Write(application + L"\\shell\\open\\command", nullptr, OpenCommand(path)) &&
      Write(application + L"\\SupportedTypes", L".htm", L"") && Write(application + L"\\SupportedTypes", L".html", L"") &&
      Write(client, nullptr, L"Sol") && Write(client + L"\\DefaultIcon", nullptr, icon) &&
      Write(client + L"\\shell\\open\\command", nullptr, L"\"" + path.wstring() + L"\" --workspace") &&
      Write(client + L"\\InstallInfo", L"ReinstallCommand", L"\"" + path.wstring() + L"\" --make-default-browser") &&
      WriteDword(client + L"\\InstallInfo", L"IconsVisible", 1) &&
      Write(capabilities, L"ApplicationName", L"Sol") &&
      Write(capabilities, L"ApplicationDescription", L"Browse the web with Sol.") &&
      Write(capabilities, L"ApplicationIcon", icon) &&
      Write(capabilities + L"\\Startmenu", L"StartMenuInternet", L"Sol") &&
      Write(capabilities + L"\\URLAssociations", L"http", kUrlId) &&
      Write(capabilities + L"\\URLAssociations", L"https", kUrlId) &&
      Write(capabilities + L"\\FileAssociations", L".htm", kHtmlId) &&
      Write(capabilities + L"\\FileAssociations", L".html", kHtmlId);
  for (const auto* id : {kUrlId, kHtmlId}) {
    const auto key = std::wstring(L"Software\\Classes\\") + id;
    ok = ok && Write(key, nullptr, id == kUrlId ? L"Sol URL" : L"Sol HTML Document") &&
        Write(key + L"\\DefaultIcon", nullptr, icon) && Write(key + L"\\shell\\open\\command", nullptr, OpenCommand(path)) &&
        Write(key + L"\\Application", L"ApplicationName", L"Sol") && Write(key + L"\\Application", L"ApplicationIcon", icon) &&
        Write(key + L"\\Application", L"ApplicationCompany", L"Sol") &&
        Write(key + L"\\Application", L"ApplicationDescription", L"Browse the web with Sol.") &&
        Write(key, L"AppUserModelId", kBrowserAppId) && Write(key + L"\\Application", L"AppUserModelId", kBrowserAppId);
  }
  ok = ok && Write(L"Software\\Classes\\SolBrowserURL", L"URL Protocol", L"") &&
      Write(L"Software\\Classes\\.htm\\OpenWithProgids", kHtmlId, L"") &&
      Write(L"Software\\Classes\\.html\\OpenWithProgids", kHtmlId, L"");
  // Publish last so a partial registration is not advertised as complete.
  // ApplicationName must match this value name (Microsoft's registration
  // contract); a differing name can omit the app from Windows Settings.
  ok = ok && Write(L"Software\\RegisteredApplications", L"Sol", capabilities);
  if (ok && Read(L"Software\\RegisteredApplications", L"RedBrowser") == L"Software\\Clients\\StartMenuInternet\\RedBrowser\\Capabilities") {
    HKEY applications = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\RegisteredApplications", 0, KEY_SET_VALUE, &applications) == ERROR_SUCCESS) {
      RegDeleteValueW(applications, L"RedBrowser"); RegCloseKey(applications);
    }
  }
  if (ok) SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
  return ok;
}
}  // namespace
std::filesystem::path InstalledBrowserPath() {
  PWSTR local = nullptr;
  if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DEFAULT, nullptr, &local))) return {};
  const std::filesystem::path root(local);
  CoTaskMemFree(local);
  // Velopack keeps each version in <root>\current (a stable path); the Inno setup
  // used Sol\Application before it.
  for (const auto& path : {root / L"SolBrowser" / L"current" / L"sol.exe", root / L"Sol" / L"Application" / L"sol.exe"}) {
    std::error_code error;
    if (std::filesystem::is_regular_file(path, error) && !error) return path;
  }
  return {};
}
bool EnsureRegistered() {
  const auto path = InstalledBrowserPath();
  return !path.empty() && (Registered(path) || Register(path));
}
void Unregister() {
  // Offers only: Windows keeps the person's own choices (UserChoice) and drops
  // ours with the ProgIDs. The profile is never touched.
  for (const auto* tree : {L"Software\\Classes\\SolBrowserURL", L"Software\\Classes\\SolBrowserHTML", L"Software\\Clients\\StartMenuInternet\\Sol",
                           L"Software\\Classes\\Applications\\sol.exe", L"Software\\Microsoft\\Windows\\CurrentVersion\\App Paths\\sol.exe"})
    RegDeleteTreeW(HKEY_CURRENT_USER, tree);
  HKEY key = nullptr;
  if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\RegisteredApplications", 0, KEY_SET_VALUE, &key) == ERROR_SUCCESS) { RegDeleteValueW(key, L"Sol"); RegCloseKey(key); }
  for (const auto* list : {L"Software\\Classes\\.htm\\OpenWithProgids", L"Software\\Classes\\.html\\OpenWithProgids"})
    if (RegOpenKeyExW(HKEY_CURRENT_USER, list, 0, KEY_SET_VALUE, &key) == ERROR_SUCCESS) { RegDeleteValueW(key, kHtmlId); RegCloseKey(key); }
  RemoveBrowserShortcut();
  SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
}
CefRefPtr<CefDictionaryValue> DefaultBrowserStatus() {
  auto result = CefDictionaryValue::Create();
  const auto path = InstalledBrowserPath();
  result->SetBool("installed", !path.empty()); result->SetBool("registered", Registered(path));
  const auto initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  Microsoft::WRL::ComPtr<IApplicationAssociationRegistration> registration;
  bool known = SUCCEEDED(CoCreateInstance(CLSID_ApplicationAssociationRegistration, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&registration)));
  bool is_default = true;
  if (known) for (const auto* scheme : {L"http", L"https"}) {
    PWSTR current = nullptr;
    const auto status = registration->QueryCurrentDefault(scheme, AT_URLPROTOCOL, AL_EFFECTIVE, &current);
    if (FAILED(status) || !current) known = false;
    is_default = is_default && current && _wcsicmp(current, kUrlId) == 0;
    CoTaskMemFree(current);
  }
  registration.Reset(); if (SUCCEEDED(initialized)) CoUninitialize();
  // Effective HTTP/HTTPS ownership comes from Windows, not the exact spelling
  // or completeness of our installation's registration. Keep those separate.
  if (known) result->SetBool("isDefault", is_default); else result->SetNull("isDefault");
  return result;
}
void AdoptRedLinks() {
  const auto path = InstalledBrowserPath();
  if (path.empty()) return;
  bool changed = false;
  for (const auto* id : {L"RedBrowserURL", L"RedBrowserHTML"}) {
    const auto key = std::wstring(L"Software\\Classes\\") + id + L"\\shell\\open\\command";
    const auto current = Read(key);
    if (current.empty() || current == OpenCommand(path)) continue;
    changed = Write(key, nullptr, OpenCommand(path)) || changed;
  }
  if (changed) SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
}
bool OpenDefaultBrowserSettings(HWND owner, std::string& error) {
  const auto path = InstalledBrowserPath();
  if (path.empty()) { error = "Install Sol before choosing it as your default browser."; return false; }
  if (!Register(path)) { error = "Could not register Sol with Windows. Please try reinstalling Sol."; return false; }
  // Microsoft documents this per-user deep link for current Windows 11.
  // Windows 10 opens the Default Apps page; select Sol for the web browser.
  const auto opened = reinterpret_cast<INT_PTR>(ShellExecuteW(owner, L"open",
      L"ms-settings:defaultapps?registeredAppUser=Sol", nullptr, nullptr, SW_SHOWNORMAL));
  if (opened <= 32) { error = "Could not open Windows Default Apps settings."; return false; }
  return true;
}
}  // namespace red
