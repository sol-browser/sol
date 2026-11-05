#include "feed.h"
#include <cctype>
#include <charconv>
#include <map>
#include <memory>
#include <variant>
#include <vector>

namespace sol_installer {
namespace {
// A small JSON reader: enough for Velopack's feed, strict about syntax.
struct Value;
using Object = std::map<std::string, Value, std::less<>>;
using Array = std::vector<Value>;
struct Value {
  std::variant<std::nullptr_t, bool, double, std::string, std::shared_ptr<Array>, std::shared_ptr<Object>> data;
};

class Reader {
 public:
  explicit Reader(std::string_view text) : text_(text) {}
  std::optional<Value> Document() {
    auto value = Parse(0);
    Space();
    if (!value || at_ != text_.size()) return std::nullopt;
    return value;
  }

 private:
  void Space() { while (at_ < text_.size() && (text_[at_] == ' ' || text_[at_] == '\t' || text_[at_] == '\n' || text_[at_] == '\r')) ++at_; }
  bool Take(char c) { Space(); if (at_ < text_.size() && text_[at_] == c) { ++at_; return true; } return false; }
  bool Word(std::string_view word) {
    if (text_.substr(at_, word.size()) != word) return false;
    at_ += word.size();
    return true;
  }
  std::optional<std::string> String() {
    if (!Take('"')) return std::nullopt;
    std::string out;
    while (at_ < text_.size()) {
      const char c = text_[at_++];
      if (c == '"') return out;
      if (static_cast<unsigned char>(c) < 0x20) return std::nullopt;
      if (c != '\\') { out += c; continue; }
      if (at_ >= text_.size()) return std::nullopt;
      const char e = text_[at_++];
      switch (e) {
        case '"': case '\\': case '/': out += e; break;
        case 'b': out += '\b'; break;
        case 'f': out += '\f'; break;
        case 'n': out += '\n'; break;
        case 'r': out += '\r'; break;
        case 't': out += '\t'; break;
        case 'u': {
          // Only ids, versions and file names matter; keep other text as '?'.
          if (text_.size() - at_ < 4) return std::nullopt;
          for (int i = 0; i < 4; ++i) if (!std::isxdigit(static_cast<unsigned char>(text_[at_ + i]))) return std::nullopt;
          at_ += 4; out += '?';
          break;
        }
        default: return std::nullopt;
      }
    }
    return std::nullopt;
  }
  std::optional<Value> Parse(int depth) {
    if (depth > 32) return std::nullopt;
    Space();
    if (at_ >= text_.size()) return std::nullopt;
    const char c = text_[at_];
    if (c == '{') {
      ++at_;
      auto object = std::make_shared<Object>();
      if (Take('}')) return Value{object};
      do {
        auto key = String();
        if (!key || !Take(':')) return std::nullopt;
        auto value = Parse(depth + 1);
        if (!value) return std::nullopt;
        (*object)[*key] = std::move(*value);
      } while (Take(','));
      if (!Take('}')) return std::nullopt;
      return Value{object};
    }
    if (c == '[') {
      ++at_;
      auto array = std::make_shared<Array>();
      if (Take(']')) return Value{array};
      do {
        auto value = Parse(depth + 1);
        if (!value) return std::nullopt;
        array->push_back(std::move(*value));
      } while (Take(','));
      if (!Take(']')) return std::nullopt;
      return Value{array};
    }
    if (c == '"') { auto s = String(); if (!s) return std::nullopt; return Value{std::move(*s)}; }
    if (Word("true")) return Value{true};
    if (Word("false")) return Value{false};
    if (Word("null")) return Value{nullptr};
    const size_t start = at_;
    if (at_ < text_.size() && text_[at_] == '-') ++at_;
    while (at_ < text_.size() && (std::isdigit(static_cast<unsigned char>(text_[at_])) || text_[at_] == '.' || text_[at_] == 'e' ||
                                  text_[at_] == 'E' || text_[at_] == '+' || text_[at_] == '-')) ++at_;
    double number = 0;
    const auto [end, error] = std::from_chars(text_.data() + start, text_.data() + at_, number);
    if (start == at_ || error != std::errc() || end != text_.data() + at_) return std::nullopt;
    return Value{number};
  }
  std::string_view text_;
  size_t at_ = 0;
};

const std::string* StringField(const Object& object, std::string_view key) {
  const auto found = object.find(key);
  return found == object.end() ? nullptr : std::get_if<std::string>(&found->second.data);
}
}  // namespace

std::optional<Version> ParseVersion(std::string_view text) {
  Version version;
  int* parts[] = {&version.major, &version.minor, &version.patch};
  for (int i = 0; i < 3; ++i) {
    const size_t dot = i < 2 ? text.find('.') : text.size();
    if (dot == std::string_view::npos) return std::nullopt;
    const auto part = text.substr(0, dot);
    if (part.empty() || part.size() > 5 || (part.size() > 1 && part[0] == '0')) return std::nullopt;
    const auto [end, error] = std::from_chars(part.data(), part.data() + part.size(), *parts[i]);
    if (error != std::errc() || end != part.data() + part.size()) return std::nullopt;
    text = dot < text.size() ? text.substr(dot + 1) : std::string_view();
  }
  return version;
}

std::string ToString(const Version& version) {
  return std::to_string(version.major) + "." + std::to_string(version.minor) + "." + std::to_string(version.patch);
}

bool ValidPackageId(std::string_view id) {
  if (id.empty() || id.size() > 64) return false;
  for (const char c : id) if (!std::isalnum(static_cast<unsigned char>(c))) return false;
  return true;
}

std::optional<Release> LatestFullRelease(std::string_view json) {
  const auto document = Reader(json).Document();
  if (!document) return std::nullopt;
  const auto* root = std::get_if<std::shared_ptr<Object>>(&document->data);
  if (!root) return std::nullopt;
  const auto assets = (*root)->find("Assets");
  if (assets == (*root)->end()) return std::nullopt;
  const auto* list = std::get_if<std::shared_ptr<Array>>(&assets->second.data);
  if (!list) return std::nullopt;
  std::optional<Release> best;
  for (const auto& item : **list) {
    const auto* asset = std::get_if<std::shared_ptr<Object>>(&item.data);
    if (!asset) continue;
    const auto* type = StringField(**asset, "Type");
    const auto* id = StringField(**asset, "PackageId");
    const auto* number = StringField(**asset, "Version");
    if (!type || *type != "Full" || !id || !ValidPackageId(*id) || !number) continue;
    const auto version = ParseVersion(*number);
    if (!version) continue;
    if (best && best->package_id != *id) return std::nullopt;  // One product per feed.
    if (!best || *version > best->version) best = Release{*id, *version};
  }
  return best;
}

std::optional<Checksum> SetupChecksum(std::string_view sums, const Version& version) {
  const std::string wanted = "Sol-Setup-" + ToString(version) + ".exe";
  while (!sums.empty()) {
    const size_t end = sums.find('\n');
    auto line = sums.substr(0, end);
    sums = end == std::string_view::npos ? std::string_view() : sums.substr(end + 1);
    if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
    if (line.size() < 66 || line.substr(64, 2) != "  " || line.substr(66) != wanted) continue;
    std::string hash(line.substr(0, 64));
    bool hex = true;
    for (auto& c : hash) {
      if (!std::isxdigit(static_cast<unsigned char>(c))) hex = false;
      c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    if (hex) return Checksum{wanted, hash};
  }
  return std::nullopt;
}
}  // namespace sol_installer
