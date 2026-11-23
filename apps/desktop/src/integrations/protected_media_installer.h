#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include "include/cef_values.h"

namespace red {
class ProtectedMediaHost;

// Settings → Protected content. Downloads the pinned, VMP-signed helper
// archive (integrations/protected_media_release.h), checks its size and
// SHA-256 while it arrives, unpacks it with Windows' tar.exe into a new
// version folder, grants the sandbox read access and only then makes it
// visible under its final name. Remove stops the helper first. All calls on
// the browser UI thread. docs/codecs.md#protected-content-install
class ProtectedMediaInstaller : public std::enable_shared_from_this<ProtectedMediaInstaller> {
 public:
  ProtectedMediaInstaller(std::filesystem::path base, std::shared_ptr<ProtectedMediaHost> host);
  ~ProtectedMediaInstaller();

  // Removes unfinished downloads and versions other than the one in use.
  // When a previously installed helper is older than the pinned one, starts
  // the update right away: the person already chose to have it.
  void Prepare();
  bool Install();
  void Cancel();
  // |data|: the folder with the helper's sign-ins and site data, also removed.
  bool Remove(const std::filesystem::path& data);
  // {state: absent|downloading|installing|installed|removing|failed,
  //  percent, error, outdated, manual, available, download, installed}
  CefRefPtr<CefDictionaryValue> Status() const;

 private:
  class Fetch;
  enum class Phase { Absent, Downloading, Installing, Installed, Removing, Failed };
  void Downloaded(bool ok, const std::string& error);
  void Unpacked(const std::string& error);
  void Fail(const std::string& error);
  std::filesystem::path base_;
  std::weak_ptr<ProtectedMediaHost> host_;
  Phase phase_ = Phase::Absent;
  int percent_ = 0;
  std::string error_;
  CefRefPtr<Fetch> fetch_;
};
}  // namespace red
