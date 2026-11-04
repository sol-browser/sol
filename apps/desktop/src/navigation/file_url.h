#pragma once
#include <filesystem>
#include <string>
#include "trust_policy.h"
namespace red {
// file:/// URL for an absolute local drive path ("C:/dir/a b.pdf" ->
// "file:///C:/dir/a%20b.pdf"): UTF-8 bytes percent-encoded except unreserved
// characters, '/' and the drive colon. Empty for UNC, relative or odd paths.
inline std::string FileUrlFromGenericPath(const std::u8string& generic) {
  if (generic.size() < 3 || !std::isalpha(static_cast<unsigned char>(generic[0])) || generic[1] != u8':' || generic[2] != u8'/') return {};
  std::string url = "file:///";
  url += static_cast<char>(generic[0]); url += ':';
  constexpr char hex[] = "0123456789ABCDEF";
  for (size_t i = 2; i < generic.size(); ++i) {
    const auto c = static_cast<unsigned char>(generic[i]);
    if (std::isalnum(c) || c == '/' || c == '-' || c == '.' || c == '_' || c == '~') url += static_cast<char>(c);
    else { url += '%'; url += hex[c >> 4]; url += hex[c & 15]; }
  }
  return IsLocalFileUrl(url) ? url : std::string();
}
// The URL of an existing regular local file, or empty.
inline std::string LocalFileUrl(const std::filesystem::path& path) {
  std::error_code error;
  if (!path.is_absolute() || path.root_name().wstring().starts_with(L"\\\\") || !std::filesystem::is_regular_file(path, error) || error) return {};
  return FileUrlFromGenericPath(path.generic_u8string());
}
}  // namespace red
