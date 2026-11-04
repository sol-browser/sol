#pragma once
#include <algorithm>
#include <vector>
namespace red {
// A Chrome child cannot be held in a partially closed state, so a window close
// never asks (user decision 2026-10-22, like Arc): every page's beforeunload is
// accepted for that close only, pages close one at a time, then the shell.
// Saved tabs and folders persist; only live page state (form text) is lost.
class CloseSequence {
 public:
  enum class Phase { Idle, Preparing, Pages, Shell };
  bool Begin(std::vector<int> pages, int shell) {
    if (phase_ != Phase::Idle) return false;
    pages_ = std::move(pages); shell_ = shell; phase_ = Phase::Preparing; return true;
  }
  void Prepared() { if (phase_ == Phase::Preparing) { discard_ = true; phase_ = Phase::Pages; } }
  void Cancel() { phase_ = Phase::Idle; pages_.clear(); waiting_ = -1; discard_ = false; }
  int Next() {
    if (waiting_ >= 0 || phase_ != Phase::Pages) return -1;
    if (pages_.empty()) { phase_ = Phase::Shell; waiting_ = shell_; }
    else waiting_ = pages_.front();
    return waiting_;
  }
  void Closed(int id) { std::erase(pages_, id); if (waiting_ == id) waiting_ = -1; }
  bool DiscardApproved(int id, bool reload) const { return !reload && discard_ && phase_ == Phase::Pages && waiting_ == id; }
  Phase phase() const { return phase_; }
  int waiting() const { return waiting_; }
  size_t count() const { return pages_.size(); }
 private:
  Phase phase_ = Phase::Idle;
  std::vector<int> pages_;
  int shell_ = -1, waiting_ = -1;
  bool discard_ = false;
};
}  // namespace red
