#pragma once
#include <windows.h>
#include <cstring>
#include <string_view>
namespace red {
// Native UI thread only. The caller supplies an authoritative browser address,
// never arbitrary text received from a website or renderer.
inline int CopyLinkText(HWND owner, std::wstring_view text) {
  if (text.empty() || text.size() > 4096) return 400;
  const auto bytes = (text.size() + 1) * sizeof(wchar_t);
  HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, bytes);
  if (!memory) return 500;
  auto* destination = GlobalLock(memory);
  if (!destination) { GlobalFree(memory); return 500; }
  std::memcpy(destination, text.data(), text.size() * sizeof(wchar_t)); GlobalUnlock(memory);
  if (!OpenClipboard(owner)) { GlobalFree(memory); return 409; }
  const bool copied = EmptyClipboard() && SetClipboardData(CF_UNICODETEXT, memory);
  CloseClipboard();
  // After SetClipboardData succeeds Windows owns this allocation.
  if (!copied) GlobalFree(memory);
  return copied ? 0 : 500;
}
}  // namespace red
