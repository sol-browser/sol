#include "workspace/recent_cycle.h"
#include <cstdio>

// Ctrl+Tab by recent use: the order is fixed by the first press, then each press goes one step deeper.
namespace {
int failures = 0;
void Expect(const std::string& got, const std::string& want, const char* what) {
  if (got != want) { std::printf("FAIL %s: got '%s', want '%s'\n", what, got.c_str(), want.c_str()); ++failures; }
}
}  // namespace

int main() {
  using Tabs = std::vector<std::string>;
  {
    red::RecentCycle cycle;
    const Tabs recent{"a", "b", "c"}, tabs{"a", "b", "c", "d"};
    Expect(red::RecentCycleStep(cycle, recent, tabs, "c", false), "b", "first press goes to the tab used before this one");
    Expect(red::RecentCycleStep(cycle, recent, tabs, "b", false), "a", "second press goes one step deeper");
    Expect(red::RecentCycleStep(cycle, recent, tabs, "a", false), "c", "the cycle wraps to where it began: tab d was never used and is not in it");
    Expect(red::RecentCycleStep(cycle, recent, tabs, "c", true), "a", "Shift goes back");
  }
  {
    red::RecentCycle cycle;
    Expect(red::RecentCycleStep(cycle, {"a", "b", "c"}, {"a", "b", "c", "d"}, "c", true), "a", "Shift from the start reaches the least recent tab of the cycle");
  }
  {
    // Arc's Tab Switcher: the five most recently used tabs, however many tabs there are.
    red::RecentCycle cycle;
    const Tabs recent{"a", "b", "c", "d", "e", "f", "g", "h"}, tabs{"a", "b", "c", "d", "e", "f", "g", "h", "i"};
    Expect(red::RecentCycleStep(cycle, recent, tabs, "h", false), "g", "1st press");
    Expect(red::RecentCycleStep(cycle, recent, tabs, "g", false), "f", "2nd press");
    Expect(red::RecentCycleStep(cycle, recent, tabs, "f", false), "e", "3rd press");
    Expect(red::RecentCycleStep(cycle, recent, tabs, "e", false), "d", "4th press: the fifth most recent tab");
    Expect(red::RecentCycleStep(cycle, recent, tabs, "d", false), "h", "5th press: back to the start, tabs older than five are not reached");
  }
  {
    red::RecentCycle cycle;  // Tabs of another space and closed tabs in the history are skipped.
    Expect(red::RecentCycleStep(cycle, {"x", "a", "y", "b"}, {"a", "b"}, "b", false), "a", "other spaces are not visited");
    Expect(red::RecentCycleStep(cycle, {"x", "a", "y", "b"}, {"a", "b"}, "a", false), "b", "two tabs toggle");
  }
  {
    red::RecentCycle cycle;
    Expect(red::RecentCycleStep(cycle, {"a"}, {"a"}, "a", false), "", "one tab: nowhere to go");
    red::RecentCycle empty;
    Expect(red::RecentCycleStep(empty, {}, {}, "", false), "", "no tabs: nowhere to go");
  }
  {
    red::RecentCycle cycle;  // The active tab is not in the space's list (a link preview): the list alone decides.
    Expect(red::RecentCycleStep(cycle, {"a", "b"}, {"a", "b"}, "p", false), "b", "an unlisted current tab starts from the most recent");
  }
  {
    // After a restart nothing was used in this run: the saved times decide, newest first, never the sidebar order.
    const auto history = red::RecentHistory({}, {{"a", 100}, {"b", 400}, {"c", 300}, {"d", 0}, {"e", 200}});
    red::RecentCycle cycle;
    Expect(history.size() == 4 ? history[0] + history[1] + history[2] + history[3] : "?", "aecb", "saved times order the history, never-used tabs are left out");
    Expect(red::RecentCycleStep(cycle, history, {"a", "b", "c", "d", "e"}, "b", false), "c", "the first press goes to the tab saved as used before");
    Expect(red::RecentCycleStep(cycle, history, {"a", "b", "c", "d", "e"}, "c", false), "e", "then one deeper by saved time");
    // This run's tabs are newer than anything saved, even when their saved time is later (passing through bumps it).
    const auto mixed = red::RecentHistory({"x", "y"}, {{"y", 999}, {"z", 50}, {"x", 10}});
    Expect(mixed.size() == 3 ? mixed[0] + mixed[1] + mixed[2] : "?", "zxy", "tabs used in this run come last (most recent)");
  }
  {
    // A tab closed after its last use (a Favorite closed with Ctrl+W) is left out, also after a restart, until it is used again.
    const auto history = red::RecentHistory({}, {{"a", 100, 0}, {"fav", 400, 450}, {"c", 300, 0}, {"used", 500, 200}});
    Expect(history.size() == 3 ? history[0] + history[1] + history[2] : "?", "acused", "closed since its last use: not in the history");
    red::RecentCycle cycle;
    Expect(red::RecentCycleStep(cycle, history, {"a", "fav", "c", "used"}, "used", false), "c", "the first press skips the closed Favorite");
  }
  if (failures) return 1;
  std::puts("recent_cycle ok");
  return 0;
}
