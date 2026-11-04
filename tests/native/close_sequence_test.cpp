#include "windows/close_sequence.h"
#include <cstdio>
int main() {
  int failures = 0;
  auto check = [&](bool value, const char* name) { if (!value) { std::fprintf(stderr, "%s\n", name); ++failures; } };
  red::CloseSequence gate;
  check(gate.Begin({2, 3}, 1), "first root close starts preparation");
  check(gate.Next() == -1 && !gate.Begin({4}, 1), "reentrant close cannot start any page before preparation");
  gate.Prepared();
  check(gate.Next() == 2 && gate.Next() == -1, "no prompt; only one child starts at a time");
  check(gate.DiscardApproved(2, false) && !gate.DiscardApproved(3, false) && !gate.DiscardApproved(2, true),
        "window close accepts only the current closing child's unload, never a reload");
  gate.Closed(2); check(gate.Next() == 3, "second waits for actual first OnBeforeClose");
  gate.Closed(3); check(gate.Next() == 1 && gate.phase() == red::CloseSequence::Phase::Shell, "shell closes last");
  check(!gate.DiscardApproved(1, false), "never grants privileged shell capabilities");
  gate.Cancel(); gate.Begin({7}, 1); gate.Prepared();
  check(gate.Next() == 7 && gate.DiscardApproved(7, false), "a single page closes without asking too");
  gate.Cancel(); check(gate.phase() == red::CloseSequence::Phase::Idle && !gate.DiscardApproved(7, false), "cancel resets coordinator");
  gate.Begin({}, 1); gate.Prepared(); check(gate.Next() == 1, "empty workspace closes shell directly");
  return failures ? 1 : 0;
}
