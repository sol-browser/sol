#include "import/formats/json_lite.h"
#include <cstdlib>

namespace red::import {
namespace {
class Parser {
 public:
  explicit Parser(std::string_view s) : s_(s) {}
  bool Parse(Json& out) { Space(); if (!Value(out, 0)) return false; Space(); return i_ == s_.size(); }

 private:
  void Space() { while (i_ < s_.size() && (s_[i_] == ' ' || s_[i_] == '\t' || s_[i_] == '\r' || s_[i_] == '\n')) ++i_; }
  bool Literal(std::string_view word) { if (s_.substr(i_, word.size()) != word) return false; i_ += word.size(); return true; }
  static void Utf8(std::string& out, uint32_t c) {
    if (c < 0x80) out += static_cast<char>(c);
    else if (c < 0x800) { out += static_cast<char>(0xc0 | c >> 6); out += static_cast<char>(0x80 | (c & 63)); }
    else if (c < 0x10000) { out += static_cast<char>(0xe0 | c >> 12); out += static_cast<char>(0x80 | (c >> 6 & 63)); out += static_cast<char>(0x80 | (c & 63)); }
    else { out += static_cast<char>(0xf0 | c >> 18); out += static_cast<char>(0x80 | (c >> 12 & 63)); out += static_cast<char>(0x80 | (c >> 6 & 63)); out += static_cast<char>(0x80 | (c & 63)); }
  }
  bool Hex4(uint32_t& v) {
    if (s_.size() - i_ < 4) return false; v = 0;
    for (int k = 0; k < 4; ++k) {
      const char c = s_[i_++]; v <<= 4;
      if (c >= '0' && c <= '9') v |= c - '0'; else if (c >= 'a' && c <= 'f') v |= c - 'a' + 10; else if (c >= 'A' && c <= 'F') v |= c - 'A' + 10; else return false;
    }
    return true;
  }
  bool String(std::string& out) {
    if (i_ >= s_.size() || s_[i_] != '"') return false; ++i_;
    while (i_ < s_.size()) {
      const auto c = static_cast<unsigned char>(s_[i_++]);
      if (c == '"') return true;
      if (c < 0x20) return false;
      if (c != '\\') { out += static_cast<char>(c); continue; }
      if (i_ >= s_.size()) return false;
      switch (s_[i_++]) {
        case '"': out += '"'; break; case '\\': out += '\\'; break; case '/': out += '/'; break;
        case 'b': out += '\b'; break; case 'f': out += '\f'; break; case 'n': out += '\n'; break;
        case 'r': out += '\r'; break; case 't': out += '\t'; break;
        case 'u': {
          uint32_t c1 = 0; if (!Hex4(c1)) return false;
          if (c1 >= 0xd800 && c1 < 0xdc00) {
            uint32_t c2 = 0; if (!Literal("\\u") || !Hex4(c2) || c2 < 0xdc00 || c2 > 0xdfff) return false;
            c1 = 0x10000 + ((c1 - 0xd800) << 10) + (c2 - 0xdc00);
          } else if (c1 >= 0xdc00 && c1 < 0xe000) return false;
          Utf8(out, c1); break;
        }
        default: return false;
      }
    }
    return false;
  }
  bool Value(Json& out, int depth) {
    if (depth > 128 || i_ >= s_.size()) return false;
    const char c = s_[i_];
    if (c == '{') {
      out.type = Json::Type::Object; ++i_; Space();
      if (i_ < s_.size() && s_[i_] == '}') { ++i_; return true; }
      while (true) {
        std::string key; Space(); if (!String(key)) return false; Space();
        if (i_ >= s_.size() || s_[i_++] != ':') return false; Space();
        Json value; if (!Value(value, depth + 1)) return false;
        out.fields.insert_or_assign(std::move(key), std::move(value)); Space();
        if (i_ >= s_.size()) return false;
        if (s_[i_] == ',') { ++i_; continue; }
        if (s_[i_] == '}') { ++i_; return true; }
        return false;
      }
    }
    if (c == '[') {
      out.type = Json::Type::Array; ++i_; Space();
      if (i_ < s_.size() && s_[i_] == ']') { ++i_; return true; }
      while (true) {
        Space(); Json value; if (!Value(value, depth + 1)) return false;
        out.items.push_back(std::move(value)); Space();
        if (i_ >= s_.size()) return false;
        if (s_[i_] == ',') { ++i_; continue; }
        if (s_[i_] == ']') { ++i_; return true; }
        return false;
      }
    }
    if (c == '"') { out.type = Json::Type::String; return String(out.text); }
    if (Literal("true")) { out.type = Json::Type::Bool; out.boolean = true; return true; }
    if (Literal("false")) { out.type = Json::Type::Bool; return true; }
    if (Literal("null")) return true;
    const size_t start = i_;
    if (s_[i_] == '-') ++i_;
    auto digits = [&] { const size_t from = i_; while (i_ < s_.size() && s_[i_] >= '0' && s_[i_] <= '9') ++i_; return i_ > from; };
    if (!digits()) return false;
    if (i_ < s_.size() && s_[i_] == '.') { ++i_; if (!digits()) return false; }
    if (i_ < s_.size() && (s_[i_] == 'e' || s_[i_] == 'E')) { ++i_; if (i_ < s_.size() && (s_[i_] == '+' || s_[i_] == '-')) ++i_; if (!digits()) return false; }
    out.type = Json::Type::Number; out.text = std::string(s_.substr(start, i_ - start)); out.number = std::strtod(out.text.c_str(), nullptr);
    return true;
  }
  std::string_view s_; size_t i_ = 0;
};
}  // namespace

const Json* Json::Get(std::string_view key) const {
  if (type != Type::Object) return nullptr;
  const auto found = fields.find(std::string(key)); return found == fields.end() ? nullptr : &found->second;
}
std::string Json::Str(std::string_view key) const { const auto* v = Get(key); return v && v->type == Type::String ? v->text : ""; }
int64_t Json::Int(std::string_view key, int64_t fallback) const {
  const auto* v = Get(key);
  if (!v) return fallback;
  // Chromium stores 64-bit times as decimal strings.
  const auto& digits = v->text;
  if ((v->type != Type::Number && v->type != Type::String) || digits.empty() || digits.size() > 19 ||
      digits.find_first_not_of("-0123456789") != std::string::npos) return fallback;
  return std::strtoll(digits.c_str(), nullptr, 10);
}
bool Json::Flag(std::string_view key) const { const auto* v = Get(key); return v && v->type == Type::Bool && v->boolean; }
std::optional<Json> ParseJson(std::string_view source) {
  if (source.starts_with("\xef\xbb\xbf")) source.remove_prefix(3);
  Json out; Parser parser(source);
  if (!parser.Parse(out)) return std::nullopt;
  return out;
}
}  // namespace red::import
