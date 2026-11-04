#pragma once
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>
#include <cstdint>
#include <filesystem>
namespace red {
class AppSession;
struct TreeResult;
class AutoArchive : public std::enable_shared_from_this<AutoArchive> {
 public:
  static std::shared_ptr<AutoArchive> Start(const std::shared_ptr<AppSession>& session);
  void Activity(const std::string& id);
  bool Reserved(const std::string& id) const;
  void Stop() { stopped_=true; }
 private:
  explicit AutoArchive(std::weak_ptr<AppSession> session) : session_(std::move(session)) {}
  void Schedule(int milliseconds);
  void Tick();
  void Next();
  bool Safe() const;
  int64_t Now() const;
  void CloseNext();
  void Finish(bool success);
  // Memory unloading of idle pages (independent setting; no archive).
  bool UnloadNext();
  void Unloaded(const std::string& id, bool closed);
  std::weak_ptr<AppSession> session_;
  std::filesystem::path test_clock_file_;  // Explicit disposable-fixture flag only.
  int64_t unload_test_ms_=0;  // --probe-unload: idle limit in ms, fast ticks.
  std::set<std::string> pending_, blocked_;
  std::map<std::string,int64_t> recent_;
  std::vector<std::string> ids_;
  size_t close_index_=0;
  int budget_=0, archived_=0, unload_budget_=0;
  int64_t cutoff_=0;
  bool stopped_=false;
};
}  // namespace red
