#include "import/formats/import_crypto.h"
#include "import/formats/import_types.h"
#include <windows.h>
#include <bcrypt.h>
#include <wincrypt.h>
#include <dpapi.h>
#include <algorithm>
#include <array>

namespace red::import {
void Wipe(std::string& secret) { if (!secret.empty()) SecureZeroMemory(secret.data(), secret.size()); secret.clear(); }
void WipeAll(std::vector<Login>& logins) { for (auto& login : logins) Wipe(login.password); logins.clear(); }

bool IsWebUrl(const std::string& url) {
  if (url.empty() || url.size() > 4096 || std::any_of(url.begin(), url.end(), [](unsigned char c) { return c <= 32 || c == 127; })) return false;
  size_t start = 0;
  if (url.starts_with("https://")) start = 8; else if (url.starts_with("http://")) start = 7; else return false;
  const auto end = url.find_first_of("/?#", start);
  const auto authority = url.substr(start, end == std::string::npos ? std::string::npos : end - start);
  return !authority.empty() && authority.find('@') == std::string::npos && authority.find('\\') == std::string::npos && authority[0] != ':';
}
std::string CleanLabel(std::string s, size_t max, const char* fallback) {
  std::erase_if(s, [](unsigned char c) { return c < 32 || c == 127; });
  if (s.size() > max) { size_t end = max; while (end && (static_cast<unsigned char>(s[end]) & 0xc0) == 0x80) --end; s.resize(end); }
  return s.find_first_not_of(" \t") == std::string::npos ? fallback : s;
}

namespace {
class Algorithm {
 public:
  Algorithm(LPCWSTR id, ULONG flags = 0) { if (BCryptOpenAlgorithmProvider(&handle, id, nullptr, flags) != 0) handle = nullptr; }
  ~Algorithm() { if (handle) BCryptCloseAlgorithmProvider(handle, 0); }
  BCRYPT_ALG_HANDLE handle = nullptr;
};
class Key {
 public:
  ~Key() { if (handle) BCryptDestroyKey(handle); }
  BCRYPT_KEY_HANDLE handle = nullptr;
};
PUCHAR Bytes(std::string_view s) { return reinterpret_cast<PUCHAR>(const_cast<char*>(s.data())); }
}  // namespace

bool Base64Decode(std::string_view text, std::string& out) {
  out.clear(); DWORD size = 0;
  if (text.empty() || text.size() > 64 * 1024 * 1024) return false;
  if (!CryptStringToBinaryA(text.data(), static_cast<DWORD>(text.size()), CRYPT_STRING_BASE64, nullptr, &size, nullptr, nullptr)) return false;
  out.resize(size);
  if (!CryptStringToBinaryA(text.data(), static_cast<DWORD>(text.size()), CRYPT_STRING_BASE64, Bytes(out), &size, nullptr, nullptr)) { out.clear(); return false; }
  out.resize(size); return true;
}
bool DpapiUnprotect(std::string_view blob, std::string& out) {
  out.clear(); DATA_BLOB in{static_cast<DWORD>(blob.size()), Bytes(blob)}, result{};
  if (blob.empty() || !CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &result)) return false;
  out.assign(reinterpret_cast<const char*>(result.pbData), result.cbData);
  SecureZeroMemory(result.pbData, result.cbData); LocalFree(result.pbData); return true;
}
bool AesGcmDecrypt(std::string_view key, std::string_view nonce, std::string_view sealed, std::string& out) {
  out.clear();
  if (key.size() != 32 || nonce.size() != 12 || sealed.size() < 16) return false;
  Algorithm alg(BCRYPT_AES_ALGORITHM); if (!alg.handle) return false;
  if (BCryptSetProperty(alg.handle, BCRYPT_CHAINING_MODE, reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(BCRYPT_CHAIN_MODE_GCM)), sizeof(BCRYPT_CHAIN_MODE_GCM), 0) != 0) return false;
  Key k; if (BCryptGenerateSymmetricKey(alg.handle, &k.handle, nullptr, 0, Bytes(key), static_cast<ULONG>(key.size()), 0) != 0) return false;
  const auto data = sealed.substr(0, sealed.size() - 16); const auto tag = sealed.substr(sealed.size() - 16);
  BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO info; BCRYPT_INIT_AUTH_MODE_INFO(info);
  info.pbNonce = Bytes(nonce); info.cbNonce = 12; info.pbTag = Bytes(tag); info.cbTag = 16;
  std::string plain(data.size(), '\0'); ULONG written = 0;
  if (BCryptDecrypt(k.handle, Bytes(data), static_cast<ULONG>(data.size()), &info, nullptr, 0, data.empty() ? nullptr : Bytes(plain),
                    static_cast<ULONG>(plain.size()), &written, 0) != 0) { Wipe(plain); return false; }
  plain.resize(written); out = std::move(plain); return true;
}
bool CbcDecrypt(Cipher cipher, std::string_view key, std::string_view iv, std::string_view data, std::string& out) {
  out.clear();
  const size_t block = cipher == Cipher::Aes ? 16 : 8;
  if (iv.size() != block || data.empty() || data.size() % block || (cipher == Cipher::Aes ? key.size() != 32 : key.size() != 24)) return false;
  Algorithm alg(cipher == Cipher::Aes ? BCRYPT_AES_ALGORITHM : BCRYPT_3DES_ALGORITHM); if (!alg.handle) return false;
  if (BCryptSetProperty(alg.handle, BCRYPT_CHAINING_MODE, reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(BCRYPT_CHAIN_MODE_CBC)), sizeof(BCRYPT_CHAIN_MODE_CBC), 0) != 0) return false;
  Key k; if (BCryptGenerateSymmetricKey(alg.handle, &k.handle, nullptr, 0, Bytes(key), static_cast<ULONG>(key.size()), 0) != 0) return false;
  std::string vector(iv); std::string plain(data.size(), '\0'); ULONG written = 0;
  if (BCryptDecrypt(k.handle, Bytes(data), static_cast<ULONG>(data.size()), nullptr, Bytes(vector), static_cast<ULONG>(vector.size()), Bytes(plain),
                    static_cast<ULONG>(plain.size()), &written, 0) != 0) { Wipe(plain); return false; }
  plain.resize(written);
  const auto pad = static_cast<unsigned char>(plain.back());
  bool padded = pad && pad <= block && pad <= plain.size();
  for (size_t i = 0; padded && i < pad; ++i) padded = static_cast<unsigned char>(plain[plain.size() - 1 - i]) == pad;
  if (!padded) { Wipe(plain); return false; }
  plain.resize(plain.size() - pad); out = std::move(plain); return true;
}
bool Pbkdf2(Hash hash, std::string_view password, std::string_view salt, uint64_t iterations, size_t length, std::string& out) {
  out.clear();
  if (!iterations || iterations > 10'000'000 || !length || length > 64) return false;
  Algorithm alg(hash == Hash::Sha256 ? BCRYPT_SHA256_ALGORITHM : BCRYPT_SHA1_ALGORITHM, BCRYPT_ALG_HANDLE_HMAC_FLAG); if (!alg.handle) return false;
  out.resize(length);
  // CNG rejects a null key pointer even for an empty password.
  const char empty = 0;
  if (BCryptDeriveKeyPBKDF2(alg.handle, password.empty() ? reinterpret_cast<PUCHAR>(const_cast<char*>(&empty)) : Bytes(password), static_cast<ULONG>(password.size()),
                            salt.empty() ? nullptr : Bytes(salt), static_cast<ULONG>(salt.size()), iterations, Bytes(out), static_cast<ULONG>(length), 0) != 0) { Wipe(out); return false; }
  return true;
}
std::string Sha1(std::string_view data) {
  std::array<UCHAR, 20> digest{}; Algorithm alg(BCRYPT_SHA1_ALGORITHM);
  if (!alg.handle || BCryptHash(alg.handle, nullptr, 0, Bytes(data), static_cast<ULONG>(data.size()), digest.data(), static_cast<ULONG>(digest.size())) != 0) return "";
  return std::string(reinterpret_cast<const char*>(digest.data()), digest.size());
}
std::string HmacSha1(std::string_view key, std::string_view data) {
  std::array<UCHAR, 20> digest{}; Algorithm alg(BCRYPT_SHA1_ALGORITHM, BCRYPT_ALG_HANDLE_HMAC_FLAG);
  if (!alg.handle || BCryptHash(alg.handle, Bytes(key), static_cast<ULONG>(key.size()), Bytes(data), static_cast<ULONG>(data.size()), digest.data(), static_cast<ULONG>(digest.size())) != 0) return "";
  return std::string(reinterpret_cast<const char*>(digest.data()), digest.size());
}
}  // namespace red::import
