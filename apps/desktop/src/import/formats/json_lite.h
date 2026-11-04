#pragma once
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace red::import {
// A small strict JSON reader for untrusted browser profile files (Bookmarks,
// Local State, logins.json). CEF-free so the format parsers stay unit-testable.
struct Json {
  enum class Type { Null, Bool, Number, String, Array, Object };
  Type type = Type::Null;
  bool boolean = false;
  double number = 0;
  std::string text;  // String value, or the number's source text.
  std::vector<Json> items;
  std::map<std::string, Json> fields;

  const Json* Get(std::string_view key) const;
  std::string Str(std::string_view key) const;
  int64_t Int(std::string_view key, int64_t fallback = 0) const;
  bool Flag(std::string_view key) const;
  bool IsObject() const { return type == Type::Object; }
  bool IsArray() const { return type == Type::Array; }
};
// Rejects trailing data, depth > 128 and invalid UTF-16 escapes.
std::optional<Json> ParseJson(std::string_view source);
}  // namespace red::import
