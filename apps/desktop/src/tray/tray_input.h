#pragma once
// What sol-tray.exe's command bar does with the text it was given: the same rules as Sol's own
// bar (apps/browser-ui/src/navigation-input.ts, resolveNavigationInput), for web addresses and
// searches only. Local files are not opened from here: such text becomes a search.
#include <windows.h>
#include <algorithm>
#include <cctype>
#include <cwctype>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sol_tray {
struct Destination {
  std::string url;       // UTF-8, for sol.exe --little-sol-url.
  bool search = false;
  std::wstring title;    // The result row: what was typed...
  std::wstring detail;   // ...and where it goes ("wikipedia.org", "Search Google").
};

inline std::string Utf8(std::wstring_view text) {
  if (text.empty()) return {};
  const int bytes = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
  std::string out(bytes > 0 ? bytes : 0, '\0');
  if (bytes > 0) WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), bytes, nullptr, nullptr);
  return out;
}

inline std::wstring Lower(std::wstring_view text) {
  std::wstring out(text);
  for (auto& c : out) c = static_cast<wchar_t>(std::towlower(c));
  return out;
}

inline bool IsDigits(std::wstring_view text) {
  if (text.empty()) return false;
  for (const wchar_t c : text) if (c < L'0' || c > L'9') return false;
  return true;
}

// localhost, [::1] and private IPv4 in canonical decimal (as navigation-input.ts isLocalAuthority).
inline bool IsLocalAuthority(std::wstring_view authority) {
  std::wstring host(authority);
  if (const auto colon = host.rfind(L':'); colon != std::wstring::npos && host.find(L']') == std::wstring::npos && IsDigits(host.substr(colon + 1)))
    host.resize(colon);
  else if (const auto bracket = host.find(L"]:"); bracket != std::wstring::npos && IsDigits(host.substr(bracket + 2)))
    host.resize(bracket + 1);
  const auto lower = Lower(host);
  if (lower == L"localhost" || lower == L"[::1]") return true;
  int octets[4]{}, count = 0;
  size_t start = 0;
  for (;;) {
    const auto dot = host.find(L'.', start);
    const auto part = host.substr(start, dot == std::wstring::npos ? std::wstring::npos : dot - start);
    if (count == 4 || !IsDigits(part) || part.size() > 3 || (part.size() > 1 && part[0] == L'0')) return false;
    octets[count] = std::stoi(part);
    if (octets[count++] > 255) return false;
    if (dot == std::wstring::npos) break;
    start = dot + 1;
  }
  if (count != 4) return false;
  const int first = octets[0], second = octets[1];
  return first == 127 || first == 10 || (first == 172 && second >= 16 && second <= 31) || (first == 192 && second == 168);
}

// encodeURIComponent: UTF-8, everything but A-Z a-z 0-9 - _ . ! ~ * ' ( ) escaped.
inline std::string EncodeComponent(std::wstring_view text) {
  static constexpr char kHex[] = "0123456789ABCDEF";
  std::string out;
  for (const unsigned char c : Utf8(text)) {
    if (std::isalnum(c) || std::string_view("-_.!~*'()").find(static_cast<char>(c)) != std::string_view::npos) out += static_cast<char>(c);
    else { out += '%'; out += kHex[c >> 4]; out += kHex[c & 15]; }
  }
  return out;
}

inline std::optional<Destination> ResolveInput(std::wstring_view raw, std::wstring_view engine) {
  if (raw.size() > 4096) return std::nullopt;
  for (const wchar_t c : raw) if (c < 0x20 || c == 0x7f) return std::nullopt;
  std::wstring text(raw);
  const auto first = text.find_first_not_of(L"  　");
  if (first == std::wstring::npos) return std::nullopt;
  text = text.substr(first, text.find_last_not_of(L"  　") - first + 1);
  if (text.size() >= 2 && text.front() == L'"' && text.back() == L'"') text = text.substr(1, text.size() - 2);
  if (text.empty()) return std::nullopt;

  const auto authority = text.substr(0, text.find_first_of(L"/?#"));
  const bool local = IsLocalAuthority(authority);
  // A scheme ("https:"), but not "host:port".
  size_t scheme = 0;
  while (scheme < text.size() && (std::iswalnum(text[scheme]) || text[scheme] == L'+' || text[scheme] == L'.' || text[scheme] == L'-')) ++scheme;
  const bool scheme_like = scheme > 0 && scheme < text.size() && text[scheme] == L':' && std::iswalpha(text[0]) && text[0] < 0x80;
  bool host_port = false;
  if (const auto colon = authority.find(L':'); colon != std::wstring::npos && colon > 0 && authority.find_first_of(L" \t") == std::wstring::npos)
    host_port = IsDigits(authority.substr(colon + 1));
  const bool explicit_scheme = scheme_like && !local && !host_port;
  const auto lower = Lower(text);
  const bool web = lower.starts_with(L"http://") || lower.starts_with(L"https://");
  const bool spaces = text.find_first_of(L" \t 　") != std::wstring::npos;
  // name@example.com: an e-mail address is searched for, not opened.
  const auto at = text.find(L'@');
  const bool email = at != std::wstring::npos && at > 0 && text.find_first_of(L" :/\\", 0) == std::wstring::npos &&
      text.find(L'@', at + 1) == std::wstring::npos && at + 1 < text.size() && std::iswalnum(text[at + 1]);
  const bool bracketed = authority.size() > 2 && authority.front() == L'[' && authority.find(L']') != std::wstring::npos;
  const bool address = (explicit_scheme && web) ||
      (!explicit_scheme && !email && !spaces && (local || authority.find(L'.') != std::wstring::npos || bracketed));
  if (address) {
    std::wstring url = explicit_scheme ? text : std::wstring(local ? L"http://" : L"https://") + text;
    const auto host_start = url.find(L"://") + 3;
    const auto host_end = url.find_first_of(L"/?#", host_start);
    const auto host = url.substr(host_start, host_end == std::wstring::npos ? std::wstring::npos : host_end - host_start);
    // No credentials, no backslashes, a real host (as navigation-input.ts).
    if (host.empty() || host.find(L'@') != std::wstring::npos || url.find(L'\\') != std::wstring::npos || host.find(L"..") != std::wstring::npos ||
        host.front() == L'.' || host.front() == L':')
      return std::nullopt;
    std::string utf8;
    for (const char c : Utf8(url)) { if (c == '"') utf8 += "%22"; else if (c == ' ') utf8 += "%20"; else utf8 += c; }
    std::wstring shown = host;
    if (const auto colon = shown.rfind(L':'); colon != std::wstring::npos && shown.find(L']') == std::wstring::npos) shown.resize(colon);
    return Destination{utf8, false, text, Lower(shown)};
  }
  if (explicit_scheme) return std::nullopt;  // mailto:, javascript:... are not for Little Sol.
  struct Engine { std::wstring_view id, title; std::string_view url; };
  static constexpr Engine kEngines[] = {
    {L"google", L"Google", "https://www.google.com/search?q="},
    {L"duckduckgo", L"DuckDuckGo", "https://duckduckgo.com/?q="},
    {L"bing", L"Bing", "https://www.bing.com/search?q="},
    {L"brave", L"Brave Search", "https://search.brave.com/search?q="},
  };
  const Engine* chosen = &kEngines[0];
  for (const auto& candidate : kEngines) if (candidate.id == engine) chosen = &candidate;
  return Destination{std::string(chosen->url) + EncodeComponent(text), true, text, L"Search " + std::wstring(chosen->title)};
}
// Google's completions, as Sol's bar asks for them (navigation/search_suggestions.cpp,
// CommandBar.tsx): a search of 2 to 200 characters that does not look like an address.
inline bool ValidSuggestionQuery(std::wstring_view text) {
  if (text.size() < 2 || text.size() > 200 || text.find_first_not_of(L' ') == std::wstring_view::npos) return false;
  if (text.find_first_of(L".:/\\@=#") != std::wstring_view::npos) return false;
  for (const wchar_t c : text) if (c < 0x20 || c == 0x7f) return false;
  return true;
}

inline std::wstring Wide(std::string_view text) {
  if (text.empty()) return {};
  const int chars = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
  std::wstring out(chars > 0 ? chars : 0, L'\0');
  if (chars > 0) MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), out.data(), chars);
  return out;
}

// The reply is ["query", ["one", "two", ...], ...] (UTF-8 JSON). Up to |limit| distinct entries,
// each without control characters and at most 400 characters; anything else in the reply is ignored.
inline std::vector<std::wstring> ParseSuggestions(std::string_view body, size_t limit = 5) {
  std::vector<std::wstring> out;
  if (body.size() > 32768) return out;
  size_t at = 0;
  const auto skip = [&] { while (at < body.size() && (body[at] == ' ' || body[at] == '\n' || body[at] == '\r' || body[at] == '\t')) ++at; };
  // A JSON string at |at|; nullopt when malformed.
  const auto string = [&]() -> std::optional<std::wstring> {
    if (at >= body.size() || body[at] != '"') return std::nullopt;
    std::string bytes;
    std::wstring text;
    for (++at; at < body.size(); ++at) {
      const char c = body[at];
      if (c == '"') { ++at; return Wide(bytes); }
      if (static_cast<unsigned char>(c) < 0x20) return std::nullopt;
      if (c != '\\') { bytes += c; continue; }
      if (++at >= body.size()) return std::nullopt;
      const char e = body[at];
      if (e == 'u') {
        if (at + 4 >= body.size()) return std::nullopt;
        unsigned value = 0;
        for (int i = 1; i <= 4; ++i) {
          const char h = body[at + i];
          value = value * 16 + (h >= '0' && h <= '9' ? h - '0' : h >= 'a' && h <= 'f' ? h - 'a' + 10 : h >= 'A' && h <= 'F' ? h - 'A' + 10 : 99);
          if (value > 0xffff) return std::nullopt;
        }
        at += 4;
        // Re-encode as UTF-8 so the whole string decodes once (surrogate pairs included).
        bytes += Utf8(std::wstring(1, static_cast<wchar_t>(value)));
        if (value >= 0xd800 && value <= 0xdbff && at + 6 < body.size() && body[at + 1] == '\\' && body[at + 2] == 'u') {
          unsigned low = 0;
          for (int i = 3; i <= 6; ++i) {
            const char h = body[at + i];
            low = low * 16 + (h >= '0' && h <= '9' ? h - '0' : h >= 'a' && h <= 'f' ? h - 'a' + 10 : h >= 'A' && h <= 'F' ? h - 'A' + 10 : 99);
          }
          if (low >= 0xdc00 && low <= 0xdfff) {
            bytes.erase(bytes.size() - 3);  // The lone high surrogate's replacement.
            const wchar_t pair[2] = {static_cast<wchar_t>(value), static_cast<wchar_t>(low)};
            bytes += Utf8(std::wstring_view(pair, 2));
            at += 6;
          }
        }
        continue;
      }
      static constexpr std::string_view kFrom = "\"\\/bfnrt", kTo = "\"\\/\b\f\n\r\t";
      const auto index = kFrom.find(e);
      if (index == std::string_view::npos) return std::nullopt;
      bytes += kTo[index];
    }
    return std::nullopt;
  };
  skip();
  if (at >= body.size() || body[at++] != '[') return out;
  skip();
  if (!string()) return out;
  skip();
  if (at >= body.size() || body[at++] != ',') return out;
  skip();
  if (at >= body.size() || body[at++] != '[') return out;
  for (int seen = 0; seen < 20 && out.size() < limit; ++seen) {
    skip();
    if (at < body.size() && body[at] == ']') break;
    const auto entry = string();
    if (!entry) break;
    const bool clean = !entry->empty() && entry->size() <= 400 &&
        std::none_of(entry->begin(), entry->end(), [](wchar_t c) { return c < 0x20 || c == 0x7f; });
    if (clean && std::find(out.begin(), out.end(), *entry) == out.end()) out.push_back(*entry);
    skip();
    if (at < body.size() && body[at] == ',') ++at; else break;
  }
  return out;
}
}  // namespace sol_tray
