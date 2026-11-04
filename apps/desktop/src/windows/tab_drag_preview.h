#pragma once
#include "app/state.h"
namespace red {
void ShowTabDragPreview(const std::shared_ptr<State>& state, const std::string& id);
void HideTabDragPreview(const std::shared_ptr<State>& state);
}
