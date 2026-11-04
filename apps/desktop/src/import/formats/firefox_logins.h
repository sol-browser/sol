#pragma once
#include "import/formats/import_types.h"
#include <filesystem>
#include <string_view>

namespace red::import {
// Firefox/Zen saved logins: logins.json values encrypted with a key stored in
// the NSS key4.db, wrapped with PBES2 (PBKDF2-HMAC-SHA256 + AES-256-CBC) or
// NSS's older SHA1/3DES PBE. Legacy key3.db (pre-Firefox 58) is not supported.
enum class FirefoxStatus { Ok, NoLogins, PrimaryPasswordRequired, WrongPrimaryPassword, Unsupported, Error };
struct FirefoxLogins {
  FirefoxStatus status = FirefoxStatus::Ok;
  std::vector<Login> logins;
  PasswordCounts counts;
  std::string error;
};
// `primary_password` is the profile's Primary Password (empty when none).
FirefoxLogins ReadFirefoxLogins(const std::filesystem::path& profile, std::string_view primary_password);
// Format pieces, exposed for tests.
FirefoxLogins DecryptFirefoxLogins(std::string_view logins_json, std::string_view global_salt, std::string_view password_check,
                                   const std::vector<std::pair<std::string, std::string>>& wrapped_keys, std::string_view primary_password);
}  // namespace red::import
