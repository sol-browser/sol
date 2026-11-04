#pragma once
#include "app/state.h"
namespace red {
// Acceptance (--probe-extension-popup, with --load-extension of the owned
// popup fixture): open /second, switch back to the first tab, open the site
// popover and click the fixture's extension button like a person. The popup
// window must load the extension popup, resolve chrome.tabs.query to the first
// (visible) tab, follow the document's size and close on window.close().
void OnExtensionPopupProbeLoad(const std::shared_ptr<State>& state, const std::string& url);
// After the popup closed: runs the popup-less "Action Probe" fixture from the
// site panel; the probe ends on its onClicked evidence or after a timeout.
void OnExtensionPopupProbeClosed(const std::shared_ptr<State>& state);
// Page console evidence of action.onClicked from the action fixture.
void ObserveExtensionActionProbe(const std::shared_ptr<State>& state, const std::string& message);
}
