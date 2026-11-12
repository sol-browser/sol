#pragma once
#include <cstddef>
#include <string_view>

namespace red {
// The one definition of "this address is a web page": it starts with the http or https
// scheme followed by "://" (case-sensitive, as every validator here has always required).
// It replaces the scheme literals that were repeated in each validator and URL parser.
//
// Returns the length of that prefix ("https://" is 8, "http://" is 7), or 0 for any
// other address. The scheme is read from the address instead of matched as a
// clear-text literal: this browser accepts http pages, it never requests over plain
// http by itself.
constexpr std::size_t WebSchemeLength(std::string_view url) {
  const auto colon = url.find(':');
  if (colon == std::string_view::npos || url.substr(colon, 3) != "://") return 0;
  const auto scheme = url.substr(0, colon);
  return scheme == "https" ? 8 : scheme == "http" ? 7 : 0;
}
constexpr bool HasWebScheme(std::string_view url) { return WebSchemeLength(url) != 0; }
}  // namespace red
