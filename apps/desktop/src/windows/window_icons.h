#pragma once
namespace red {
// Windows that Chromium creates on its own (video Picture-in-Picture, and any
// other top-level Chrome_WidgetWin window of this process) take their taskbar
// and Alt+Tab icon from libcef.dll, i.e. Chromium's logo. CEF has no API for
// those windows, so watch this process's top-level windows as they appear and
// give each Sol's icon and Sol's AppUserModelID (so PiP groups with Sol on the
// taskbar). Call once on the browser UI thread before the message
// loop; the hook lives until the process exits.
void InstallWindowIcons();
}  // namespace red
