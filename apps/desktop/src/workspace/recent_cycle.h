#pragma once
#include <algorithm>
#include <string>
#include <vector>

namespace red {
// Ctrl+Tab, as in Arc: it jumps to the tab used last, and while Ctrl stays down each further press goes
// one step deeper into the recently used order. One cycle is fixed when its first press arrives; the
// tabs it visits do not reorder it, and only the tab where it ends counts as used.
struct RecentCycle {
  std::vector<std::string> order;  // The active tab first, then the others, most recently used first.
  size_t index = 0;
  bool active = false;
  bool from_outside = false;  // The tab shown when the cycle began is not one of its tabs.
};

// Which tab was used when, from the saved tree (last_active_at, milliseconds; 0 = never).
// |closed|: when it was last closed (0 = never); a tab closed after its last use is left out until used again.
struct UsedAt { std::string id; long long at = 0; long long closed = 0; };

// The history Ctrl+Tab walks, oldest first: tabs used before this run (by their saved time, so a restart
// keeps the order and a cold start does not walk the sidebar top to bottom), then the tabs used in this run.
inline std::vector<std::string> RecentHistory(const std::vector<std::string>& recent, std::vector<UsedAt> saved) {
  std::erase_if(saved, [&](const UsedAt& tab) { return tab.at <= 0 || tab.closed > tab.at || std::find(recent.begin(), recent.end(), tab.id) != recent.end(); });
  std::stable_sort(saved.begin(), saved.end(), [](const UsedAt& a, const UsedAt& b) { return a.at < b.at; });
  std::vector<std::string> history;
  for (const auto& tab : saved) history.push_back(tab.id);
  history.insert(history.end(), recent.begin(), recent.end());
  return history;
}

// Arc's Tab Switcher covers the five most recently used tabs, not every tab (its help center: "Switch
// between your 5 most recent tabs"): a cycle never walks the whole sidebar.
inline constexpr size_t kRecentCycleTabs = 5;

// |recent|: used tabs, oldest first (RecentHistory). |tabs|: the tabs a cycle may visit. The cycle is the
// current tab, then the most recently used ones, at most kRecentCycleTabs in all; tabs without a use
// time are not part of it. Returns the tab to show, or "" when there is nowhere to go.
inline std::string RecentCycleStep(RecentCycle& cycle, const std::vector<std::string>& recent, const std::vector<std::string>& tabs,
                                   const std::string& current, bool reverse) {
  if (!cycle.active) {
    cycle.order.clear(); cycle.index = 0;
    const auto allowed = [&](const std::string& id) { return std::find(tabs.begin(), tabs.end(), id) != tabs.end(); };
    const auto listed = [&](const std::string& id) { return std::find(cycle.order.begin(), cycle.order.end(), id) != cycle.order.end(); };
    cycle.from_outside = !allowed(current);
    if (!cycle.from_outside) cycle.order.push_back(current);
    for (auto i = recent.rbegin(); i != recent.rend() && cycle.order.size() < kRecentCycleTabs; ++i)
      if (allowed(*i) && !listed(*i)) cycle.order.push_back(*i);
    cycle.active = true;
    // A current tab outside the list (a link preview): the first press goes to the first or the last of the list.
    if (cycle.from_outside) {
      if (cycle.order.empty()) return {};
      cycle.index = reverse ? cycle.order.size() - 1 : 0;
      return cycle.order[cycle.index];
    }
  }
  const size_t count = cycle.order.size();
  if (count < 2) return {};
  cycle.index = (cycle.index + (reverse ? count - 1 : 1)) % count;
  return cycle.order[cycle.index];
}
}  // namespace red
