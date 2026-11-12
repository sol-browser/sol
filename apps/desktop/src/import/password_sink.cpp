#include "import/password_sink.h"
#include "include/cef_browser.h"
#include "include/cef_client.h"
#include "include/cef_devtools_message_observer.h"
#include "include/cef_parser.h"
#include "include/base/cef_callback.h"
#include "include/wrapper/cef_closure_task.h"
#include <windows.h>

namespace red {
namespace {
constexpr char kPasswordManager[] = "chrome://password-manager/passwords";
constexpr int64_t kTimeoutMs = 120000;
// Loads the existing (origin, username) pairs so present sign-ins are kept.
// Only realms and usernames are read; Chromium never hands passwords to this list.
constexpr char kSetup[] = R"JS((async () => {
  const p = globalThis.chrome && chrome.passwordsPrivate;
  if (!p || typeof p.addPassword !== 'function' || typeof p.getSavedPasswordList !== 'function') return 'unavailable';
  const list = await new Promise(resolve => {
    try { const r = p.getSavedPasswordList(resolve); if (r && r.then) r.then(resolve, () => resolve([])); }
    catch (e) { try { p.getSavedPasswordList().then(resolve, () => resolve([])); } catch (e2) { resolve([]); } }
  });
  const seen = new Set();
  // Chromium 156 lists realms in affiliatedDomains[]; older builds used urls.signonRealm.
  for (const e of list || []) {
    const realms = (e.affiliatedDomains || []).map(d => d.signonRealm);
    if (e.urls && e.urls.signonRealm) realms.push(e.urls.signonRealm);
    for (const realm of realms) if (realm) seen.add(realm + '\n' + (e.username || ''));
  }
  globalThis.__redImportSeen = seen;
  return 'ready';
})())JS";
// %URL%, %USER%, %PASSWORD% are replaced with JSON string literals.
constexpr char kAdd[] = R"JS((async () => {
  const p = chrome.passwordsPrivate, url = %URL%, username = %USER%;
  let realm; try { realm = new URL(url).origin + '/'; } catch (e) { return 'error:invalid url'; }
  const key = realm + '\n' + username;
  if (globalThis.__redImportSeen.has(key)) return 'exists';
  const options = { url, username, password: %PASSWORD%, note: '', useAccountStore: false };
  try {
    await new Promise((resolve, reject) => {
      const done = () => { const e = globalThis.chrome && chrome.runtime && chrome.runtime.lastError; e ? reject(e) : resolve(); };
      let r; try { r = p.addPassword(options, done); } catch (e) { r = p.addPassword(options); }
      if (r && r.then) r.then(resolve, reject);
    });
  } catch (e) { return 'error:' + String((e && e.message) || e).slice(0, 160); }
  globalThis.__redImportSeen.add(key);
  return 'added';
})())JS";

std::string Quote(const std::string& s) {
  std::string out = "\"";
  for (unsigned char c : s) {
    switch (c) {
      case '"': out += "\\\""; break; case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break; case '\r': out += "\\r"; break; case '\t': out += "\\t"; break;
      default:
        if (c < 0x20 || c == 0x7f) { char hex[8]; snprintf(hex, sizeof(hex), "\\u%04x", c); out += hex; }
        else out += static_cast<char>(c);
    }
  }
  // U+2028/U+2029 are valid in JS string literals since ES2019; < > stay inert inside strings.
  return out + "\"";
}
void Replace(std::string& text, const std::string& token, const std::string& value) {
  const auto at = text.find(token); if (at != std::string::npos) text.replace(at, token.size(), value);
}
LRESULT CALLBACK HostProc(HWND window, UINT message, WPARAM w, LPARAM l) { return DefWindowProcW(window, message, w, l); }
HWND CreateHost() {
  static const wchar_t* kClass = L"RedPasswordImportHost";
  static bool registered = false;
  if (!registered) {
    WNDCLASSEXW wc{sizeof(wc)}; wc.lpfnWndProc = HostProc; wc.hInstance = GetModuleHandleW(nullptr); wc.lpszClassName = kClass;
    registered = RegisterClassExW(&wc) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
  }
  // Never shown: the page only needs a native parent to load and run script.
  return CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, kClass, L"", WS_POPUP, -32000, -32000, 800, 600, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
}

class Sink final : public CefClient, public CefLifeSpanHandler, public CefLoadHandler, public CefRequestHandler, public CefDevToolsMessageObserver {
 public:
  Sink(std::vector<importer::Login> logins, std::function<void(PasswordSinkResult)> done) : logins_(std::move(logins)), done_(std::move(done)) {}
  ~Sink() override { importer::WipeAll(logins_); }
  void Start() {
    host_ = CreateHost();
    if (!host_) { Finish("Could not prepare the password store"); return; }
    CefWindowInfo info; info.SetAsChild(host_, CefRect(0, 0, 800, 600)); info.runtime_style = CEF_RUNTIME_STYLE_CHROME;  // Alloy style blocks chrome://password-manager.
    CefBrowserSettings settings;
    // The global context is the profile whose password store websites use.
    if (!CefBrowserHost::CreateBrowser(info, this, kPasswordManager, settings, nullptr, CefRequestContext::GetGlobalContext())) {
      Finish("Could not open Sol's password manager"); return;
    }
    CefPostDelayedTask(TID_UI, base::BindOnce([](CefRefPtr<Sink> self) { self->Finish("Saving passwords timed out"); }, CefRefPtr<Sink>(this)), kTimeoutMs);
  }
  CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return this; }
  CefRefPtr<CefLoadHandler> GetLoadHandler() override { return this; }
  CefRefPtr<CefRequestHandler> GetRequestHandler() override { return this; }
  void OnAfterCreated(CefRefPtr<CefBrowser> browser) override {
    browser_ = browser; registration_ = browser->GetHost()->AddDevToolsMessageObserver(this);
    if (finished_) browser->GetHost()->CloseBrowser(true);
  }
  void OnBeforeClose(CefRefPtr<CefBrowser>) override {
    browser_ = nullptr;
    Finish("Sol's password manager closed unexpectedly");
    // The Chrome-style browser's own window goes first; its parent after.
    CefPostTask(TID_UI, base::BindOnce([](CefRefPtr<Sink> self) { self->DestroyHost(); }, CefRefPtr<Sink>(this)));
  }
  bool OnBeforePopup(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, int, const CefString&, const CefString&, cef_window_open_disposition_t, bool,
                     const CefPopupFeatures&, CefWindowInfo&, CefRefPtr<CefClient>&, CefBrowserSettings&, CefRefPtr<CefDictionaryValue>&, bool*) override { return true; }
  // Only Chromium's password manager page may load in this browser.
  bool OnBeforeBrowse(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame> frame, CefRefPtr<CefRequest> request, bool, bool) override {
    return frame->IsMain() && !request->GetURL().ToString().starts_with("chrome://password-manager/");
  }
  void OnLoadEnd(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, int) override {
    if (!frame->IsMain() || stage_ != Stage::Loading || finished_) return;
    if (!frame->GetURL().ToString().starts_with("chrome://password-manager/")) { Finish("Sol's password manager is not available"); return; }
    stage_ = Stage::Setup; Evaluate(browser, kSetup);
  }
  void OnLoadError(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame> frame, ErrorCode code, const CefString&, const CefString&) override {
    if (frame->IsMain() && code != ERR_ABORTED) Finish("Sol's password manager could not be opened");
  }
  void OnDevToolsMethodResult(CefRefPtr<CefBrowser> browser, int message_id, bool success, const void* result, size_t size) override {
    if (finished_ || message_id != pending_) return;
    std::string value;
    if (success) {
      const auto parsed = CefParseJSON(result, size, JSON_PARSER_RFC);
      const auto outer = parsed && parsed->GetType() == VTYPE_DICTIONARY ? parsed->GetDictionary() : nullptr;
      const auto inner = outer && outer->GetType("result") == VTYPE_DICTIONARY ? outer->GetDictionary("result") : nullptr;
      if (inner && inner->GetType("value") == VTYPE_STRING) value = inner->GetString("value").ToString().substr(0, 200);
    }
    if (stage_ == Stage::Setup) {
      if (value != "ready") { Finish("Sol's password manager is not available in this version"); return; }
      stage_ = Stage::Adding; Next(browser); return;
    }
    if (value == "added") ++result_.added;
    else if (value == "exists") ++result_.existing;
    else { ++result_.failed; if (first_error_.empty()) first_error_ = value.empty() ? "no result" : value; }
    Next(browser);
  }

 private:
  enum class Stage { Loading, Setup, Adding };
  void Evaluate(CefRefPtr<CefBrowser> browser, const std::string& expression) {
    auto params = CefDictionaryValue::Create();
    params->SetString("expression", expression); params->SetBool("awaitPromise", true);
    params->SetBool("returnByValue", true); params->SetBool("silent", true);
    pending_ = browser->GetHost()->ExecuteDevToolsMethod(0, "Runtime.evaluate", params);
    if (!pending_) Finish("Could not reach Sol's password manager");
  }
  void Next(CefRefPtr<CefBrowser> browser) {
    if (next_ >= logins_.size()) { Finish(""); return; }
    auto& login = logins_[next_++];
    std::string script = kAdd, password = Quote(login.password);
    Replace(script, "%URL%", Quote(login.url)); Replace(script, "%USER%", Quote(login.username)); Replace(script, "%PASSWORD%", password);
    importer::Wipe(password); importer::Wipe(login.password);
    Evaluate(browser, script); importer::Wipe(script);
  }
  void Finish(const std::string& error) {
    if (finished_) return;
    finished_ = true; importer::WipeAll(logins_);
    if (!error.empty()) result_.error = error;
    else if (result_.failed && !first_error_.empty()) result_.error = "Some passwords were not saved (" + first_error_ + ")";
    // Finish can run inside a DevTools observer callback: tear down afterwards,
    // never while CEF is still dispatching to this observer.
    CefPostTask(TID_UI, base::BindOnce([](CefRefPtr<Sink> self) { self->Teardown(); }, CefRefPtr<Sink>(this)));
    if (auto done = std::move(done_)) done(result_);
  }
  void Teardown() {
    registration_ = nullptr;
    if (browser_) browser_->GetHost()->CloseBrowser(true);
    else DestroyHost();
  }
  void DestroyHost() { if (host_) { DestroyWindow(host_); host_ = nullptr; } }
  std::vector<importer::Login> logins_;
  std::function<void(PasswordSinkResult)> done_;
  PasswordSinkResult result_;
  std::string first_error_;
  CefRefPtr<CefBrowser> browser_;
  CefRefPtr<CefRegistration> registration_;
  HWND host_ = nullptr;
  Stage stage_ = Stage::Loading;
  size_t next_ = 0;
  int pending_ = 0;
  bool finished_ = false;
  IMPLEMENT_REFCOUNTING(Sink);
};
}  // namespace

void SavePasswords(std::vector<importer::Login> logins, std::function<void(PasswordSinkResult)> done) {
  if (logins.empty()) { done({}); return; }
  CefRefPtr<Sink> sink = new Sink(std::move(logins), std::move(done));
  sink->Start();
}
}  // namespace red
