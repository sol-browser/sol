#include "net.h"
#include <windows.h>
#include <bcrypt.h>
#include <winhttp.h>
#include <cstdio>
#include <memory>
#include <vector>

namespace sol_installer {
namespace {
struct HandleCloser { void operator()(void* handle) const { if (handle) WinHttpCloseHandle(handle); } };
using Handle = std::unique_ptr<void, HandleCloser>;

std::wstring SystemMessage(DWORD code) {
  wchar_t* text = nullptr;
  const DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_FROM_HMODULE | FORMAT_MESSAGE_IGNORE_INSERTS;
  FormatMessageW(flags, GetModuleHandleW(L"winhttp.dll"), code, 0, reinterpret_cast<wchar_t*>(&text), 0, nullptr);
  std::wstring message = text ? text : L"";
  if (text) LocalFree(text);
  while (!message.empty() && (message.back() == L'\n' || message.back() == L'\r' || message.back() == L' ' || message.back() == L'.')) message.pop_back();
  if (message.empty()) message = L"Network error " + std::to_wstring(code);
  return message;
}

bool Fail(std::wstring* error, std::wstring message) { if (error) *error = std::move(message); return false; }

// Opens a GET request and waits for a 200 response.
Handle Open(void* session, const std::wstring& url, Handle& connection, std::wstring* error) {
  URL_COMPONENTS parts{sizeof(parts)};
  wchar_t host[256]{}, path[2048]{};
  parts.lpszHostName = host; parts.dwHostNameLength = 256;
  parts.lpszUrlPath = path; parts.dwUrlPathLength = 2048;
  if (!WinHttpCrackUrl(url.c_str(), 0, 0, &parts)) { Fail(error, L"Invalid download address"); return nullptr; }
  const std::wstring host_name(host, parts.dwHostNameLength);
  const bool secure = parts.nScheme == INTERNET_SCHEME_HTTPS;
  const bool loopback = host_name == L"127.0.0.1" || host_name == L"localhost";
  if (!secure && !loopback) { Fail(error, L"Refusing an insecure download address"); return nullptr; }
  connection.reset(WinHttpConnect(session, host_name.c_str(), parts.nPort, 0));
  if (!connection) { Fail(error, SystemMessage(GetLastError())); return nullptr; }
  Handle request(WinHttpOpenRequest(connection.get(), L"GET", std::wstring(path, parts.dwUrlPathLength).c_str(), nullptr,
                                    WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, secure ? WINHTTP_FLAG_SECURE : 0));
  if (!request) { Fail(error, SystemMessage(GetLastError())); return nullptr; }
  DWORD policy = WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP;
  WinHttpSetOption(request.get(), WINHTTP_OPTION_REDIRECT_POLICY, &policy, sizeof(policy));
  if (!WinHttpSendRequest(request.get(), WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
      !WinHttpReceiveResponse(request.get(), nullptr)) {
    Fail(error, SystemMessage(GetLastError())); return nullptr;
  }
  DWORD status = 0, size = sizeof(status);
  if (!WinHttpQueryHeaders(request.get(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                           &status, &size, WINHTTP_NO_HEADER_INDEX)) {
    Fail(error, SystemMessage(GetLastError())); return nullptr;
  }
  if (status != 200) {
    Fail(error, status == 404 ? L"No release found (HTTP 404)" : L"The server answered HTTP " + std::to_wstring(status));
    return nullptr;
  }
  return request;
}

uint64_t ContentLength(void* request) {
  wchar_t text[32]{};
  DWORD size = sizeof(text);
  if (!WinHttpQueryHeaders(request, WINHTTP_QUERY_CONTENT_LENGTH, WINHTTP_HEADER_NAME_BY_INDEX, text, &size, WINHTTP_NO_HEADER_INDEX)) return 0;
  return _wcstoui64(text, nullptr, 10);
}
}  // namespace

Http::Http(const std::wstring& user_agent) {
  session_ = WinHttpOpen(user_agent.c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
  if (session_) {
    WinHttpSetTimeouts(session_, 15000, 15000, 30000, 60000);
    DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 | WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
    if (!WinHttpSetOption(session_, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols))) {
      protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;  // Before TLS 1.3 support.
      WinHttpSetOption(session_, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols));
    }
  }
}

Http::~Http() { if (session_) WinHttpCloseHandle(session_); }

std::optional<std::string> Http::Fetch(const std::wstring& url, size_t limit, std::wstring* error) {
  if (!session_) { Fail(error, SystemMessage(GetLastError())); return std::nullopt; }
  Handle connection;
  const Handle request = Open(session_, url, connection, error);
  if (!request) return std::nullopt;
  std::string body;
  for (;;) {
    char buffer[16384];
    DWORD read = 0;
    if (!WinHttpReadData(request.get(), buffer, sizeof(buffer), &read)) { Fail(error, SystemMessage(GetLastError())); return std::nullopt; }
    if (!read) return body;
    if (body.size() + read > limit) { Fail(error, L"The release information is too large"); return std::nullopt; }
    body.append(buffer, read);
  }
}

bool Http::Download(const std::wstring& url, const std::filesystem::path& file, uint64_t limit,
                    const std::function<void(uint64_t, uint64_t)>& progress, const std::atomic<bool>& cancel,
                    std::wstring* error) {
  if (!session_) return Fail(error, SystemMessage(GetLastError()));
  Handle connection;
  const Handle request = Open(session_, url, connection, error);
  if (!request) return false;
  const uint64_t total = ContentLength(request.get());
  if (total > limit) return Fail(error, L"The download is unexpectedly large");
  FILE* out = nullptr;
  _wfopen_s(&out, file.c_str(), L"wb");
  if (!out) return Fail(error, L"Cannot write to the temporary folder");
  uint64_t received = 0;
  std::vector<char> buffer(1 << 18);
  bool ok = true;
  for (;;) {
    if (cancel) { ok = Fail(error, L"Cancelled"); break; }
    DWORD read = 0;
    if (!WinHttpReadData(request.get(), buffer.data(), static_cast<DWORD>(buffer.size()), &read)) { ok = Fail(error, SystemMessage(GetLastError())); break; }
    if (!read) break;
    received += read;
    if (received > limit) { ok = Fail(error, L"The download is unexpectedly large"); break; }
    if (fwrite(buffer.data(), 1, read, out) != read) { ok = Fail(error, L"Not enough disk space"); break; }
    progress(received, total);
  }
  if (fclose(out) != 0 && ok) ok = Fail(error, L"Not enough disk space");
  if (ok && total && received != total) ok = Fail(error, L"The download was interrupted");
  if (!ok) { std::error_code ignored; std::filesystem::remove(file, ignored); }
  return ok;
}

std::string Sha256File(const std::filesystem::path& file) {
  BCRYPT_ALG_HANDLE algorithm = nullptr;
  BCRYPT_HASH_HANDLE hash = nullptr;
  std::string result;
  FILE* in = nullptr;
  _wfopen_s(&in, file.c_str(), L"rb");
  if (in && BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0)) &&
      BCRYPT_SUCCESS(BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0))) {
    std::vector<unsigned char> buffer(1 << 20);
    bool ok = true;
    size_t read = 0;
    while ((read = fread(buffer.data(), 1, buffer.size(), in)) > 0)
      if (!BCRYPT_SUCCESS(BCryptHashData(hash, buffer.data(), static_cast<ULONG>(read), 0))) { ok = false; break; }
    unsigned char digest[32];
    if (ok && !ferror(in) && BCRYPT_SUCCESS(BCryptFinishHash(hash, digest, sizeof(digest), 0))) {
      static constexpr char kHex[] = "0123456789abcdef";
      for (const unsigned char byte : digest) { result += kHex[byte >> 4]; result += kHex[byte & 15]; }
    }
  }
  if (hash) BCryptDestroyHash(hash);
  if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
  if (in) fclose(in);
  return result;
}
}  // namespace sol_installer
