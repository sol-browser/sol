#pragma once
#include <string_view>
namespace red {
// A cookie of |domain| is sent to |host|. Chromium stores host-only cookies
// without a leading dot (exact match) and domain cookies as ".example.com"
// (that host and its subdomains).
inline bool CookieDomainMatches(std::string_view domain, std::string_view host) {
  const bool domain_cookie = domain.starts_with('.');
  if (domain_cookie) domain.remove_prefix(1);
  if (domain.empty() || host.empty()) return false;
  return host == domain || (domain_cookie && (host.size() > domain.size() && host.ends_with(domain) && host[host.size() - domain.size() - 1] == '.'));
}
}  // namespace red
