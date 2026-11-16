#pragma once
#include <cstdint>
#include <string>
#include <string_view>

// Portable cryptography and compression for cloud-folder sync (docs/sync.md):
// Argon2id and XChaCha20-Poly1305 from Monocypher, zstd. No OS API except
// random bytes (sync_platform.h). All functions fail closed and leave `out`
// empty on error. Secrets are byte strings that callers wipe with Wipe().
namespace red::sync {
constexpr size_t kKeySize = 32, kNonceSize = 24, kTagSize = 16;
// Largest decompressed payload accepted (a decompression-bomb limit).
constexpr size_t kMaxPlainSize = 64u * 1024 * 1024;

struct KdfParams {
  uint32_t memory_kib = 65536, iterations = 3, lanes = 4;  // RFC 9106, second recommended option.
  bool operator==(const KdfParams&) const = default;
  // Bounds accepted from an untrusted vault.json: strong enough, and not a
  // memory/time bomb on a PC that opens the folder.
  bool Acceptable() const { return memory_kib >= 19456 && memory_kib <= 1048576 && iterations >= 2 && iterations <= 16 && lanes >= 1 && lanes <= 16; }
};

void Wipe(std::string& secret);
bool RandomBytes(size_t count, std::string& out);
// Argon2id (version 0x13), `length` bytes of output.
bool Argon2id(std::string_view passphrase, std::string_view salt, const KdfParams& params, size_t length, std::string& out);
// XChaCha20-Poly1305. Sealed form: nonce (24, random) ‖ ciphertext ‖ tag (16).
bool Seal(std::string_view key, std::string_view plain, std::string_view aad, std::string& out);
bool Open(std::string_view key, std::string_view sealed, std::string_view aad, std::string& out);
// zstd frame with the content size recorded; Decompress refuses frames over kMaxPlainSize.
bool Compress(std::string_view plain, std::string& out);
bool Decompress(std::string_view frame, std::string& out);
// Standard alphabet with padding; Decode accepts nothing else (no whitespace).
std::string Base64Encode(std::string_view bytes);
bool Base64Decode(std::string_view text, std::string& out);
std::string Hex(std::string_view bytes);
}  // namespace red::sync
