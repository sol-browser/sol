#include "translation/translator_host.h"

#include <windows.h>

#include <deque>
#include <map>
#include <regex>
#include <utility>
#include <vector>

#include "include/base/cef_callback.h"
#include "include/cef_browser.h"
#include "include/cef_app.h"
#include "include/cef_client.h"
#include "include/cef_display_handler.h"
#include "include/cef_devtools_message_observer.h"
#include "include/cef_parser.h"
#include "include/cef_request_context_handler.h"
#include "include/cef_scheme.h"
#include "include/wrapper/cef_closure_task.h"
#include "include/wrapper/cef_stream_resource_handler.h"
#include "translation/model_catalog.h"
#include "translation/model_store.h"

namespace red {
namespace fs = std::filesystem;
namespace {

constexpr char kPage[] = "sol://translator/index.html";
constexpr char kOrigin[] = "sol://translator/";
constexpr size_t kMaxReply = 8 * 1024 * 1024;
constexpr int kIdleCloseMs = 5 * 60 * 1000;
constexpr int kCallTimeoutMs = 3 * 60 * 1000;
constexpr char kPolicy[] =
    "default-src 'none'; script-src 'self' 'wasm-unsafe-eval'; worker-src 'self'; connect-src 'self'; "
    "base-uri 'none'; form-action 'none'; frame-ancestors 'none'";

struct Call {
  std::string expression;
  std::function<void(CefRefPtr<CefValue>)> reply;
};

class Client;

struct Host {
  fs::path profile, program;
  CefRefPtr<CefRequestContext> context;
  bool context_pending = false;
  std::vector<std::function<void(CefRefPtr<CefRequestContext>)>> context_waiters;
  HWND window = nullptr;
  CefRefPtr<Client> client;
  bool starting = false, ready = false;
  std::deque<Call> queue;
  unsigned generation = 0;
};

std::function<void(const std::string&, const std::string&)>& Probe() {
  static std::function<void(const std::string&, const std::string&)> probe;
  return probe;
}
void Note(const std::string& step, const std::string& detail = {}) { if (Probe()) Probe()(step, detail.substr(0, 400)); }

Host& TheHost() {
  static Host host;
  return host;
}

// Files of the translator page and the models on disk; nothing else, and only in the Translator profile.
class Resources final : public CefSchemeHandlerFactory {
 public:
  explicit Resources(fs::path root) : root_(std::move(root)) {}
  CefRefPtr<CefResourceHandler> Create(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, const CefString&,
                                       CefRefPtr<CefRequest> request) override {
    CefURLParts parts;
    if (!CefParseURL(request->GetURL(), parts) || CefString(&parts.host).ToString() != "translator") return nullptr;
    const std::string path = CefString(&parts.path).ToString();
    static const std::regex kSafe(R"(^/[A-Za-z0-9_.\-/]+$)");
    if (!std::regex_match(path, kSafe) || path.find("..") != std::string::npos) return nullptr;
    fs::path file;
    std::string mime = "application/octet-stream";
    if (path.starts_with("/models/")) {
      const auto rest = path.substr(8);
      const auto slash = rest.find('/'), dash = rest.find('-');
      if (slash == std::string::npos || dash == std::string::npos || dash > slash) return nullptr;
      const auto* model = FindTranslationModel(rest.substr(0, dash), rest.substr(dash + 1, slash - dash - 1));
      if (!model) return nullptr;
      const auto name = rest.substr(slash + 1);
      for (const auto* part : {&model->model, &model->shortlist, &model->vocab, &model->target_vocab})
        if (part->present() && part->name() == name) file = TranslationModelFilePath(*model, *part);
      if (file.empty()) return nullptr;
    } else {
      file = root_ / fs::path(path.substr(1)).make_preferred();
      std::error_code error;
      const auto real = fs::weakly_canonical(file, error), base = fs::weakly_canonical(root_, error);
      if (error || real.native().rfind(base.native(), 0) != 0) return nullptr;
      const auto extension = file.extension().string();
      if (extension == ".html") mime = "text/html";
      else if (extension == ".js") mime = "text/javascript";
      else if (extension == ".wasm") mime = "application/wasm";
      else return nullptr;
    }
    std::error_code error;
    if (!fs::is_regular_file(file, error)) return nullptr;
    const auto stream = CefStreamReader::CreateForFile(file.wstring());
    if (!stream) return nullptr;
    CefResponse::HeaderMap headers;
    headers.emplace("Content-Security-Policy", kPolicy);
    headers.emplace("X-Content-Type-Options", "nosniff");
    headers.emplace("Cache-Control", "no-store");
    return new CefStreamResourceHandler(200, "OK", mime, headers, stream);
  }

 private:
  fs::path root_;
  IMPLEMENT_REFCOUNTING(Resources);
};

class ContextReady final : public CefRequestContextHandler {
 public:
  void OnRequestContextInitialized(CefRefPtr<CefRequestContext> context) override {
    auto& host = TheHost();
    context->RegisterSchemeHandlerFactory("sol", "translator", new Resources(host.program / "translator"));
    host.context = context; host.context_pending = false;
    auto waiters = std::move(host.context_waiters);
    for (auto& waiter : waiters) waiter(context);
  }
  IMPLEMENT_REFCOUNTING(ContextReady);
};

std::string CatalogJson() {
  auto catalog = CefDictionaryValue::Create();
  for (const auto& model : TranslationModels()) {
    auto entry = CefDictionaryValue::Create();
    const auto add = [&entry](const char* key, const TranslationModelFile& file) {
      if (!file.present()) return;
      auto value = CefDictionaryValue::Create();
      value->SetString("name", std::string(file.name())); value->SetString("sha256", std::string(file.sha256));
      entry->SetDictionary(key, value);
    };
    add("model", model.model); add("shortlist", model.shortlist); add("vocab", model.vocab); add("targetVocab", model.target_vocab);
    catalog->SetDictionary(std::string(model.from) + "-" + std::string(model.to), entry);
  }
  auto value = CefValue::Create(); value->SetDictionary(catalog);
  return CefWriteJSON(value, JSON_WRITER_DEFAULT).ToString();
}

LRESULT CALLBACK HostProc(HWND window, UINT message, WPARAM w, LPARAM l) { return DefWindowProcW(window, message, w, l); }

HWND CreateHiddenWindow() {
  static const wchar_t* kClass = L"SolTranslatorHost";
  static bool registered = false;
  if (!registered) {
    WNDCLASSEXW wc{sizeof(wc)}; wc.lpfnWndProc = HostProc; wc.hInstance = GetModuleHandleW(nullptr); wc.lpszClassName = kClass;
    registered = RegisterClassExW(&wc) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
  }
  // Never shown: the page only needs a native parent to load and run script.
  return CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, kClass, L"", WS_POPUP, -32000, -32000, 320, 240, nullptr, nullptr,
                         GetModuleHandleW(nullptr), nullptr);
}

void FailAll();
void Flush();
void ScheduleIdleClose();

class Client final : public CefClient, public CefLifeSpanHandler, public CefLoadHandler, public CefRequestHandler,
                     public CefDisplayHandler, public CefDevToolsMessageObserver {
 public:
  CefRefPtr<CefDisplayHandler> GetDisplayHandler() override { return this; }
  bool OnConsoleMessage(CefRefPtr<CefBrowser>, cef_log_severity_t level, const CefString& message, const CefString& source, int line) override {
    if (level >= LOGSEVERITY_WARNING) Note("console", message.ToString() + " @" + source.ToString() + ":" + std::to_string(line));
    return true;
  }
  CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return this; }
  CefRefPtr<CefLoadHandler> GetLoadHandler() override { return this; }
  CefRefPtr<CefRequestHandler> GetRequestHandler() override { return this; }

  void OnAfterCreated(CefRefPtr<CefBrowser> browser) override {
    browser_ = browser; registration_ = browser->GetHost()->AddDevToolsMessageObserver(this);
    if (closing_) browser->GetHost()->CloseBrowser(true);
  }
  bool OnBeforePopup(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, int, const CefString&, const CefString&, cef_window_open_disposition_t, bool,
                     const CefPopupFeatures&, CefWindowInfo&, CefRefPtr<CefClient>&, CefBrowserSettings&, CefRefPtr<CefDictionaryValue>&, bool*) override {
    return true;
  }
  void OnBeforeClose(CefRefPtr<CefBrowser>) override {
    browser_ = nullptr; registration_ = nullptr;
    auto& host = TheHost();
    if (host.client.get() == this) {
      host.client = nullptr; host.ready = false; host.starting = false;
      FailAll();
      const HWND window = host.window; host.window = nullptr;
      CefPostTask(TID_UI, base::BindOnce([](HWND window) { if (window) DestroyWindow(window); }, window));
    }
  }
  // Only the translator page itself may load here.
  bool OnBeforeBrowse(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame> frame, CefRefPtr<CefRequest> request, bool, bool) override {
    return !frame->IsMain() || request->GetURL().ToString() != kPage;
  }
  void OnRenderProcessTerminated(CefRefPtr<CefBrowser>, TerminationStatus status, int, const CefString&) override {
    Note("renderer_terminated", std::to_string(status)); Close();
  }
  void OnLoadEnd(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame> frame, int status) override {
    if (!frame->IsMain() || ready_requested_) return;
    Note("load_end", frame->GetURL().ToString() + " " + std::to_string(status));
    if (status != 200 || frame->GetURL().ToString() != kPage) { Close(); return; }
    ready_requested_ = true;
    // The module may still be evaluating: wait for window.solTranslator, then hand it the catalog.
    Evaluate("(async()=>{for(let i=0;i<200&&!window.solTranslator;i++)await new Promise(r=>setTimeout(r,25));"
             "return window.solTranslator.init(" + CatalogJson() + ");})()",
             [](CefRefPtr<CefValue> value) {
               auto& host = TheHost();
               const bool ok = value && value->GetType() == VTYPE_DICTIONARY && value->GetDictionary()->GetBool("ready");
               Note("init", ok ? "ready" : value ? CefWriteJSON(value, JSON_WRITER_DEFAULT).ToString() : "null");
               host.starting = false;
               if (!ok) { if (host.client) host.client->Close(); return; }
               host.ready = true;
               Flush();
             });
  }
  void OnLoadError(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame> frame, ErrorCode code, const CefString& text, const CefString& url) override {
    Note("load_error", std::to_string(code) + " " + text.ToString() + " " + url.ToString());
    if (frame->IsMain()) Close();
  }
  void OnDevToolsMethodResult(CefRefPtr<CefBrowser>, int id, bool success, const void* data, size_t size) override {
    std::string json;
    if (success && size <= kMaxReply) json.assign(static_cast<const char*>(data), size);
    CefPostTask(TID_UI, base::BindOnce(&Client::Result, CefRefPtr<Client>(this), id, std::move(json)));
  }

  void Evaluate(const std::string& expression, std::function<void(CefRefPtr<CefValue>)> reply) {
    if (!browser_) { reply(nullptr); return; }
    auto params = CefDictionaryValue::Create();
    params->SetString("expression", expression); params->SetBool("returnByValue", true); params->SetBool("awaitPromise", true);
    const int id = browser_->GetHost()->ExecuteDevToolsMethod(0, "Runtime.evaluate", params);
    if (id <= 0) { reply(nullptr); return; }
    pending_[id] = std::move(reply);
    CefPostDelayedTask(TID_UI, base::BindOnce(&Client::Result, CefRefPtr<Client>(this), id, std::string()), kCallTimeoutMs);
  }
  void Close() {
    closing_ = true;
    auto pending = std::move(pending_); pending_.clear();
    for (auto& [id, reply] : pending) reply(nullptr);
    if (browser_) browser_->GetHost()->CloseBrowser(true);
  }
  bool Busy() const { return !pending_.empty(); }

 private:
  void Result(int id, std::string json) {
    const auto found = pending_.find(id);
    if (found == pending_.end()) return;
    auto reply = std::move(found->second); pending_.erase(found);
    CefRefPtr<CefValue> value;
    if (const auto parsed = json.empty() ? nullptr : CefParseJSON(json, JSON_PARSER_RFC); parsed && parsed->GetType() == VTYPE_DICTIONARY) {
      const auto reply_object = parsed->GetDictionary();
      const auto result = reply_object->HasKey("exceptionDetails") ? nullptr : reply_object->GetDictionary("result");
      // Copy: a dictionary value only references its parent, which goes away here.
      if (result && result->HasKey("value")) value = result->GetValue("value")->Copy();
    }
    if (!value) Note("call_failed", json.substr(0, 400));
    reply(value);
    ScheduleIdleClose();
  }
  CefRefPtr<CefBrowser> browser_;
  CefRefPtr<CefRegistration> registration_;
  std::map<int, std::function<void(CefRefPtr<CefValue>)>> pending_;
  bool ready_requested_ = false, closing_ = false;
  IMPLEMENT_REFCOUNTING(Client);
};

void FailAll() {
  auto queue = std::move(TheHost().queue); TheHost().queue.clear();
  for (auto& call : queue) call.reply(nullptr);
}

void Flush() {
  auto& host = TheHost();
  while (host.ready && host.client && !host.queue.empty()) {
    auto call = std::move(host.queue.front()); host.queue.pop_front();
    host.client->Evaluate(call.expression, std::move(call.reply));
  }
}

void ScheduleIdleClose() {
  const unsigned generation = ++TheHost().generation;
  CefPostDelayedTask(TID_UI, base::BindOnce([](unsigned generation) {
    auto& host = TheHost();
    if (host.generation != generation || !host.client || host.client->Busy() || !host.queue.empty()) return;
    host.client->Close();
  }, generation), kIdleCloseMs);
}

void StartPage() {
  auto& host = TheHost();
  if (host.client || host.starting) return;
  host.starting = true;
  WithTranslatorContext([](CefRefPtr<CefRequestContext> context) {
    auto& host = TheHost();
    Note("context", context ? "ready" : "none");
    if (!context) { host.starting = false; FailAll(); return; }
    host.window = CreateHiddenWindow();
    if (!host.window) { host.starting = false; FailAll(); return; }
    host.client = new Client();
    CefWindowInfo info; info.SetAsChild(host.window, CefRect(0, 0, 320, 240)); info.runtime_style = CEF_RUNTIME_STYLE_CHROME;
    CefBrowserSettings settings;
    if (!CefBrowserHost::CreateBrowser(info, host.client, kPage, settings, nullptr, context)) {
      host.client = nullptr; host.starting = false; DestroyWindow(host.window); host.window = nullptr; FailAll();
    }
  });
}

}  // namespace

void CloseTranslator() {
  auto& host = TheHost();
  ++host.generation;
  FailAll();
  StopTranslationModelDownloads();
  if (host.client) host.client->Close();
}

void ShutdownTranslator() {
  auto& host = TheHost();
  ++host.generation;
  FailAll();
  StopTranslationModelDownloads();
  if (host.client) {
    host.client->Close();
    // The message loop has ended: run CEF's work until the page has closed.
    const auto deadline = GetTickCount64() + 3000;
    while (host.client && GetTickCount64() < deadline) { CefDoMessageLoopWork(); Sleep(5); }
  }
  host.client = nullptr; host.ready = false; host.starting = false;
  if (host.window) { DestroyWindow(host.window); host.window = nullptr; }
  host.context = nullptr; host.context_waiters.clear();
}

void SetTranslatorProbe(std::function<void(const std::string&, const std::string&)> probe) { Probe() = std::move(probe); }

void ConfigureTranslator(const fs::path& profile, const fs::path& program_directory) {
  TheHost().profile = profile; TheHost().program = program_directory;
  SetTranslationModelDirectory(profile / "Translation Models");
}

void WithTranslatorContext(std::function<void(CefRefPtr<CefRequestContext>)> use) {
  auto& host = TheHost();
  if (host.context) { use(host.context); return; }
  if (host.profile.empty()) { use(nullptr); return; }
  host.context_waiters.push_back(std::move(use));
  if (host.context_pending) return;
  host.context_pending = true;
  CefRequestContextSettings settings;
  // Its own profile under the root, as the Shell profile is: no storage or process is shared with websites.
  CefString(&settings.cache_path) = (host.profile / "Translator").wstring();
  CefRequestContext::CreateContext(settings, new ContextReady());
}

void CallTranslator(std::string expression, std::function<void(CefRefPtr<CefValue>)> reply) {
  auto& host = TheHost();
  host.queue.push_back({std::move(expression), std::move(reply)});
  ++host.generation;
  if (host.ready) Flush();
  else StartPage();
}

}  // namespace red
