#pragma once
#include <memory>
#include "app/state.h"
#include "include/cef_browser.h"

namespace red {
// Built-in Picture-in-Picture (Alt+P, like Google's extension): the page's
// video pops out (playing first, then largest), or the page's PiP closes.
void TogglePictureInPicture(CefRefPtr<CefBrowser> page);
// Whether leaving |page| may pop its video out: the site's switch, else the
// profile default (off).
bool AutoPipEnabled(const std::shared_ptr<State>& state, CefRefPtr<CefBrowser> page);
// Arc-style automatic Picture-in-Picture. Chrome's own auto-PiP needs a tab
// strip and a media-session "enterpictureinpicture" handler (not YouTube), so
// Red triggers it itself (docs/auto-pip.md): when a page stops being visible
// while it plays an audible video, its largest such <video> enters PiP through
// a DevTools user gesture; when the page is shown again, the PiP that Red
// opened closes. Pages and their own PiP windows are otherwise untouched.
void OnPageSwitched(const std::shared_ptr<State>& state, CefRefPtr<CefBrowser> left, CefRefPtr<CefBrowser> shown);
// Firefox-style button over videos on websites (video_pip_button.cpp):
// installed in an isolated world of each new website page when enabled.
void InstallVideoPipButton(const std::shared_ptr<State>& state, CefRefPtr<CefBrowser> page);
}  // namespace red
