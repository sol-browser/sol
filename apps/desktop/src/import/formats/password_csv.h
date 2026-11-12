#pragma once
#include "import/formats/import_types.h"
#include <string_view>

namespace red::importer {
// Password exports made by the user from Chrome, Edge, Arc (Chrome format:
// name,url,username,password,note) or Firefox/Zen (url,username,password,...).
// Columns are found by header name; RFC 4180 quoting and CRLF/LF are accepted.
struct CsvResult {
  std::vector<Login> logins;
  PasswordCounts counts;
  std::string error;  // Set when the file is not a recognised password export.
};
CsvResult ParsePasswordCsv(std::string_view text);
}  // namespace red::importer
