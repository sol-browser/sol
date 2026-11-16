#pragma once
#include <optional>
#include <string>
#include <string_view>
#include <windows.h>
#include "navigation/key_binding.h"

namespace red {
constexpr KeyBinding kDefaultLittleSolHotkey{'N', true, false, true};
constexpr std::string_view kLittleSolHotkeyAction = "littleSolGlobal";
constexpr std::wstring_view kLittleSolHotkeySwitch = L"--little-sol-hotkey=";

inline UINT LittleSolModifiers(const KeyBinding& binding) {
  return (binding.ctrl ? MOD_CONTROL : 0) | (binding.alt ? MOD_ALT : 0) | (binding.shift ? MOD_SHIFT : 0);
}

inline bool ValidLittleSolHotkey(const KeyBinding& binding) {
  // Require two modifiers so this global accelerator leaves common shortcuts available in other apps.
  const auto modifiers = LittleSolModifiers(binding);
  return modifiers >= 3 && modifiers != MOD_SHIFT &&
      ((binding.key >= 'A' && binding.key <= 'Z') || (binding.key >= '0' && binding.key <= '9') ||
       (binding.key >= VK_F1 && binding.key <= VK_F24 && binding.key != VK_F12));
}

inline bool RegisterLittleSolHotkey(HWND window, int id, const KeyBinding& binding) {
  return RegisterHotKey(window, id, LittleSolModifiers(binding) | MOD_NOREPEAT, static_cast<UINT>(binding.key)) != FALSE;
}

inline std::string LittleSolHotkeyLabel(const KeyBinding& binding) {
  std::string label;
  if (binding.ctrl) label += "Ctrl+";
  if (binding.alt) label += "Alt+";
  if (binding.shift) label += "Shift+";
  if (binding.key >= VK_F1 && binding.key <= VK_F24) return label + "F" + std::to_string(binding.key - VK_F1 + 1);
  return label + static_cast<char>(binding.key);
}

inline std::wstring LittleSolHotkeyArgument(const KeyBinding& binding) {
  if (binding == kDefaultLittleSolHotkey) return {};
  return L" " + std::wstring(kLittleSolHotkeySwitch) +
      std::to_wstring((LittleSolModifiers(binding) << 8) | static_cast<UINT>(binding.key));
}

inline std::optional<KeyBinding> ParseLittleSolHotkey(std::wstring_view value) {
  if (value.empty() || value.size() > 4) return std::nullopt;
  unsigned encoded = 0;
  for (const auto digit : value) {
    if (digit < L'0' || digit > L'9') return std::nullopt;
    encoded = encoded * 10 + static_cast<unsigned>(digit - L'0');
  }
  if (encoded > 0x7ff) return std::nullopt;
  const auto modifiers = encoded >> 8;
  const KeyBinding binding{static_cast<int>(encoded & 0xff), (modifiers & MOD_CONTROL) != 0,
                           (modifiers & MOD_SHIFT) != 0, (modifiers & MOD_ALT) != 0};
  return ValidLittleSolHotkey(binding) ? std::optional{binding} : std::nullopt;
}
}  // namespace red
