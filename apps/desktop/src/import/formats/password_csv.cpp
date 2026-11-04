#include "import/formats/password_csv.h"
#include <algorithm>
#include <cctype>

namespace red::import {
namespace {
constexpr size_t kMaxRows = 50000;
// Returns false on an unterminated quote. Fields of the row land in `row`.
bool NextRow(std::string_view text, size_t& i, std::vector<std::string>& row) {
  row.clear(); std::string field; bool quoted = false, any = false;
  while (i < text.size()) {
    const char c = text[i++]; any = true;
    if (quoted) {
      if (c != '"') { field += c; continue; }
      if (i < text.size() && text[i] == '"') { field += '"'; ++i; continue; }
      quoted = false; continue;
    }
    if (c == '"' && field.empty()) { quoted = true; continue; }
    if (c == ',') { row.push_back(std::move(field)); field.clear(); continue; }
    if (c == '\r' && i < text.size() && text[i] == '\n') continue;
    if (c == '\n' || c == '\r') break;
    field += c;
  }
  if (quoted) { Wipe(field); return false; }
  if (any) row.push_back(std::move(field));
  return true;
}
std::string Lower(std::string s) { for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); return s; }
void WipeRow(std::vector<std::string>& row) { for (auto& field : row) Wipe(field); row.clear(); }
}  // namespace

CsvResult ParsePasswordCsv(std::string_view text) {
  CsvResult out;
  if (text.starts_with("\xef\xbb\xbf")) text.remove_prefix(3);
  size_t i = 0; std::vector<std::string> row;
  if (!NextRow(text, i, row) || row.empty()) { out.error = "This file is empty or not a CSV file"; return out; }
  int url = -1, user = -1, pass = -1;
  for (int c = 0; c < static_cast<int>(row.size()); ++c) {
    const auto name = Lower(row[c]);
    if (url < 0 && (name == "url" || name == "origin" || name == "website" || name == "login_uri" || name == "web site")) url = c;
    else if (user < 0 && (name == "username" || name == "login" || name == "login_username" || name == "user name")) user = c;
    else if (pass < 0 && (name == "password" || name == "login_password")) pass = c;
  }
  if (url < 0 || user < 0 || pass < 0) { out.error = "This CSV file has no url, username and password columns"; return out; }
  const auto need = static_cast<size_t>(std::max({url, user, pass}));
  while (i < text.size()) {
    if (!NextRow(text, i, row)) { WipeRow(row); WipeAll(out.logins); out.error = "The CSV file is damaged (unterminated quote)"; return out; }
    if (row.empty() || (row.size() == 1 && row[0].empty())) continue;
    ++out.counts.found;
    if (row.size() <= need || out.logins.size() >= kMaxRows) { ++out.counts.unreadable; WipeRow(row); continue; }
    auto& origin = row[url];
    // Firefox writes bare origins; Chrome writes full URLs. Both are web URLs.
    if (!IsWebUrl(origin) || row[pass].empty() || row[user].size() > 4096 || row[pass].size() > 4096) { ++out.counts.skipped; WipeRow(row); continue; }
    out.logins.emplace_back(std::move(origin), std::move(row[user]), std::move(row[pass]));
    ++out.counts.decrypted; WipeRow(row);
  }
  if (!out.counts.found) out.error = "This CSV file has no saved passwords";
  return out;
}
}  // namespace red::import
