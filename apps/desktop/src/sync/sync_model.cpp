#include "sync/sync_model.h"
#include "import/formats/json_lite.h"
#include "sync/sync_crypto.h"
#include <algorithm>

namespace red::sync {
namespace {
constexpr size_t kMaxKey = 2048, kMaxValue = 1u << 20, kMaxRecords = 500000;
constexpr int64_t kMaxMs = 253402300799999;  // 9999-12-31, rejects absurd remote clocks.

bool ValidStamp(const Stamp& stamp) { return stamp.ms > 0 && stamp.ms <= kMaxMs && ValidDeviceId(stamp.device); }
}  // namespace

bool ValidDeviceId(std::string_view id) {
  return id.size() == 32 && std::all_of(id.begin(), id.end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}

bool ValidKey(std::string_view key) {
  if (key.empty() || key.size() > kMaxKey) return false;
  const auto slash = key.find('/');
  return slash != std::string_view::npos && slash > 0 && slash + 1 < key.size() &&
         std::none_of(key.begin(), key.end(), [](char c) { return c == '\0'; });
}

Stamp Clock::Next(int64_t now_ms) {
  if (now_ms > ms_) { ms_ = now_ms; counter_ = 0; }
  else if (counter_ == UINT32_MAX) { ++ms_; counter_ = 0; }
  else ++counter_;
  return Stamp{ms_, counter_, device_};
}

void Clock::Observe(const Stamp& stamp) {
  if (stamp.ms > ms_ || (stamp.ms == ms_ && stamp.counter > counter_)) { ms_ = stamp.ms; counter_ = stamp.counter; }
}

void Clock::Observe(const State& state) { for (const auto& [key, record] : state) Observe(record.stamp); }

size_t StampLocalChanges(State& base, const Values& local, Clock& clock, int64_t now_ms) {
  clock.Observe(base);
  size_t changed = 0;
  for (const auto& [key, value] : local) {
    const auto found = base.find(key);
    if (found != base.end() && !found->second.deleted && found->second.value == value) continue;
    Record& record = base[key];
    Wipe(record.value);
    record = Record{clock.Next(now_ms), false, value};
    ++changed;
  }
  for (auto& [key, record] : base) {
    if (record.deleted || local.contains(key)) continue;
    Wipe(record.value);
    record = Record{clock.Next(now_ms), true, {}};
    ++changed;
  }
  return changed;
}

size_t Merge(State& into, const State& other) {
  size_t taken = 0;
  for (const auto& [key, record] : other) {
    const auto found = into.find(key);
    if (found != into.end() && !(found->second.stamp < record.stamp)) continue;
    if (found != into.end()) Wipe(found->second.value);
    into[key] = record;
    ++taken;
  }
  return taken;
}

Changes Diff(const State& merged, const Values& local) {
  Changes changes;
  for (const auto& [key, record] : merged) {
    const auto found = local.find(key);
    if (record.deleted) { if (found != local.end()) changes.remove.push_back(key); }
    else if (found == local.end() || found->second != record.value) changes.upsert.emplace(key, record.value);
  }
  return changes;
}

Values Live(const State& state) {
  Values values;
  for (const auto& [key, record] : state) if (!record.deleted) values.emplace(key, record.value);
  return values;
}

void AppendJsonString(std::string& out, std::string_view text) {
  static constexpr char kHex[] = "0123456789abcdef";
  out.push_back('"');
  for (const unsigned char c : text) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (c < 0x20 || c == 0x7f) { out += "\\u00"; out.push_back(kHex[c >> 4]); out.push_back(kHex[c & 15]); }
        else out.push_back(static_cast<char>(c));
    }
  }
  out.push_back('"');
}

std::string Serialize(const State& state) {
  std::string out = "{\"records\":[";
  bool first = true;
  for (const auto& [key, record] : state) {
    if (!first) out.push_back(',');
    first = false;
    out += "{\"k\":"; AppendJsonString(out, key);
    out += ",\"t\":" + std::to_string(record.stamp.ms) + ",\"c\":" + std::to_string(record.stamp.counter) + ",\"d\":";
    AppendJsonString(out, record.stamp.device);
    if (record.deleted) out += ",\"x\":true";
    else { out += ",\"v\":"; AppendJsonString(out, record.value); }
    out.push_back('}');
  }
  out += "]}";
  return out;
}

bool Parse(std::string_view json, State& out) {
  out.clear();
  auto root = importer::ParseJson(json);
  const auto* records = root ? root->Get("records") : nullptr;
  if (!records || !records->IsArray() || records->items.size() > kMaxRecords) return false;
  State state;
  bool ok = true;
  for (const auto& item : records->items) {
    const auto* key = item.Get("k");
    const auto* value = item.Get("v");
    const auto* deleted = item.Get("x");
    const auto counter = item.Int("c", -1);
    Record record{Stamp{item.Int("t", 0), static_cast<uint32_t>(counter), item.Str("d")}, deleted && deleted->type == importer::Json::Type::Bool && deleted->boolean, {}};
    if (!item.IsObject() || !key || key->type != importer::Json::Type::String || !ValidKey(key->text) || counter < 0 || counter > UINT32_MAX ||
        !ValidStamp(record.stamp) || state.contains(key->text)) { ok = false; break; }
    if (record.deleted == (value != nullptr)) { ok = false; break; }  // Exactly one of a value or a tombstone.
    if (value) {
      if (value->type != importer::Json::Type::String || value->text.size() > kMaxValue) { ok = false; break; }
      record.value = value->text;
    }
    state.emplace(key->text, std::move(record));
  }
  // The parse tree holds copies of every value (passwords included).
  if (root) {
    for (auto& item : root->fields["records"].items) if (auto found = item.fields.find("v"); found != item.fields.end()) Wipe(found->second.text);
  }
  if (!ok) { WipeState(state); return false; }
  out = std::move(state);
  return true;
}

void WipeState(State& state) { for (auto& [key, record] : state) Wipe(record.value); state.clear(); }
void WipeValues(Values& values) { for (auto& [key, value] : values) Wipe(value); values.clear(); }
}  // namespace red::sync
