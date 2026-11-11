#pragma once
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace red {
// After an update, Sol opens what changed in the new version, once.
//
// The last version that ran is kept in <profile>/BrowserData/last-version. On each start of the
// workspace browser:
// - a new profile (it gets the welcome instead) or a profile on the same version: nothing opens;
// - a newer version than last time: its release notes open, and the version is remembered;
// - an older version (a downgrade): nothing opens, the older version is remembered;
// - a profile from before this marker existed counts as updated, so the first version that ships
//   this behaviour shows its notes too.

// Where a version's release notes live. Website anchors use dashes: 0.2.1 -> #v0-2-1.
std::string ReleaseNotesUrl(std::string_view version);

// Compares dotted numeric versions ("0.2.10" > "0.2.9"). Missing parts count as 0; anything that is
// not a number makes the version invalid (std::nullopt), so a dev label never triggers a tab.
std::optional<int> CompareVersions(std::string_view a, std::string_view b);

// Updates (app/updates.cpp): whether the feed's |latest| version should be downloaded while
// |waiting| is already downloaded (empty: nothing waits). The same version, or an older one, is
// not; a version that cannot be compared counts as different.
bool UpdateReplacesWaiting(std::string_view latest, std::string_view waiting);

// Reads and updates the marker in |browser_data|; returns the URL to open, if any.
// |fresh_profile|: the profile was created by this start.
std::optional<std::string> ReleaseNotesAfterUpdate(const std::filesystem::path& browser_data,
                                                   std::string_view current_version, bool fresh_profile);
}  // namespace red
