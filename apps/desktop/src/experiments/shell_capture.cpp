#include "experiments/shell_capture.h"
#include <fstream>
#include <vector>
#include "include/cef_devtools_message_observer.h"
#include "include/cef_parser.h"
#include "include/base/cef_callback.h"
#include "include/wrapper/cef_closure_task.h"

namespace red {
namespace {
class Capture final : public CefDevToolsMessageObserver {
 public:
  explicit Capture(std::filesystem::path output) : output_(std::move(output)) {}
  void SetMessageId(int id) { message_id_ = id; }
  void OnDevToolsMethodResult(CefRefPtr<CefBrowser>, int id, bool success,
                              const void* result, size_t size) override {
    if (!success || size > 24 * 1024 * 1024) return;
    // CEF allocates monotonically increasing IDs; reusing 9101 silently skips
    // later captures. Defer matching in case a reply arrives before Execute returns.
    CefPostTask(TID_UI, base::BindOnce(&Capture::Save, CefRefPtr<Capture>(this), id,
        std::string(static_cast<const char*>(result), size)));
  }
 private:
  void Save(int id, std::string json) {
    if (id != message_id_) return;
    auto value = CefParseJSON(json, JSON_PARSER_RFC);
    if (!value || value->GetType() != VTYPE_DICTIONARY) return;
    auto binary = CefBase64Decode(value->GetDictionary()->GetString("data"));
    if (!binary || binary->GetSize() > 16 * 1024 * 1024) return;
    std::vector<char> bytes(binary->GetSize()); binary->GetData(bytes.data(), bytes.size(), 0);
    std::ofstream output(output_, std::ios::binary); output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  }
  int message_id_ = -1;
  std::filesystem::path output_;
  IMPLEMENT_REFCOUNTING(Capture);
};
}
CefRefPtr<CefRegistration> CaptureShell(CefRefPtr<CefBrowser> shell, const std::filesystem::path& output) {
  CefRefPtr<Capture> capture = new Capture(output);
  auto registration = shell->GetHost()->AddDevToolsMessageObserver(capture);
  auto params = CefDictionaryValue::Create(); params->SetString("format", "png");
  capture->SetMessageId(shell->GetHost()->ExecuteDevToolsMethod(0, "Page.captureScreenshot", params));
  return registration;
}
}
