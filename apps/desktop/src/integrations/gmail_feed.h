#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace red {
// Gmail's Atom inbox feed (https://mail.google.com/mail/u/<n>/feed/atom): the latest unread inbox
// messages as a few KB of XML, read with the profile's own cookies. It replaces loading the whole
// Gmail app in a hidden browser for the card (measured on a signed-in personal account,
// 2026-11-09: works; ~300 MB less per check). https://developers.google.com/workspace/gmail/gmail_inbox_feed
struct GmailFeedEntry {
  std::string title, sender, message_id;  // message_id: Gmail's hex ID, opened as #all/<id>.
  int64_t issued_ms = 0;                   // Unix time in milliseconds (UTC).
};
struct GmailFeed {
  int64_t unread = 0;  // <fullcount>: every unread inbox message, not only the listed ones.
  std::vector<GmailFeedEntry> entries;  // Newest first, at most 20.
};

namespace gmail_feed_detail {
inline std::string_view Between(std::string_view text, std::string_view open, std::string_view close, size_t& at) {
  const auto start = text.find(open, at);
  if (start == text.npos) { at = text.npos; return {}; }
  const auto begin = start + open.size();
  const auto end = text.find(close, begin);
  if (end == text.npos) { at = text.npos; return {}; }
  at = end + close.size();
  return text.substr(begin, end - begin);
}
inline std::string_view Tag(std::string_view text, std::string_view name) {
  size_t at = 0;
  const auto value = Between(text, "<" + std::string(name) + ">", "</" + std::string(name) + ">", at);
  return at == text.npos ? std::string_view{} : value;
}
inline void AppendUtf8(std::string& out, uint32_t code) {
  if (code == 0 || code > 0x10FFFF || (code >= 0xD800 && code <= 0xDFFF)) return;
  if (code < 0x80) out += static_cast<char>(code);
  else if (code < 0x800) { out += static_cast<char>(0xC0 | (code >> 6)); out += static_cast<char>(0x80 | (code & 0x3F)); }
  else if (code < 0x10000) { out += static_cast<char>(0xE0 | (code >> 12)); out += static_cast<char>(0x80 | ((code >> 6) & 0x3F)); out += static_cast<char>(0x80 | (code & 0x3F)); }
  else { out += static_cast<char>(0xF0 | (code >> 18)); out += static_cast<char>(0x80 | ((code >> 12) & 0x3F));
         out += static_cast<char>(0x80 | ((code >> 6) & 0x3F)); out += static_cast<char>(0x80 | (code & 0x3F)); }
}
// XML text: entities decoded, whitespace collapsed, control characters dropped.
inline std::string Text(std::string_view raw) {
  std::string out;
  for (size_t i = 0; i < raw.size(); ++i) {
    char c = raw[i];
    if (c == '&') {
      const auto end = raw.find(';', i);
      if (end != raw.npos && end - i <= 10) {
        const auto name = raw.substr(i + 1, end - i - 1);
        std::optional<uint32_t> code;
        if (name == "amp") code = '&'; else if (name == "lt") code = '<'; else if (name == "gt") code = '>';
        else if (name == "quot") code = '"'; else if (name == "apos") code = '\'';
        else if (name.size() > 1 && name[0] == '#') {
          uint32_t value = 0; bool ok = true;
          const bool hex = name[1] == 'x' || name[1] == 'X';
          for (const char d : name.substr(hex ? 2 : 1)) {
            const int digit = d >= '0' && d <= '9' ? d - '0' : hex && d >= 'a' && d <= 'f' ? d - 'a' + 10 : hex && d >= 'A' && d <= 'F' ? d - 'A' + 10 : -1;
            if (digit < 0 || value > 0x10FFFF) { ok = false; break; }
            value = value * (hex ? 16 : 10) + static_cast<uint32_t>(digit);
          }
          if (ok) code = value;
        }
        if (code) { AppendUtf8(out, *code); i = end; continue; }
      }
    }
    if (static_cast<unsigned char>(c) < 0x20) c = ' ';
    if (c == ' ' && (out.empty() || out.back() == ' ')) continue;
    out += c;
  }
  while (!out.empty() && out.back() == ' ') out.pop_back();
  return out;
}
// "2026-11-09T21:47:38Z" -> Unix milliseconds; nullopt for any other form.
inline std::optional<int64_t> IsoUtc(std::string_view s) {
  if (s.size() != 20 || s[4] != '-' || s[7] != '-' || s[10] != 'T' || s[13] != ':' || s[16] != ':' || s[19] != 'Z') return std::nullopt;
  const auto num = [&](size_t at, size_t n) -> int {
    int v = 0;
    for (size_t i = at; i < at + n; ++i) { if (s[i] < '0' || s[i] > '9') return -1; v = v * 10 + (s[i] - '0'); }
    return v;
  };
  const int y = num(0, 4), m = num(5, 2), d = num(8, 2), hh = num(11, 2), mm = num(14, 2), ss = num(17, 2);
  if (y < 1970 || m < 1 || m > 12 || d < 1 || d > 31 || hh < 0 || hh > 23 || mm < 0 || mm > 59 || ss < 0 || ss > 60) return std::nullopt;
  // Days from civil (Howard Hinnant).
  const int yy = y - (m <= 2);
  const int era = yy / 400, yoe = yy - era * 400;
  const int doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  const int64_t days = static_cast<int64_t>(era) * 146097 + doe - 719468;
  return ((days * 24 + hh) * 60 + mm) * 60000LL + ss * 1000LL;
}
inline bool HexId(std::string_view id) {
  if (id.size() < 8 || id.size() > 64) return false;
  for (const char c : id) if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
  return true;
}
}  // namespace gmail_feed_detail

// Nullopt when |xml| is not a Gmail inbox feed (for example a sign-in page).
inline std::optional<GmailFeed> ParseGmailFeed(std::string_view xml) {
  using namespace gmail_feed_detail;
  if (xml.size() > (1 << 20) || xml.find("<feed") == xml.npos || xml.find("<fullcount>") == xml.npos) return std::nullopt;
  GmailFeed feed;
  const auto count = Tag(xml, "fullcount");
  if (count.empty() || count.size() > 9) return std::nullopt;
  for (const char c : count) { if (c < '0' || c > '9') return std::nullopt; feed.unread = feed.unread * 10 + (c - '0'); }
  size_t at = 0;
  while (feed.entries.size() < 20) {
    const auto entry = Between(xml, "<entry>", "</entry>", at);
    if (at == xml.npos) break;
    GmailFeedEntry item;
    item.title = Text(Tag(entry, "title"));
    item.sender = Text(Tag(Tag(entry, "author"), "name"));
    const auto issued = IsoUtc(Tag(entry, "issued"));
    // The message ID is the link's message_id parameter (hex).
    const auto marker = entry.find("message_id=");
    std::string_view id;
    if (marker != entry.npos) {
      const auto start = marker + 11;
      const auto end = entry.find_first_of("&\"", start);
      id = entry.substr(start, end == entry.npos ? entry.npos : end - start);
    }
    if (!issued || !HexId(id)) continue;
    item.issued_ms = *issued;
    item.message_id = std::string(id);
    feed.entries.push_back(std::move(item));
  }
  return feed;
}
}  // namespace red
