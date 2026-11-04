#pragma once
#include <windows.h>
#include <filesystem>
#include <system_error>
namespace red {
// Where the browser keeps its profile, and the one-time move from the Red
// name: Red kept it in %LOCALAPPDATA%\RedBrowser\User Data, Sol keeps it in
// %LOCALAPPDATA%\Sol\User Data. The folder is renamed in place (same volume,
// atomic), so tabs, passwords, cookies and history come along unchanged.
enum class ProfileMove { None, Moved, Busy };
struct ProfileLocation {
  std::filesystem::path profile;
  ProfileMove move = ProfileMove::None;
};
inline std::filesystem::path SolProfile(const std::filesystem::path& local) { return local / L"Sol" / L"User Data"; }
inline std::filesystem::path RedProfile(const std::filesystem::path& local) { return local / L"RedBrowser" / L"User Data"; }
// Busy: the Red folder exists but could not be moved (Red is still running and
// holds it open). The caller must not start on either folder then: an empty
// Sol profile would hide the user's data, and Red's folder belongs to Red.
inline ProfileLocation ResolveProfile(const std::filesystem::path& local) {
  const auto sol = SolProfile(local), red = RedProfile(local);
  std::error_code error;
  if (std::filesystem::exists(sol, error) || error) return {sol};
  if (!std::filesystem::is_directory(red, error) || error) return {sol};
  std::filesystem::create_directories(sol.parent_path(), error);
  if (error) return {sol, ProfileMove::Busy};
  // No MOVEFILE_COPY_ALLOWED: a directory can only be renamed, never half-copied.
  if (!MoveFileExW(red.c_str(), sol.c_str(), 0)) return {sol, ProfileMove::Busy};
  return {sol, ProfileMove::Moved};
}
}  // namespace red
