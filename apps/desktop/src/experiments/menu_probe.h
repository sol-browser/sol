#pragma once
#include "app/state.h"
namespace red {
// Experiment (--probe-menu): opens the browser menu (windows/menu_popup.cpp), hovers "Tabs" in its
// document so the submenu opens beside it, clicks "Next Tab" there, then reports whether the menu
// closed. Evidence: menu_presented (per level), menu_chosen, menu_action_ran, menu_probe_result.
void OnMenuProbeLoad(const std::shared_ptr<State>& state, const std::string& url);
}
