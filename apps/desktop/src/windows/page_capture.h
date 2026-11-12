#pragma once
#include <functional>
#include <memory>
#include <string>
#include "app/state.h"
namespace red {
// Screenshots of the page in front. The PNG is saved as a download is (the download folder, and "Ask where to
// save each file" opens Windows' Save as) and/or goes onto the clipboard
// (as "PNG", which chat and design apps paste). Pictures come from Chrome's own capture of that page
// (Page.captureScreenshot); nothing is sent anywhere and nothing is put into the page. UI thread only.

// What happens to a picture: both (the menu items), or the one the capture bar's button names.
enum class ScreenshotAction { Both, Copy, Save };

// The visible part, or the whole page (menu items, command bar).
void CapturePage(const std::shared_ptr<State>& state, bool full_page);
// "Capture" (Ctrl+Shift+5, the site controls' camera): the capture window over a still picture of the
// page (windows/capture_window.cpp), where the person picks an element or an area.
void StartCapturePicker(const std::shared_ptr<State>& state);

// Saves and/or copies a finished picture, with the toast; records page_screenshot.
void DeliverScreenshot(const std::shared_ptr<State>& state, std::string png, ScreenshotAction action, bool region, bool full);
// Chrome's clipped capture of a document region (CSS pixels from the document's top left) or of the whole
// page. In the pinned Chromium a clip resizes the page's render widget until it answers, which shows on
// screen (docs/page-transitions.md): use it only where nothing else can, ideally while the page is covered.
// |done| runs once the picture was delivered or failed.
void CaptureDocument(const std::shared_ptr<State>& state, CefRefPtr<CefBrowser> page, bool full, double x, double y, double width,
                     double height, double dpr, ScreenshotAction action, std::function<void()> done = nullptr);
}  // namespace red
