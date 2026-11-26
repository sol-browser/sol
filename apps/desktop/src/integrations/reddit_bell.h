#pragma once
#include <optional>
#include <string>
#include <string_view>

namespace red {
// Reddit's header badges, from the server-rendered header items the web app itself loads with the
// profile's cookies (captured on the user's account with one unread each, 2026-11-26):
//   Bell:  GET https://www.reddit.com/svc/shreddit/header-action-item-inbox?render-mode=partial
//     <faceplate-tracker ... data-faceplate-tracking-context="{&quot;inbox&quot;:{&quot;badgeCount&quot;:&quot;1&quot;}}">
//       <a ... href="/notifications" id="notifications-inbox-button">...</a> ...
//     <dynamic-badge initial-count="1" set-count-event="inbox_count_changed" appearance="ALERT"
//                    data-id="notification-count-element"></dynamic-badge>
//   Chats: GET https://www.reddit.com/svc/shreddit/header-action-item-chat
//     <reddit-chat-header-button initial-view="closed"> ... <button ... id="header-action-item-chat-button"> ...
//     <dynamic-badge id="header-action-item-chat-button-badge" initial-count="1" set-count-event="" appearance="ALERT">
// The badge's initial-count decides. nullopt when the button is missing (signed out, a login page,
// another shape); 0 when the button is there without a count.
namespace reddit_header_detail {
inline std::optional<int> Number(std::string_view text, size_t at) {
  size_t end = at;
  while (end < text.size() && end - at < 6 && text[end] >= '0' && text[end] <= '9') ++end;
  if (end == at) return std::nullopt;
  return std::stoi(std::string(text.substr(at, end - at)));
}
// initial-count="N" of the tag containing |marker|.
inline std::optional<int> BadgeCount(std::string_view html, std::string_view marker) {
  const auto at = html.find(marker);
  if (at == html.npos) return std::nullopt;
  const auto open = html.rfind('<', at), close = html.find('>', at);
  if (open == html.npos || close == html.npos) return std::nullopt;
  const auto tag = html.substr(open, close - open);
  constexpr std::string_view kCount = "initial-count=\"";
  const auto count = tag.find(kCount);
  return count == tag.npos ? std::nullopt : Number(tag, count + kCount.size());
}
}  // namespace reddit_header_detail

inline std::optional<int> RedditBellCount(std::string_view html) {
  using namespace reddit_header_detail;
  if (html.size() > 512 * 1024 || html.find("notifications-inbox-button") == html.npos) return std::nullopt;
  if (const auto n = BadgeCount(html, "\"notification-count-element\"")) return n;
  // The click tracker's context, HTML-escaped or not: "badgeCount":"1".
  for (const std::string_view key : {"badgeCount&quot;:&quot;", "badgeCount\":\"", "badgeCount&quot;:", "badgeCount\":"})
    if (const auto at = html.find(key); at != html.npos)
      if (const auto n = Number(html, at + key.size())) return n;
  return 0;
}

inline std::optional<int> RedditChatCount(std::string_view html) {
  using namespace reddit_header_detail;
  if (html.size() > 512 * 1024 || html.find("\"header-action-item-chat-button\"") == html.npos) return std::nullopt;
  return BadgeCount(html, "\"header-action-item-chat-button-badge\"").value_or(0);
}
}  // namespace red
