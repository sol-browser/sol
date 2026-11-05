#pragma once
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>

namespace sol_installer {
// HTTPS through WinHTTP with the system proxy. Plain http is accepted only for
// a loopback host (the local test feed). Redirects are followed (GitHub serves
// release files from its download host) but never from https to http.
class Http {
 public:
  explicit Http(const std::wstring& user_agent);
  ~Http();
  Http(const Http&) = delete;
  Http& operator=(const Http&) = delete;

  // A small text file (at most |limit| bytes).
  std::optional<std::string> Fetch(const std::wstring& url, size_t limit, std::wstring* error);
  // Streams |url| to |file|. |progress| gets (received, total or 0 when
  // unknown). Stops with false when |cancel| becomes true.
  bool Download(const std::wstring& url, const std::filesystem::path& file, uint64_t limit,
                const std::function<void(uint64_t, uint64_t)>& progress, const std::atomic<bool>& cancel,
                std::wstring* error);

 private:
  void* session_ = nullptr;
};

// Lowercase hex SHA-256 of a file; empty on a read error.
std::string Sha256File(const std::filesystem::path& file);
}  // namespace sol_installer
