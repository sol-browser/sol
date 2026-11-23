#pragma once
#include <filesystem>
#include <string_view>

namespace red {
// Protected content (docs/codecs.md#protected-content-install). Pure layout
// rules shared by the installer and the helper host; no CEF.

// Where helper versions live. In a Velopack install that is
// <root>\protected-media beside "current": updates replace only "current",
// and uninstalling Sol removes the whole root. A development build keeps them
// in <exe dir>\protected-media.
std::filesystem::path ProtectedMediaBase(const std::filesystem::path& exe_dir);

// A version folder name: "<ECS version>-<package revision>", e.g. 45.2.1-1.
bool ProtectedMediaVersionName(std::wstring_view name);

// The signed executable, its VMP signature and the app archive are present.
bool ProtectedMediaComplete(const std::filesystem::path& runtime);

// The helper Sol starts: one staged by hand directly in |base| (development),
// else the pinned |id|, else the newest other complete version, which keeps
// working until the pinned one is installed. Empty when there is none.
std::filesystem::path ProtectedMediaRuntime(const std::filesystem::path& base, std::string_view id);

// |host| is |domain| or one of its subdomains (help.netflix.com in
// netflix.com, not notnetflix.com). Expects canonical (lowercase) hosts.
bool ProtectedMediaHostInDomain(std::string_view host, std::string_view domain);

// |host| is one of the space-separated |hosts| ("www.hbomax.com play.hbomax.com").
bool ProtectedMediaHostListed(std::string_view host, std::string_view hosts);

// SOL_PROTECTED_MEDIA_URL may replace the download address for testing: an
// https URL, or http on the loopback host. The pinned size and SHA-256 still apply.
bool ProtectedMediaSourceAllowed(std::string_view url);
}  // namespace red
