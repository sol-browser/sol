#include "sync/sync_crypto.h"
#include "sync/sync_platform.h"
#include <cstdlib>
#include <memory>
#include <monocypher.h>
#include <zstd.h>

namespace red::sync {
namespace {
const uint8_t* Bytes(std::string_view s) { return reinterpret_cast<const uint8_t*>(s.data()); }
uint8_t* Bytes(std::string& s) { return reinterpret_cast<uint8_t*>(s.data()); }
constexpr int kCompressionLevel = 9;
constexpr char kBase64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
int Base64Value(char c) {
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= 'a' && c <= 'z') return c - 'a' + 26;
  if (c >= '0' && c <= '9') return c - '0' + 52;
  if (c == '+') return 62;
  if (c == '/') return 63;
  return -1;
}
struct FreeDeleter { void operator()(void* p) const { std::free(p); } };
}  // namespace

void Wipe(std::string& secret) {
  if (!secret.empty()) crypto_wipe(secret.data(), secret.size());
  secret.clear();
}

bool RandomBytes(size_t count, std::string& out) {
  out.assign(count, '\0');
  if (!platform::RandomBytes(out.data(), count)) { out.clear(); return false; }
  return true;
}

bool Argon2id(std::string_view passphrase, std::string_view salt, const KdfParams& params, size_t length, std::string& out) {
  out.clear();
  if (!params.Acceptable() || salt.size() < 16 || salt.size() > 64 || length < 16 || length > 64 || passphrase.size() > 4096 ||
      params.memory_kib < 8 * params.lanes) return false;
  // One 1 KiB block per KiB of memory; allocation failure is reported, not fatal.
  const std::unique_ptr<void, FreeDeleter> work(std::malloc(static_cast<size_t>(params.memory_kib) * 1024));
  if (!work) return false;
  out.assign(length, '\0');
  const crypto_argon2_config config{CRYPTO_ARGON2_ID, params.memory_kib, params.iterations, params.lanes};
  const crypto_argon2_inputs inputs{Bytes(passphrase), Bytes(salt), static_cast<uint32_t>(passphrase.size()), static_cast<uint32_t>(salt.size())};
  crypto_argon2(Bytes(out), static_cast<uint32_t>(length), work.get(), config, inputs, crypto_argon2_no_extras);
  crypto_wipe(work.get(), static_cast<size_t>(params.memory_kib) * 1024);
  return true;
}

bool Seal(std::string_view key, std::string_view plain, std::string_view aad, std::string& out) {
  out.clear();
  std::string nonce;
  if (key.size() != kKeySize || plain.size() > kMaxPlainSize || !RandomBytes(kNonceSize, nonce)) return false;
  out.assign(kNonceSize + plain.size() + kTagSize, '\0');
  std::copy(nonce.begin(), nonce.end(), out.begin());
  auto* cipher = Bytes(out) + kNonceSize;
  crypto_aead_lock(cipher, cipher + plain.size(), Bytes(key), Bytes(nonce), Bytes(aad), aad.size(), Bytes(plain), plain.size());
  return true;
}

bool Open(std::string_view key, std::string_view sealed, std::string_view aad, std::string& out) {
  out.clear();
  if (key.size() != kKeySize || sealed.size() < kNonceSize + kTagSize) return false;
  const size_t size = sealed.size() - kNonceSize - kTagSize;
  std::string plain(size, '\0');
  const auto* nonce = Bytes(sealed);
  if (crypto_aead_unlock(Bytes(plain), nonce + kNonceSize + size, Bytes(key), nonce, Bytes(aad), aad.size(), nonce + kNonceSize, size) != 0) {
    Wipe(plain);
    return false;
  }
  out = std::move(plain);
  return true;
}

bool Compress(std::string_view plain, std::string& out) {
  out.clear();
  if (plain.size() > kMaxPlainSize) return false;
  std::string frame(ZSTD_compressBound(plain.size()), '\0');
  const size_t written = ZSTD_compress(frame.data(), frame.size(), plain.data(), plain.size(), kCompressionLevel);
  if (ZSTD_isError(written)) return false;
  frame.resize(written);
  out = std::move(frame);
  return true;
}

bool Decompress(std::string_view frame, std::string& out) {
  out.clear();
  const unsigned long long size = ZSTD_getFrameContentSize(frame.data(), frame.size());
  // The size is always recorded by Compress; unknown or oversized frames are refused before allocating.
  if (size == ZSTD_CONTENTSIZE_UNKNOWN || size == ZSTD_CONTENTSIZE_ERROR || size > kMaxPlainSize) return false;
  std::string plain(static_cast<size_t>(size), '\0');
  const size_t written = ZSTD_decompress(plain.data(), plain.size(), frame.data(), frame.size());
  if (ZSTD_isError(written) || written != size) { Wipe(plain); return false; }
  out = std::move(plain);
  return true;
}

std::string Base64Encode(std::string_view bytes) {
  std::string out;
  out.reserve((bytes.size() + 2) / 3 * 4);
  for (size_t i = 0; i < bytes.size(); i += 3) {
    const uint32_t a = static_cast<unsigned char>(bytes[i]);
    const uint32_t b = i + 1 < bytes.size() ? static_cast<unsigned char>(bytes[i + 1]) : 0;
    const uint32_t c = i + 2 < bytes.size() ? static_cast<unsigned char>(bytes[i + 2]) : 0;
    const uint32_t triple = (a << 16) | (b << 8) | c;
    out.push_back(kBase64[(triple >> 18) & 63]);
    out.push_back(kBase64[(triple >> 12) & 63]);
    out.push_back(i + 1 < bytes.size() ? kBase64[(triple >> 6) & 63] : '=');
    out.push_back(i + 2 < bytes.size() ? kBase64[triple & 63] : '=');
  }
  return out;
}

bool Base64Decode(std::string_view text, std::string& out) {
  out.clear();
  if (text.empty() || text.size() % 4 || text.size() > 512u * 1024 * 1024) return false;
  const size_t padding = text.ends_with("==") ? 2 : text.ends_with('=') ? 1 : 0;
  std::string bytes;
  bytes.reserve(text.size() / 4 * 3);
  for (size_t i = 0; i < text.size(); i += 4) {
    const bool last = i + 4 == text.size();
    int v[4];
    for (size_t j = 0; j < 4; ++j) {
      const char c = text[i + j];
      v[j] = c == '=' && last && j >= 4 - padding ? 0 : Base64Value(c);
      if (v[j] < 0) return false;
    }
    const uint32_t triple = (static_cast<uint32_t>(v[0]) << 18) | (static_cast<uint32_t>(v[1]) << 12) | (static_cast<uint32_t>(v[2]) << 6) | static_cast<uint32_t>(v[3]);
    bytes.push_back(static_cast<char>((triple >> 16) & 255));
    if (!last || padding < 2) bytes.push_back(static_cast<char>((triple >> 8) & 255));
    if (!last || padding < 1) bytes.push_back(static_cast<char>(triple & 255));
    // Canonical form only: the bits that padding drops must be zero.
    if (last && ((padding == 2 && (triple & 0xffff)) || (padding == 1 && (triple & 0xff)))) return false;
  }
  out = std::move(bytes);
  return true;
}

std::string Hex(std::string_view bytes) {
  static constexpr char kDigits[] = "0123456789abcdef";
  std::string out;
  out.reserve(bytes.size() * 2);
  for (const unsigned char c : bytes) { out.push_back(kDigits[c >> 4]); out.push_back(kDigits[c & 15]); }
  return out;
}
}  // namespace red::sync
