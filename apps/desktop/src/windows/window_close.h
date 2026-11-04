#pragma once
#include "app/state.h"
#include "windows/close_sequence.h"
namespace red {
class WindowClose : public std::enable_shared_from_this<WindowClose> {
 public:
  explicit WindowClose(std::weak_ptr<State> state) : state_(std::move(state)) {}
  bool CanClose();
  void OnClosed(int browser);
  void Cancel(int browser);
  bool DiscardApproved(int browser, bool reload) const { return sequence_.DiscardApproved(browser, reload); }
 private:
  void Begin();
  void Prepared(bool success);
  void Advance();
  void QueueAdvance();
  std::weak_ptr<State> state_;
  CloseSequence sequence_;
  bool queued_ = false;
};
}  // namespace red
