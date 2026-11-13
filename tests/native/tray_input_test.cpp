// sol-tray.exe's command bar resolves typed text like Sol's own bar (navigation-input.ts).
#include "tray/tray_input.h"
#include <cstdio>
#include <cstdlib>

namespace {
int failures = 0;
void check(bool ok, const char* what) {
  if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what); ++failures; }
}
std::string Url(const wchar_t* text, const wchar_t* engine = L"google") {
  const auto destination = sol_tray::ResolveInput(text, engine);
  return destination ? destination->url : std::string("<none>");
}
bool Search(const wchar_t* text) {
  const auto destination = sol_tray::ResolveInput(text, L"google");
  return destination && destination->search;
}
}  // namespace

int main() {
  check(Url(L"wikipedia.org") == "https://wikipedia.org", "a bare domain opens over https");
  check(Url(L"  example.com/a?b=1#c ") == "https://example.com/a?b=1#c", "trimmed, path, query and fragment kept");
  check(Url(L"http://example.com") == "http://example.com", "an explicit http address is kept");
  check(Url(L"HTTPS://Example.com") == "HTTPS://Example.com", "the scheme is matched without case");
  check(Url(L"localhost:3000/x") == "http://localhost:3000/x", "localhost with a port opens over http");
  check(Url(L"192.168.1.10") == "http://192.168.1.10", "a private IPv4 address opens over http");
  check(Url(L"8.8.8.8") == "https://8.8.8.8", "a public IPv4 address opens over https");
  check(Url(L"[::1]:8080") == "http://[::1]:8080", "IPv6 loopback with a port opens over http");
  check(Url(L"\"example.com\"") == "https://example.com", "surrounding quotes are dropped");
  check(Url(L"hello world") == "https://www.google.com/search?q=hello%20world", "words are searched for");
  check(Url(L"wikipedia") == "https://www.google.com/search?q=wikipedia", "a single word is searched for");
  check(Url(L"c++ & co", L"duckduckgo") == "https://duckduckgo.com/?q=c%2B%2B%20%26%20co", "Sol's engine and encodeURIComponent");
  check(Url(L"привет", L"bing") == "https://www.bing.com/search?q=%D0%BF%D1%80%D0%B8%D0%B2%D0%B5%D1%82", "non-ASCII is UTF-8 encoded");
  check(Url(L"x", L"unknown") == "https://www.google.com/search?q=x", "an unknown engine falls back to Google");
  check(Search(L"name@example.com"), "an e-mail address is searched for");
  check(Url(L"user:pass@example.com") == "<none>", "credentials are never opened");
  check(Url(L"https://user@example.com") == "<none>", "credentials after a scheme are never opened");
  check(Url(L"javascript:alert(1)") == "<none>", "other schemes are not opened");
  check(Url(L"file:///C:/x.txt") == "<none>", "files are not opened from the tray bar");
  check(Url(L"example..com") == "<none>", "an invalid host is not opened");
  check(Url(L"a\\b.com") == "<none>", "backslashes are not opened");
  check(Url(L"") == "<none>" && Url(L"   ") == "<none>", "nothing typed opens nothing");
  check(Url(L"bad\x01text") == "<none>", "control characters open nothing");
  check(Url(L"example.com/\"x\"") == "https://example.com/%22x%22", "quotes in an address are escaped for the command line");
  const auto row = sol_tray::ResolveInput(L"wikipedia.org/wiki/Sol", L"google");
  check(row && row->title == L"wikipedia.org/wiki/Sol" && row->detail == L"wikipedia.org", "an address row shows its host");
  const auto search = sol_tray::ResolveInput(L"weather", L"brave");
  check(search && search->detail == L"Search Brave Search", "a search row names the engine");
  // Google's completions (as navigation/search_suggestions.cpp).
  check(sol_tray::ValidSuggestionQuery(L"wiki") && !sol_tray::ValidSuggestionQuery(L"w") && !sol_tray::ValidSuggestionQuery(L"a.com") &&
        !sol_tray::ValidSuggestionQuery(L"  ") && !sol_tray::ValidSuggestionQuery(L"a=b"), "suggestions only for searches of 2+ characters");
  // JSON escapes are spelled out with "\\u" here: the reply carries them as text.
  const auto parsed = sol_tray::ParseSuggestions("[\"wiki\",[\"wikipedia\",\"wiki \\u043f\\u0440\\u0438\",\"wikipedia\",\"say \\\"hi\\\"\",3,\"x\"]]");
  check(parsed.size() == 3 && parsed[0] == L"wikipedia" && parsed[1] == L"wiki при" && parsed[2] == L"say \"hi\"",
        "suggestions are decoded, deduplicated and stop at a non-string");
  const auto emoji = sol_tray::ParseSuggestions("[\"x\",[\"\\ud83d\\ude00 smile\"]]");
  check(emoji.size() == 1 && emoji[0] == L"\U0001F600 smile", "surrogate pairs decode");
  const auto raw = sol_tray::ParseSuggestions("[\"x\",[\"\xD0\xBF\xD1\x80\xD0\xB8\"]]");
  check(raw.size() == 1 && raw[0] == L"при", "UTF-8 text decodes");
  check(sol_tray::ParseSuggestions(R"(["x",["a","b","c","d","e","f","g"]])").size() == 5, "at most five suggestions");
  check(sol_tray::ParseSuggestions(R"(["x",["bad\u0001"]])").empty(), "control characters are dropped");
  check(sol_tray::ParseSuggestions("<html>").empty() && sol_tray::ParseSuggestions(R"({"a":1})").empty() &&
        sol_tray::ParseSuggestions(R"(["x","y"])").empty(), "other replies give nothing");
  check(sol_tray::ParseSuggestions(std::string(40000, '[')).empty(), "an oversized reply gives nothing");
  if (failures) return EXIT_FAILURE;
  std::puts("tray input: all checks passed");
  return EXIT_SUCCESS;
}
