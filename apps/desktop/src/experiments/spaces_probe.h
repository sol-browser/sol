#pragma once
#include "app/state.h"
namespace red {
// Acceptance (--probe-spaces): on /home, create a "Work" space, open /work in
// it, switch back and forth (Ctrl+1/2 path), open the space editor, then
// delete the space. Each step records the active space/page; the smoke script
// screenshots between steps.
void OnSpacesProbeLoad(const std::shared_ptr<State>& state, const std::string& url);
}
