#pragma once
#include <windows.h>
#include <string>
namespace red {
// Opens the Windows share sheet (Win10+ DataTransferManager) for a web link,
// anchored to `owner` (a visible top-level window). Native UI thread only; the
// caller supplies the authoritative page address, never renderer text.
// Returns false when the system cannot show it; the caller then falls back.
bool ShowShareSheet(HWND owner, const std::wstring& url, const std::wstring& title);
}  // namespace red
