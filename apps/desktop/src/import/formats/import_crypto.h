#pragma once
#include <cstdint>
#include <string>
#include <string_view>

namespace red::import {
// Thin Windows CNG/DPAPI wrappers. Inputs and outputs are byte strings; all
// functions fail closed and leave `out` empty on any error.
bool Base64Decode(std::string_view text, std::string& out);
bool DpapiUnprotect(std::string_view blob, std::string& out);
bool AesGcmDecrypt(std::string_view key, std::string_view nonce, std::string_view sealed_with_tag, std::string& out);
enum class Cipher { Aes, TripleDes };
// CBC with PKCS#7 padding, which is checked and removed.
bool CbcDecrypt(Cipher cipher, std::string_view key, std::string_view iv, std::string_view data, std::string& out);
enum class Hash { Sha1, Sha256 };
bool Pbkdf2(Hash hash, std::string_view password, std::string_view salt, uint64_t iterations, size_t length, std::string& out);
std::string Sha1(std::string_view data);
std::string HmacSha1(std::string_view key, std::string_view data);
}  // namespace red::import
