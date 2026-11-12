#pragma once
#include <windows.h>
#include "include/cef_image.h"
#include "windows/branding_ids.h"
namespace red {
// Sol's icon (icons/logo_sol.svg via scripts/make_icons.py), embedded in this DLL
// as PNG resources at the Windows 100/150/200% icon scales.
inline CefRefPtr<CefImage> RedWindowIcon() {
  auto image = CefImage::CreateImage();
  HMODULE library = nullptr;
  if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                          reinterpret_cast<LPCWSTR>(&RedWindowIcon), &library)) return image;
  const struct { float scale; int id; } frames[] = {
      {1.0f, IDR_RED_ICON_32}, {1.5f, IDR_RED_ICON_48}, {2.0f, IDR_RED_ICON_64}};
  for (const auto& frame : frames) {
    HRSRC resource = FindResourceW(library, MAKEINTRESOURCEW(frame.id), MAKEINTRESOURCEW(10));  // RT_RCDATA
    HGLOBAL data = resource ? LoadResource(library, resource) : nullptr;
    const void* bytes = data ? LockResource(data) : nullptr;
    if (bytes) image->AddPNG(frame.scale, bytes, SizeofResource(library, resource));
  }
  return image;
}
}  // namespace red
