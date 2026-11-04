#pragma once
#include "app/state.h"
namespace red {
// Experiment (--probe-pin, with --load-extension of the popup fixture): pins the first installed
// extension that has a popup, checks the top bar shows its button, clicks that button in the shell
// document (a person's click), checks the popup opened, then unpins and checks the button is gone.
// Evidence: pin_probe_result {pinned_shown, icon, popup_opened, unpinned_hidden, file}.
void OnPinProbeLoad(const std::shared_ptr<State>& state, const std::string& url);
}
