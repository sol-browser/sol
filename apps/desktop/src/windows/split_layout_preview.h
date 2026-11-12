#pragma once
#include <map>
#include <memory>
#include <string>
#include <vector>
#include "app/state.h"
#include "windows/split_overlay.h"

namespace red {
struct SplitPageLabel { std::wstring title; std::string icon; };
// Every tab's title and icon PNG, for the preview's cards.
std::map<std::string, SplitPageLabel> SplitPageLabels(const std::shared_ptr<State>& state);

// A page card of a previewed layout: where it is shown now and where the drop
// puts it (screen pixels); |accent| marks the tab being dropped or moved.
struct SplitPreviewCard { std::string id; CefRect from, to; bool accent = false; };

// The layout a drop or a pane move would produce, opaque over the page area:
// every page as a card with its icon and title, the moving one in the accent.
// When |key| (the target) changes, cards glide from where they are shown to
// their new place. Only this picture moves; the website windows below keep
// their size until the drop.
class SplitLayoutPreview {
 public:
  void Hide();
  void Update(const std::shared_ptr<State>& state, const std::string& key, const CefRect& area,
              const std::vector<SplitPreviewCard>& cards, const std::map<std::string, SplitPageLabel>& labels, float scale);
 private:
  SplitOverlay overlay_;
  std::string key_, painted_;
  bool peek_shown_ = false;
  std::weak_ptr<State> state_;  // For probe evidence of each show/hide (flicker).
  unsigned long long started_ = 0;
  std::map<std::string, CefRect> from_, shown_;
};
}  // namespace red
