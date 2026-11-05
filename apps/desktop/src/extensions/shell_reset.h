#pragma once
#include <chrono>
#include <filesystem>

namespace red {
// Recovery from the Shell profile guard (profile_guard.cpp). When a
// third-party extension turns up in the trusted UI profile, Sol closes; once
// CEF has shut down (nothing holds the profile any more) it deletes
// <profile>\Shell, which keeps no user data, and starts again with a clean
// interface profile. A finished reset is remembered, so an extension that
// comes straight back closes Sol instead of restarting it in a loop.
enum class ShellReset { Done, Failed };

// Whether the guard may reset now: no reset finished within |window| before |now|.
bool ShellResetAllowed(const std::filesystem::path& profile, std::filesystem::file_time_type now,
                       std::chrono::minutes window = std::chrono::minutes(10));
// After CefShutdown: deletes the Shell profile and remembers the reset.
ShellReset ResetShellProfile(const std::filesystem::path& profile);

// The guard's request to reset and start Sol again once this process shuts down.
void ScheduleShellReset();
bool ShellResetScheduled();
// Starts this executable again with its own command line.
bool Relaunch();
}  // namespace red
