#include "import/formats/firefox_logins.h"
#include "import/formats/chromium_profile.h"
#include "import/formats/import_crypto.h"
#include "import/formats/json_lite.h"
#include "import/formats/source_db.h"
#include <map>

namespace red::import {
namespace {
// Minimal DER reader: definite lengths only, as NSS writes them.
struct Der {
  unsigned char tag = 0;
  std::string_view value;
};
bool Read(std::string_view& in, Der& out) {
  if (in.size() < 2) return false;
  out.tag = static_cast<unsigned char>(in[0]); size_t length = static_cast<unsigned char>(in[1]), header = 2;
  if (length & 0x80) {
    const size_t bytes = length & 0x7f; if (!bytes || bytes > 4 || in.size() < 2 + bytes) return false;
    length = 0; for (size_t i = 0; i < bytes; ++i) length = length << 8 | static_cast<unsigned char>(in[2 + i]);
    header += bytes;
  }
  if (length > in.size() - header) return false;
  out.value = in.substr(header, length); in.remove_prefix(header + length); return true;
}
bool Expect(std::string_view& in, unsigned char tag, std::string_view& value) { Der d; if (!Read(in, d) || d.tag != tag) return false; value = d.value; return true; }
constexpr unsigned char kSequence = 0x30, kOctets = 0x04, kOid = 0x06, kInteger = 0x02;
// DER-encoded OID bodies.
constexpr std::string_view kSha1TripleDes("\x2a\x86\x48\x86\xf7\x0d\x01\x0c\x05\x01\x03", 11);
constexpr std::string_view kPbes2("\x2a\x86\x48\x86\xf7\x0d\x01\x05\x0d", 9);
constexpr std::string_view kPbkdf2("\x2a\x86\x48\x86\xf7\x0d\x01\x05\x0c", 9);
constexpr std::string_view kHmacSha256("\x2a\x86\x48\x86\xf7\x0d\x02\x09", 8);
constexpr std::string_view kAes256Cbc("\x60\x86\x48\x01\x65\x03\x04\x01\x2a", 9);
constexpr std::string_view kDesEde3Cbc("\x2a\x86\x48\x86\xf7\x0d\x03\x07", 8);
bool Integer(std::string_view v, uint64_t& out) {
  if (v.empty() || v.size() > 8) return false; out = 0;
  for (char c : v) out = out << 8 | static_cast<unsigned char>(c);
  return true;
}
enum class Unwrap { Ok, BadPassword, Unsupported };
// PBES2 EncryptedPrivateKeyInfo-like item used by key4.db (metaData.item2, nssPrivate.a11).
Unwrap DecryptPbe(std::string_view der, std::string_view global_salt, std::string_view primary, std::string& out) {
  out.clear(); std::string_view root, algorithm, oid, params, kdf, kdf_oid, kdf_params, salt, iterations, cipher, cipher_oid, iv, ciphertext;
  if (!Expect(der, kSequence, root) || !Expect(root, kSequence, algorithm) || !Expect(algorithm, kOid, oid)) return Unwrap::Unsupported;
  if (oid == kSha1TripleDes) {
    // NSS's original PBE, still found in profiles created by older Firefox:
    // SEQUENCE { entrySalt, iterations }, key/IV from chained HMAC-SHA1.
    std::string_view entry_salt, rounds_value;
    if (!Expect(algorithm, kSequence, params) || !Expect(params, kOctets, entry_salt) || !Expect(params, kInteger, rounds_value) ||
        !Expect(root, kOctets, ciphertext) || entry_salt.size() > 20) return Unwrap::Unsupported;
    std::string seed = std::string(global_salt) + std::string(primary);
    std::string hp = Sha1(seed); Wipe(seed);
    const std::string salt_bytes(entry_salt), padded = salt_bytes + std::string(20 - salt_bytes.size(), '\0');
    std::string chp = Sha1(hp + salt_bytes); Wipe(hp);
    std::string k = HmacSha1(chp, padded + salt_bytes) + HmacSha1(chp, HmacSha1(chp, padded) + salt_bytes); Wipe(chp);
    if (k.size() != 40) { Wipe(k); return Unwrap::Unsupported; }
    const bool ok = CbcDecrypt(Cipher::TripleDes, std::string_view(k).substr(0, 24), std::string_view(k).substr(32, 8), ciphertext, out); Wipe(k);
    return ok ? Unwrap::Ok : Unwrap::BadPassword;
  }
  if (oid != kPbes2) return Unwrap::Unsupported;
  if (!Expect(algorithm, kSequence, params) || !Expect(params, kSequence, kdf) || !Expect(kdf, kOid, kdf_oid) || kdf_oid != kPbkdf2 ||
      !Expect(kdf, kSequence, kdf_params) || !Expect(kdf_params, kOctets, salt) || !Expect(kdf_params, kInteger, iterations)) return Unwrap::Unsupported;
  uint64_t rounds = 0, length = 32; std::string_view key_length, prf, prf_oid;
  if (!Integer(iterations, rounds)) return Unwrap::Unsupported;
  // Optional keyLength INTEGER, then the PRF (HMAC-SHA256 for NSS).
  std::string_view peek = kdf_params; Der next;
  if (Read(peek, next) && next.tag == kInteger) { kdf_params = peek; if (!Integer(next.value, length)) return Unwrap::Unsupported; }
  if (!Expect(kdf_params, kSequence, prf) || !Expect(prf, kOid, prf_oid) || prf_oid != kHmacSha256 || length != 32) return Unwrap::Unsupported;
  if (!Expect(params, kSequence, cipher) || !Expect(cipher, kOid, cipher_oid) || cipher_oid != kAes256Cbc || !Expect(cipher, kOctets, iv) ||
      !Expect(root, kOctets, ciphertext)) return Unwrap::Unsupported;
  // NSS feeds SHA1(global salt || primary password) to PBKDF2 and, for a
  // 14-byte IV, uses the IV's DER encoding (04 0e + bytes) as the AES IV.
  std::string seed = std::string(global_salt) + std::string(primary);
  std::string hashed = Sha1(seed); Wipe(seed);
  std::string key; const bool derived = Pbkdf2(Hash::Sha256, hashed, salt, rounds, 32, key); Wipe(hashed);
  if (!derived) return Unwrap::Unsupported;
  std::string full_iv = iv.size() == 14 ? std::string("\x04\x0e", 2) + std::string(iv) : std::string(iv);
  const bool ok = CbcDecrypt(Cipher::Aes, key, full_iv, ciphertext, out); Wipe(key);
  return ok ? Unwrap::Ok : Unwrap::BadPassword;
}
// logins.json value: SEQUENCE { keyId, SEQUENCE { cipher OID, iv }, ciphertext }.
bool DecryptValue(const std::string& base64, const std::map<std::string, std::string>& keys, std::string& out) {
  out.clear(); std::string der; if (!Base64Decode(base64, der)) return false;
  std::string_view in(der), root, key_id, cipher, oid, iv, data;
  if (!Expect(in, kSequence, root) || !Expect(root, kOctets, key_id) || !Expect(root, kSequence, cipher) || !Expect(cipher, kOid, oid) ||
      !Expect(cipher, kOctets, iv) || !Expect(root, kOctets, data)) return false;
  const auto key = keys.find(std::string(key_id)); if (key == keys.end()) return false;
  if (oid == kDesEde3Cbc) return key->second.size() >= 24 && CbcDecrypt(Cipher::TripleDes, std::string_view(key->second).substr(0, 24), iv, data, out);
  if (oid == kAes256Cbc) return key->second.size() >= 32 && CbcDecrypt(Cipher::Aes, std::string_view(key->second).substr(0, 32), iv, data, out);
  return false;
}
}  // namespace

FirefoxLogins DecryptFirefoxLogins(std::string_view logins_json, std::string_view global_salt, std::string_view password_check,
                                   const std::vector<std::pair<std::string, std::string>>& wrapped_keys, std::string_view primary) {
  FirefoxLogins out;
  std::string check;
  switch (DecryptPbe(password_check, global_salt, primary, check)) {
    case Unwrap::Unsupported: out.status = FirefoxStatus::Unsupported; out.error = "This profile uses an older password storage format that Sol can't read. Export a CSV file from the browser instead."; return out;
    case Unwrap::BadPassword: break;
    case Unwrap::Ok: break;
  }
  if (check != "password-check") {
    Wipe(check);
    out.status = primary.empty() ? FirefoxStatus::PrimaryPasswordRequired : FirefoxStatus::WrongPrimaryPassword;
    out.error = primary.empty() ? "This profile is protected by a Primary Password" : "The Primary Password is not correct";
    return out;
  }
  Wipe(check);
  std::map<std::string, std::string> keys;
  for (const auto& [id, wrapped] : wrapped_keys) {
    std::string key; if (DecryptPbe(wrapped, global_salt, primary, key) == Unwrap::Ok && key.size() >= 24) keys[id] = std::move(key); else Wipe(key);
  }
  auto clear_keys = [&] { for (auto& [id, key] : keys) Wipe(key); };
  if (keys.empty()) { out.status = FirefoxStatus::Unsupported; out.error = "The profile's password key could not be read"; return out; }
  const auto doc = ParseJson(logins_json); const auto* entries = doc ? doc->Get("logins") : nullptr;
  if (!entries || !entries->IsArray()) { clear_keys(); out.status = FirefoxStatus::Error; out.error = "The saved logins file is damaged"; return out; }
  for (const auto& entry : entries->items) {
    ++out.counts.found;
    if (out.logins.size() >= 50000) { ++out.counts.skipped; continue; }
    // HTTP-auth entries (httpRealm) are not form sign-ins.
    const auto origin = entry.Str("hostname");
    if (!IsWebUrl(origin) || !entry.Str("httpRealm").empty()) { ++out.counts.skipped; continue; }
    std::string user, password;
    if (!DecryptValue(entry.Str("encryptedUsername"), keys, user) || !DecryptValue(entry.Str("encryptedPassword"), keys, password)) {
      Wipe(user); Wipe(password); ++out.counts.unreadable; continue;
    }
    if (password.empty() || password.size() > 4096 || user.size() > 4096) { Wipe(user); Wipe(password); ++out.counts.skipped; continue; }
    out.logins.emplace_back(origin, std::move(user), std::move(password)); ++out.counts.decrypted;
  }
  clear_keys();
  if (!out.counts.found) out.status = FirefoxStatus::NoLogins;
  return out;
}

FirefoxLogins ReadFirefoxLogins(const std::filesystem::path& profile, std::string_view primary) {
  FirefoxLogins out; std::error_code ec;
  if (!std::filesystem::is_regular_file(profile / "logins.json", ec)) { out.status = FirefoxStatus::NoLogins; return out; }
  if (!std::filesystem::is_regular_file(profile / "key4.db", ec)) {
    out.status = FirefoxStatus::Unsupported; out.error = "This profile has no key4.db (older format). Export a CSV file from the browser instead."; return out;
  }
  std::string json;
  if (!ReadSmallFile(profile / "logins.json", 64 * 1024 * 1024, json)) { out.status = FirefoxStatus::Error; out.error = "Cannot read saved logins"; return out; }
  SourceDb db(profile / "key4.db");
  if (!db.db) { out.status = FirefoxStatus::Error; out.error = "Cannot open the password key database. Close the browser and try again."; return out; }
  std::string salt, check; std::vector<std::pair<std::string, std::string>> wrapped;
  {
    Query meta(db.db, "SELECT item1,item2 FROM metaData WHERE id='password'");
    if (!meta.Row()) { out.status = FirefoxStatus::Unsupported; out.error = "Unsupported password key database"; return out; }
    salt = meta.Text(0); check = meta.Text(1);
  }
  {
    Query keys(db.db, "SELECT a102,a11 FROM nssPrivate LIMIT 64");
    if (!keys.s) { out.status = FirefoxStatus::Unsupported; out.error = "Unsupported password key database"; return out; }
    while (keys.Row()) wrapped.emplace_back(keys.Text(0), keys.Text(1));
  }
  return DecryptFirefoxLogins(json, salt, check, wrapped, primary);
}
}  // namespace red::import
