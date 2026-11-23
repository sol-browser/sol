#pragma once
#include <filesystem>

namespace red {
enum class ShellQuarantine { Missing, Moved, Failed };
// Call only while Chromium is not using Shell. Keep the entire old directory.
ShellQuarantine QuarantineShellProfile(const std::filesystem::path& profile);

// Claim a single emergency restart for this user-data root. Reserve it before
// shutting down, so a failed repair or a repeated violation cannot relaunch.
bool ScheduleShellReset(const std::filesystem::path& profile);
bool ShellResetScheduled();
bool ShellResetPending(const std::filesystem::path& profile);
// Starts this executable again with its own command line.
bool Relaunch();
}  // namespace red
