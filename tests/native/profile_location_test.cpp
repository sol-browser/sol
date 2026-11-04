#include "app/profile_location.h"
#include <cstdio>
#include <fstream>

namespace {
int failures = 0;
void check(bool ok, const char* name) {
  if (!ok) { std::fprintf(stderr, "FAIL: %s\n", name); ++failures; }
}
std::filesystem::path Fresh(const wchar_t* name) {
  wchar_t temp[MAX_PATH]{};
  GetTempPathW(MAX_PATH, temp);
  const auto root = std::filesystem::path(temp) / (std::wstring(L"sol-profile-test-") + name + L"-" + std::to_wstring(GetCurrentProcessId()));
  std::error_code error;
  std::filesystem::remove_all(root, error);
  std::filesystem::create_directories(root);
  return root;
}
void Touch(const std::filesystem::path& file) {
  std::filesystem::create_directories(file.parent_path());
  std::ofstream(file) << "data";
}
}  // namespace

int main() {
  {
    const auto local = Fresh(L"fresh");
    const auto result = red::ResolveProfile(local);
    check(result.profile == red::SolProfile(local) && result.move == red::ProfileMove::None, "fresh install uses Sol's folder");
  }
  {
    const auto local = Fresh(L"move");
    Touch(red::RedProfile(local) / L"Default" / L"Cookies");
    const auto result = red::ResolveProfile(local);
    check(result.move == red::ProfileMove::Moved, "Red profile moves");
    check(std::filesystem::exists(red::SolProfile(local) / L"Default" / L"Cookies"), "moved data is in Sol's folder");
    check(!std::filesystem::exists(red::RedProfile(local)), "Red's folder is gone");
    check(red::ResolveProfile(local).move == red::ProfileMove::None, "second start does not move again");
  }
  {
    const auto local = Fresh(L"both");
    Touch(red::RedProfile(local) / L"old");
    Touch(red::SolProfile(local) / L"new");
    const auto result = red::ResolveProfile(local);
    check(result.move == red::ProfileMove::None && std::filesystem::exists(red::RedProfile(local) / L"old"),
          "an existing Sol profile wins and Red's is left alone");
  }
  {
    const auto local = Fresh(L"busy");
    Touch(red::RedProfile(local) / L"lock");
    // An open handle without FILE_SHARE_DELETE blocks the rename, as a running Red does.
    const auto file = (red::RedProfile(local) / L"lock").wstring();
    HANDLE held = CreateFileW(file.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    const auto result = red::ResolveProfile(local);
    CloseHandle(held);
    check(result.move == red::ProfileMove::Busy, "a Red profile in use is reported busy");
    check(std::filesystem::exists(red::RedProfile(local) / L"lock"), "busy profile is untouched");
  }
  return failures ? 1 : 0;
}
