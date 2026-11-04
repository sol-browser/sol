#include "windows/share_sheet.h"
#include <roapi.h>
#include <shobjidl_core.h>
#include <windows.applicationmodel.datatransfer.h>
#include <windows.foundation.h>
#include <wrl/client.h>
#include <wrl/event.h>
#include <wrl/wrappers/corewrappers.h>

namespace red {
namespace {
using namespace ABI::Windows::ApplicationModel::DataTransfer;
using ABI::Windows::Foundation::ITypedEventHandler;
using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;
using Microsoft::WRL::Wrappers::HStringReference;

// One share at a time: the sheet is modal to its window, and re-registering on
// every call means a destroyed (and possibly reused) HWND never keeps a stale
// manager. DataRequested is raised on the owner's thread, which is ours.
struct Pending {
  ComPtr<IDataTransferManager> manager;
  EventRegistrationToken token{};
  std::wstring url, title;
};
Pending& Slot() { static Pending pending; return pending; }

void Unregister() {
  auto& slot = Slot();
  if (slot.manager) slot.manager->remove_DataRequested(slot.token);
  slot.manager.Reset(); slot.token = {};
}

HRESULT Fill(IDataRequestedEventArgs* args) {
  const auto& slot = Slot();
  ComPtr<IDataRequest> request; ComPtr<IDataPackage> package; ComPtr<IDataPackagePropertySet> properties;
  if (FAILED(args->get_Request(&request)) || FAILED(request->get_Data(&package)) || FAILED(package->get_Properties(&properties))) return E_FAIL;
  // A title is required, or the sheet reports "nothing to share".
  const auto& title = slot.title.empty() ? slot.url : slot.title;
  properties->put_Title(HStringReference(title.c_str(), static_cast<unsigned>(title.size())).Get());
  ComPtr<ABI::Windows::Foundation::IUriRuntimeClassFactory> factory; ComPtr<ABI::Windows::Foundation::IUriRuntimeClass> uri;
  ComPtr<IDataPackage2> web;
  if (SUCCEEDED(RoGetActivationFactory(HStringReference(RuntimeClass_Windows_Foundation_Uri).Get(), IID_PPV_ARGS(&factory)))
      && SUCCEEDED(factory->CreateUri(HStringReference(slot.url.c_str(), static_cast<unsigned>(slot.url.size())).Get(), &uri))
      && SUCCEEDED(package.As(&web)) && SUCCEEDED(web->SetWebLink(uri.Get()))) return S_OK;
  // Not a URI Windows accepts (unusual schemes): share it as text instead.
  return package->SetText(HStringReference(slot.url.c_str(), static_cast<unsigned>(slot.url.size())).Get());
}
}  // namespace

bool ShowShareSheet(HWND owner, const std::wstring& url, const std::wstring& title) {
  if (!owner || !IsWindowVisible(owner) || url.empty() || url.size() > 4096) return false;
  ComPtr<IDataTransferManagerInterop> interop;
  if (FAILED(RoGetActivationFactory(HStringReference(RuntimeClass_Windows_ApplicationModel_DataTransfer_DataTransferManager).Get(), IID_PPV_ARGS(&interop))))
    return false;
  Unregister();
  auto& slot = Slot();
  slot.url = url; slot.title = title.substr(0, 240);
  ComPtr<IDataTransferManager> manager;
  if (FAILED(interop->GetForWindow(owner, IID_PPV_ARGS(&manager)))) return false;
  const auto handler = Callback<ITypedEventHandler<DataTransferManager*, DataRequestedEventArgs*>>(
      [](IDataTransferManager*, IDataRequestedEventArgs* args) { return Fill(args); });
  if (!handler || FAILED(manager->add_DataRequested(handler.Get(), &slot.token))) return false;
  slot.manager = manager;
  if (FAILED(interop->ShowShareUIForWindow(owner))) { Unregister(); return false; }
  return true;
}
}  // namespace red
