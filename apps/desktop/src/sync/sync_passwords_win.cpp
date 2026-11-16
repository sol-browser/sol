#include "sync/sync_passwords.h"
#include "import/formats/chromium_profile.h"
#include "import/formats/json_lite.h"
#include "storage/tree_store.h"
#include "sync/sync_crypto.h"
#include <sqlite3.h>
#include <fstream>
#include <system_error>

namespace red::sync {
namespace fs = std::filesystem;
namespace {
constexpr std::string_view kPrefix = "password/";
constexpr size_t kMaxStore = 64u * 1024 * 1024, kMaxField = 4096;

bool ReadBytes(const fs::path& path, size_t limit, std::string& out) {
  out.clear();
  std::error_code error;
  const auto size = fs::file_size(path, error);
  if (error || size > limit) return false;
  std::ifstream stream(path, std::ios::binary);
  if (!stream) return false;
  out.resize(static_cast<size_t>(size));
  return !size || static_cast<bool>(stream.read(out.data(), static_cast<std::streamsize>(size)));
}
bool JournalEmpty(const fs::path& store) {
  std::error_code error;
  auto journal = store;
  journal += "-journal";
  if (!fs::exists(journal, error)) return !error;
  const auto size = fs::file_size(journal, error);
  return !error && size == 0;
}
// SQLite header: the file change counter (bytes 24..27) moves with every write transaction.
std::string ChangeCounter(std::string_view header) { return header.size() >= 28 ? std::string(header.substr(24, 4)) : std::string(); }
std::string Column(sqlite3_stmt* statement, int column) {
  const auto* data = sqlite3_column_blob(statement, column);
  return data ? std::string(static_cast<const char*>(data), static_cast<size_t>(sqlite3_column_bytes(statement, column))) : std::string();
}
}  // namespace

bool IsPasswordKey(std::string_view key) { return key.starts_with(kPrefix); }

std::string PasswordKey(std::string_view profile, std::string_view realm, std::string_view username) {
  return std::string(kPrefix).append(profile).append("/").append(realm).append("\n").append(username);
}

bool ParsePasswordKey(std::string_view key, std::string& profile, std::string& realm, std::string& username) {
  if (!key.starts_with(kPrefix)) return false;
  key.remove_prefix(kPrefix.size());
  const auto slash = key.find('/');
  if (slash == std::string_view::npos) return false;
  const auto rest = key.substr(slash + 1);
  const auto newline = rest.find('\n');
  if (newline == std::string_view::npos) return false;
  profile = std::string(key.substr(0, slash));
  realm = std::string(rest.substr(0, newline));
  username = std::string(rest.substr(newline + 1));
  return ValidBrowsingProfileId(profile) && !realm.empty() && realm.size() <= kMaxField && username.size() <= kMaxField;
}

std::string PasswordValue(std::string_view url, std::string_view password) {
  std::string out = "{\"url\":";
  AppendJsonString(out, url);
  out += ",\"password\":";
  AppendJsonString(out, password);
  out += "}";
  return out;
}

bool ParsePasswordValue(std::string_view value, std::string& url, std::string& password) {
  url.clear();
  Wipe(password);
  auto json = importer::ParseJson(value);
  const auto* url_field = json ? json->Get("url") : nullptr;
  auto* password_field = json && json->IsObject() ? &json->fields["password"] : nullptr;
  const bool ok = url_field && url_field->type == importer::Json::Type::String && password_field &&
                  password_field->type == importer::Json::Type::String && !password_field->text.empty() &&
                  password_field->text.size() <= kMaxField && url_field->text.size() <= kMaxField;
  if (ok) { url = url_field->text; password = password_field->text; }
  if (password_field) Wipe(password_field->text);
  return ok;
}

fs::path BrowsingProfileDirectory(const fs::path& root, std::string_view profile) {
  return profile == "default" ? root / "Default" : root / ("Profile-" + std::string(profile));
}

PasswordRead ReadPasswords(const fs::path& root, std::string_view profile) {
  PasswordRead out;
  if (!ValidBrowsingProfileId(std::string(profile))) { out.status = PasswordReadStatus::Damaged; return out; }
  const auto store = BrowsingProfileDirectory(root, profile) / "Login Data";
  std::error_code error;
  if (!fs::exists(store, error)) { out.status = error ? PasswordReadStatus::Damaged : PasswordReadStatus::NoStore; return out; }

  std::string local_state, key, problem;
  bool app_bound = false;
  if (!importer::ReadSmallFile(root / "Local State", 4u * 1024 * 1024, local_state) || !importer::ChromiumMasterKey(local_state, key, app_bound, problem)) {
    out.status = PasswordReadStatus::KeyUnavailable;
    return out;
  }

  std::string bytes, header;
  if (!JournalEmpty(store) || !ReadBytes(store, kMaxStore, bytes)) { Wipe(key); out.status = PasswordReadStatus::Busy; return out; }
  if (!JournalEmpty(store) || !ReadBytes(store, kMaxStore, header) || ChangeCounter(header) != ChangeCounter(bytes) || header.size() != bytes.size()) {
    Wipe(key); Wipe(bytes); Wipe(header);
    out.status = PasswordReadStatus::Busy;
    return out;
  }
  Wipe(header);

  sqlite3* db = nullptr;
  bool ok = sqlite3_open_v2(":memory:", &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_NOMUTEX, nullptr) == SQLITE_OK &&
            sqlite3_deserialize(db, "main", reinterpret_cast<unsigned char*>(bytes.data()), static_cast<sqlite3_int64>(bytes.size()),
                                static_cast<sqlite3_int64>(bytes.size()), SQLITE_DESERIALIZE_READONLY) == SQLITE_OK;
  sqlite3_stmt* query = nullptr;
  // scheme 0 = HTML form sign-in; newest first so a duplicate realm + username keeps the latest.
  ok = ok && sqlite3_prepare_v2(db, "SELECT origin_url,signon_realm,username_value,password_value,blacklisted_by_user,scheme FROM logins "
                                    "ORDER BY date_created DESC LIMIT 50001", -1, &query, nullptr) == SQLITE_OK;
  int step = SQLITE_DONE;
  while (ok && (step = sqlite3_step(query)) == SQLITE_ROW) {
    auto url = Column(query, 0), realm = Column(query, 1), username = Column(query, 2);
    const auto blob = Column(query, 3);
    if (sqlite3_column_int64(query, 4) || sqlite3_column_int64(query, 5) != 0 || blob.empty() || !importer::IsWebUrl(url) ||
        realm.empty() || realm.size() > kMaxField || username.size() > kMaxField || out.values.size() >= 50000) { ++out.skipped; continue; }
    std::string password;
    if (importer::DecryptChromiumValue(key, blob, password) != importer::Decrypted::Ok || password.empty() || password.size() > kMaxField) {
      Wipe(password); ++out.skipped; continue;
    }
    auto record_key = PasswordKey(profile, realm, username);
    if (ValidKey(record_key) && !out.values.contains(record_key)) out.values.emplace(std::move(record_key), PasswordValue(url, password));
    Wipe(password);
  }
  if (ok && step != SQLITE_DONE) ok = false;
  sqlite3_finalize(query);
  sqlite3_close(db);
  Wipe(bytes);
  Wipe(key);
  if (!ok) { WipeValues(out.values); out.status = PasswordReadStatus::Damaged; }
  return out;
}
}  // namespace red::sync
