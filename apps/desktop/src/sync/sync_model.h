#pragma once
#include <compare>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

// State-based last-writer-wins map for cloud-folder sync (docs/sync.md,
// "Records and merging"). CEF-free and deterministic: every PC that merges the
// same device files reaches the same state.
namespace red::sync {
// Hybrid logical clock stamp. Ordered by (ms, counter, device).
struct Stamp {
  int64_t ms = 0;
  uint32_t counter = 0;
  std::string device;
  auto operator<=>(const Stamp&) const = default;
  bool operator==(const Stamp&) const = default;
};
struct Record {
  Stamp stamp;
  bool deleted = false;
  std::string value;  // Canonical JSON of the record; empty for a tombstone.
  bool operator==(const Record&) const = default;
};
using State = std::map<std::string, Record>;
// Current local values, key -> canonical value.
using Values = std::map<std::string, std::string>;

bool ValidDeviceId(std::string_view id);  // 32 lowercase hex characters.
bool ValidKey(std::string_view key);

class Clock {
 public:
  explicit Clock(std::string device) : device_(std::move(device)) {}
  // A stamp greater than every stamp this clock issued or observed.
  Stamp Next(int64_t now_ms);
  void Observe(const Stamp& stamp);
  void Observe(const State& state);
 private:
  std::string device_;
  int64_t ms_ = 0;
  uint32_t counter_ = 0;
};

// Gives every local change since |base| a new stamp: new or edited keys get
// the local value, keys that disappeared locally become tombstones. Returns
// the number of changed records.
size_t StampLocalChanges(State& base, const Values& local, Clock& clock, int64_t now_ms);
// Per key, the higher stamp wins. Returns the number of records taken from |other|.
size_t Merge(State& into, const State& other);
struct Changes {
  Changes() = default;
  Changes(const Changes&) = default;
  Changes(Changes&&) noexcept = default;
  Changes& operator=(const Changes&) = default;
  Changes& operator=(Changes&&) noexcept = default;
  ~Changes() = default;
  Values upsert;                 // Keys whose winning value differs from the local one.
  std::vector<std::string> remove;  // Local keys whose winning record is a tombstone.
  bool empty() const { return upsert.empty() && remove.empty(); }
};
Changes Diff(const State& merged, const Values& local);
// Values a state represents (tombstones omitted).
Values Live(const State& state);

// Compact JSON for the encrypted payload. Parse rejects malformed or
// oversized input and leaves |out| empty.
std::string Serialize(const State& state);
bool Parse(std::string_view json, State& out);
// Appends a JSON string literal.
void AppendJsonString(std::string& out, std::string_view text);
// Wipes record values (passwords) before releasing a state.
void WipeState(State& state);
void WipeValues(Values& values);
}  // namespace red::sync
