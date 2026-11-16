#pragma once

namespace red {
struct KeyBinding {
  int key = 0;
  bool ctrl = false, shift = false, alt = false;
  bool operator==(const KeyBinding&) const = default;
};
}  // namespace red
