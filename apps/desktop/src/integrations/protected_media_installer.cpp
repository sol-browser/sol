#include "integrations/protected_media_installer.h"

#include <windows.h>
#include <bcrypt.h>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string_view>
#include <system_error>
#include <thread>
#include <vector>
#include "app/updates.h"
#include "integrations/protected_media_host.h"
#include "integrations/protected_media_layout.h"
#include "integrations/protected_media_release.h"
#include "include/base/cef_callback.h"
#include "include/cef_request_context.h"
#include "include/cef_urlrequest.h"
#include "include/wrapper/cef_closure_task.h"

#pragma comment(lib, "bcrypt.lib")

namespace red {
namespace fs = std::filesystem;
namespace release = protected_media_release;
namespace {
fs::path Pinned(const fs::path& base) { return base / release::kId; }
fs::path Archive(const fs::path& base) { return base / (std::string(release::kId) + ".zip.part"); }
fs::path Staging(const fs::path& base) { return base / (std::string(release::kId) + ".unpacking"); }

std::string Source() {
  char* value = nullptr;
  size_t length = 0;
  std::string url = release::kUrl;
  if (_dupenv_s(&value, &length, "SOL_PROTECTED_MEDIA_URL") == 0 && value) {
    if (ProtectedMediaSourceAllowed(value)) url = value;
    free(value);
  }
  return url;
}

// Incremental SHA-256 over the bytes as they arrive (Windows CNG).
class Sha256 {
 public:
  Sha256() {
    if (BCryptOpenAlgorithmProvider(&algorithm_, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0 ||
        BCryptCreateHash(algorithm_, &hash_, nullptr, 0, nullptr, 0, 0) != 0) failed_ = true;
  }
  ~Sha256() {
    if (hash_) BCryptDestroyHash(hash_);
    if (algorithm_) BCryptCloseAlgorithmProvider(algorithm_, 0);
  }
  void Add(const void* data, size_t length) {
    if (!failed_ && BCryptHashData(hash_, static_cast<PUCHAR>(const_cast<void*>(data)), static_cast<ULONG>(length), 0) != 0)
      failed_ = true;
  }
  std::string Hex() {
    unsigned char digest[32];
    if (failed_ || BCryptFinishHash(hash_, digest, sizeof(digest), 0) != 0) return {};
    std::string text;
    char pair[3];
    for (const unsigned char byte : digest) { std::snprintf(pair, sizeof(pair), "%02x", byte); text += pair; }
    return text;
  }

 private:
  BCRYPT_ALG_HANDLE algorithm_ = nullptr;
  BCRYPT_HASH_HANDLE hash_ = nullptr;
  bool failed_ = false;
};

// Windows' tar.exe (bsdtar, in every supported Windows 10/11) unpacks the zip.
// The archive's hash was checked first, so its entries are the ones we packed.
std::string Unpack(const fs::path& archive, const fs::path& staging) {
  std::error_code error;
  fs::remove_all(staging, error);
  if (!fs::create_directories(staging, error)) return "disk";
  wchar_t system[MAX_PATH];
  if (!GetSystemDirectoryW(system, MAX_PATH)) return "unpack";
  const auto tar = fs::path(system) / L"tar.exe";
  std::wstring command = L"\"" + tar.wstring() + L"\" -x -f \"" + archive.wstring() + L"\" -C \"" + staging.wstring() + L"\"";
  STARTUPINFOW startup{sizeof(startup)};
  PROCESS_INFORMATION process{};
  if (!CreateProcessW(tar.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW | BELOW_NORMAL_PRIORITY_CLASS,
                      nullptr, nullptr, &startup, &process)) return "unpack";
  CloseHandle(process.hThread);
  DWORD code = 1;
  if (WaitForSingleObject(process.hProcess, 10 * 60 * 1000) != WAIT_OBJECT_0) TerminateProcess(process.hProcess, 1);
  GetExitCodeProcess(process.hProcess, &code);
  CloseHandle(process.hProcess);
  if (code != 0 || !ProtectedMediaComplete(staging)) return "unpack";
  if (!GrantSandboxAccess(staging)) return "disk";
  return {};
}

// Removes every helper version and unfinished download in |base|.
bool RemoveVersions(const fs::path& base) {
  std::error_code error;
  bool ok = true;
  for (fs::directory_iterator it(base, error), end; !error && it != end; it.increment(error)) {
    const auto name = it->path().filename().wstring();
    const bool version = ProtectedMediaVersionName(name);
    const bool leftover = name.ends_with(L".unpacking") || name.ends_with(L".zip.part") || name.ends_with(L".removing");
    if (!version && !leftover) continue;
    std::error_code remove;
    fs::remove_all(it->path(), remove);
    ok &= !remove;
  }
  return ok && !error;
}
}  // namespace

class ProtectedMediaInstaller::Fetch final : public CefURLRequestClient {
 public:
  Fetch(fs::path target, std::weak_ptr<ProtectedMediaInstaller> owner) : target_(std::move(target)), owner_(std::move(owner)) {}
  bool Start(const std::string& url) {
    std::error_code error;
    fs::create_directories(target_.parent_path(), error);
    out_.open(target_, std::ios::binary | std::ios::trunc);
    if (!out_) return false;
    auto request = CefRequest::Create();
    request->SetURL(url);
    request->SetMethod("GET");
    // No cookies or credentials: UR_FLAG_ALLOW_STORED_CREDENTIALS is not set.
    request->SetFlags(UR_FLAG_DISABLE_CACHE);
    request_ = CefURLRequest::Create(request, this, CefRequestContext::GetGlobalContext());
    return request_ != nullptr;
  }
  void Cancel() {
    cancelled_ = true;
    if (request_) request_->Cancel();
  }
  void OnRequestComplete(CefRefPtr<CefURLRequest> request) override {
    out_.close();
    const auto response = request->GetResponse();
    std::string error;
    if (request->GetRequestStatus() != UR_SUCCESS || !response || response->GetStatus() != 200) error = "network";
    else if (failed_ || received_ != release::kSize || hash_.Hex() != release::kSha256) error = "integrity";
    if (write_failed_) error = "disk";
    request_ = nullptr;
    if (cancelled_) return;
    if (const auto owner = owner_.lock()) owner->Downloaded(error.empty(), error);
  }
  void OnUploadProgress(CefRefPtr<CefURLRequest>, int64_t, int64_t) override {}
  void OnDownloadProgress(CefRefPtr<CefURLRequest>, int64_t, int64_t) override {}
  void OnDownloadData(CefRefPtr<CefURLRequest> request, const void* data, size_t length) override {
    if (failed_ || write_failed_) return;
    received_ += length;
    if (received_ > release::kSize) { failed_ = true; request->Cancel(); return; }
    hash_.Add(data, length);
    out_.write(static_cast<const char*>(data), static_cast<std::streamsize>(length));
    if (!out_) { write_failed_ = true; request->Cancel(); return; }
    if (const auto owner = owner_.lock()) owner->percent_ = static_cast<int>(received_ * 100 / release::kSize);
  }
  bool GetAuthCredentials(bool, const CefString&, int, const CefString&, const CefString&,
                          CefRefPtr<CefAuthCallback>) override { return false; }

 private:
  fs::path target_;
  std::weak_ptr<ProtectedMediaInstaller> owner_;
  std::ofstream out_;
  Sha256 hash_;
  CefRefPtr<CefURLRequest> request_;
  unsigned long long received_ = 0;
  bool failed_ = false, write_failed_ = false, cancelled_ = false;
  IMPLEMENT_REFCOUNTING(Fetch);
};

ProtectedMediaInstaller::ProtectedMediaInstaller(fs::path base, std::shared_ptr<ProtectedMediaHost> host)
    : base_(std::move(base)), host_(host) {}
ProtectedMediaInstaller::~ProtectedMediaInstaller() { if (fetch_) fetch_->Cancel(); }

void ProtectedMediaInstaller::Prepare() {
  if (ProtectedMediaComplete(base_)) return;  // Staged by hand for development: left alone.
  std::error_code error;
  const auto runtime = ProtectedMediaRuntime(base_, release::kId);
  for (fs::directory_iterator it(base_, error), end; !error && it != end; it.increment(error)) {
    const auto name = it->path().filename().wstring();
    const bool leftover = name.ends_with(L".unpacking") || name.ends_with(L".zip.part") || name.ends_with(L".removing");
    // An older version stays while it is the one in use, until the pinned one replaces it.
    const bool stale = ProtectedMediaVersionName(name) && it->path() != runtime;
    std::error_code remove;
    if (leftover || stale) fs::remove_all(it->path(), remove);
  }
  if (!runtime.empty() && runtime != Pinned(base_)) Install();
}

bool ProtectedMediaInstaller::Install() {
  if (phase_ == Phase::Downloading || phase_ == Phase::Installing || phase_ == Phase::Removing) return false;
  if (ProtectedMediaComplete(base_) || ProtectedMediaComplete(Pinned(base_))) return false;
  error_.clear();
  percent_ = 0;
  fetch_ = new Fetch(Archive(base_), weak_from_this());
  phase_ = Phase::Downloading;
  if (!fetch_->Start(Source())) { fetch_ = nullptr; Fail("disk"); return false; }
  return true;
}

void ProtectedMediaInstaller::Cancel() {
  if (phase_ != Phase::Downloading || !fetch_) return;
  fetch_->Cancel();
  fetch_ = nullptr;
  std::error_code error;
  fs::remove(Archive(base_), error);
  phase_ = Phase::Absent;
  percent_ = 0;
}

void ProtectedMediaInstaller::Downloaded(bool ok, const std::string& error) {
  fetch_ = nullptr;
  if (!ok) {
    std::error_code remove;
    fs::remove(Archive(base_), remove);
    Fail(error);
    return;
  }
  phase_ = Phase::Installing;
  percent_ = 100;
  std::thread([folder = base_, weak = weak_from_this()] {
    std::string error = Unpack(Archive(folder), Staging(folder));
    std::error_code fs_error;
    if (error.empty()) {
      fs::remove_all(Pinned(folder), fs_error);
      fs::rename(Staging(folder), Pinned(folder), fs_error);
      if (fs_error) error = "disk";
    }
    fs::remove(Archive(folder), fs_error);
    if (!error.empty()) fs::remove_all(Staging(folder), fs_error);
    CefPostTask(TID_UI, base::BindOnce([](std::weak_ptr<ProtectedMediaInstaller> weak, std::string error) {
      if (const auto owner = weak.lock()) owner->Unpacked(error);
    }, weak, error));
  }).detach();
}

void ProtectedMediaInstaller::Unpacked(const std::string& error) {
  if (!error.empty()) { Fail(error); return; }
  phase_ = Phase::Installed;
  // An older version may still run open tabs; Prepare removes it next start.
  if (const auto host = host_.lock()) host->RetryFailed();
}

void ProtectedMediaInstaller::Fail(const std::string& error) {
  phase_ = Phase::Failed;
  error_ = error;
  percent_ = 0;
}

bool ProtectedMediaInstaller::Remove(const fs::path& data) {
  if (phase_ == Phase::Installing || phase_ == Phase::Removing || ProtectedMediaComplete(base_)) return false;
  Cancel();
  HANDLE process = nullptr;
  if (const auto host = host_.lock()) process = host->Shutdown();
  phase_ = Phase::Removing;
  error_.clear();
  std::thread([folder = base_, data, process, weak = weak_from_this()] {
    if (process) {
      WaitForSingleObject(process, 15000);
      CloseHandle(process);
    }
    // Its renderer and GPU processes can outlive the main one by a moment.
    bool ok = false;
    for (int attempt = 0; attempt < 20 && !ok; ++attempt) {
      ok = RemoveVersions(folder);
      if (!ok) Sleep(500);
    }
    std::error_code error;
    if (ok && !data.empty()) fs::remove_all(data, error);
    const std::string result = !ok || error ? "inUse" : "";
    CefPostTask(TID_UI, base::BindOnce([](std::weak_ptr<ProtectedMediaInstaller> weak, std::string error) {
      const auto owner = weak.lock();
      if (!owner) return;
      if (error.empty()) owner->phase_ = Phase::Absent;
      else owner->Fail(error);
    }, weak, result));
  }).detach();
  return true;
}

CefRefPtr<CefDictionaryValue> ProtectedMediaInstaller::Status() const {
  auto status = CefDictionaryValue::Create();
  const bool manual = ProtectedMediaComplete(base_);
  const auto runtime = ProtectedMediaRuntime(base_, release::kId);
  const char* state = "absent";
  switch (phase_) {
    case Phase::Downloading: state = "downloading"; break;
    case Phase::Installing: state = "installing"; break;
    case Phase::Removing: state = "removing"; break;
    case Phase::Failed: state = runtime.empty() ? "failed" : "installed"; break;
    case Phase::Absent: case Phase::Installed: state = runtime.empty() ? "absent" : "installed"; break;
  }
  status->SetString("state", state);
  status->SetInt("percent", percent_);
  status->SetString("error", phase_ == Phase::Failed ? error_ : "");
  status->SetBool("manual", manual);
  status->SetBool("outdated", !manual && !runtime.empty() && runtime != Pinned(base_));
  status->SetBool("available", *release::kUrl != '\0');
  status->SetDouble("download", static_cast<double>(release::kSize));
  status->SetDouble("installed", static_cast<double>(release::kInstalledSize));
  return status;
}
}  // namespace red
