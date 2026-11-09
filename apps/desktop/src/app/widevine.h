#pragma once
#include <memory>

namespace red {
struct State;

// Chromium's Widevine CDM component (the DRM module Spotify, Netflix and others need).
inline constexpr char kWidevineComponentId[] = "oimompecagnajdejgnnjijobebaeigek";

// Makes sure the Widevine CDM gets installed: when the component updater has not installed it yet,
// asks for it now instead of waiting for the updater's own schedule. Records what happened.
void EnsureWidevine(const std::shared_ptr<State>& state);
}  // namespace red
