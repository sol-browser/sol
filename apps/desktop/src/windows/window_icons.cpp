#include "windows/window_icons.h"
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <propkey.h>
#include <propvarutil.h>
#include <wrl/client.h>
#include <cwchar>
#include "windows/branding_ids.h"

namespace red {
namespace {
HICON big_icon = nullptr, small_icon = nullptr;

// Sol's icon from this DLL's PNG resources (resources/red.rc), sized for the
// system's big and small icon metrics.
HICON LoadSolIcon(int id, int size) {
  HMODULE library = nullptr;
  if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                          reinterpret_cast<LPCWSTR>(&LoadSolIcon), &library)) return nullptr;
  HRSRC resource = FindResourceW(library, MAKEINTRESOURCEW(id), MAKEINTRESOURCEW(10));  // RT_RCDATA
  HGLOBAL data = resource ? LoadResource(library, resource) : nullptr;
  auto* bytes = data ? static_cast<BYTE*>(LockResource(data)) : nullptr;
  if (!bytes) return nullptr;
  // PNG icon images are accepted since Windows Vista; the size scales the frame.
  return CreateIconFromResourceEx(bytes, SizeofResource(library, resource), TRUE, 0x00030000, size, size, LR_DEFAULTCOLOR);
}

// Chromium labels its own windows with Chromium's AppUserModelID, so the
// taskbar groups them apart from Sol. Give the window this process's ID (Sol's,
// or the probe's), so PiP stacks under Sol's taskbar button.
void JoinSolGroup(HWND window) {
  PWSTR process_id = nullptr;
  // Only the installed Sol has one (main_win.cpp); development builds keep Windows' default grouping.
  if (FAILED(GetCurrentProcessExplicitAppUserModelID(&process_id)) || !process_id) return;
  Microsoft::WRL::ComPtr<IPropertyStore> store;
  if (SUCCEEDED(SHGetPropertyStoreForWindow(window, IID_PPV_ARGS(&store)))) {
    PROPVARIANT current{};
    const bool same = SUCCEEDED(store->GetValue(PKEY_AppUserModel_ID, &current)) && current.vt == VT_LPWSTR &&
        current.pwszVal && wcscmp(current.pwszVal, process_id) == 0;
    PropVariantClear(&current);
    if (!same) {
      PROPVARIANT id{}; id.vt = VT_LPWSTR; id.pwszVal = process_id;
      store->SetValue(PKEY_AppUserModel_ID, id);
    }
  }
  CoTaskMemFree(process_id);
}

void CALLBACK OnShow(HWINEVENTHOOK, DWORD, HWND window, LONG object, LONG child, DWORD, DWORD) {
  if (object != OBJID_WINDOW || child != CHILDID_SELF || !window || GetAncestor(window, GA_ROOT) != window) return;
  wchar_t name[64]{};
  if (!GetClassNameW(window, name, 64) ||
      (wcsncmp(name, L"Chrome_WidgetWin_", 17) != 0 && wcscmp(name, L"SolWebsitePopup") != 0)) return;
  JoinSolGroup(window);
  if (reinterpret_cast<HICON>(SendMessageW(window, WM_GETICON, ICON_BIG, 0)) == big_icon) return;
  SendMessageW(window, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(big_icon));
  SendMessageW(window, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(small_icon));
}
}  // namespace

void InstallWindowIcons() {
  if (big_icon) return;
  const int large_size = GetSystemMetrics(SM_CXICON), small_size = GetSystemMetrics(SM_CXSMICON);
  big_icon = LoadSolIcon(large_size > 48 ? IDR_RED_ICON_64 : large_size > 32 ? IDR_RED_ICON_48 : IDR_RED_ICON_32, large_size);
  small_icon = LoadSolIcon(small_size > 32 ? IDR_RED_ICON_48 : IDR_RED_ICON_32, small_size);
  if (!big_icon || !small_icon) return;
  // Out of context: the callback runs on this thread's message loop, for this process only.
  SetWinEventHook(EVENT_OBJECT_SHOW, EVENT_OBJECT_SHOW, nullptr, OnShow, GetCurrentProcessId(), 0, WINEVENT_OUTOFCONTEXT);
}
}  // namespace red
