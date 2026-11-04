#include "extensions/extension_catalog.h"
#include <windows.h>
#include <algorithm>
#include <fstream>
#include "extensions/extension_manifest.h"
#include "include/cef_parser.h"
#include "include/cef_preference.h"

namespace red {
namespace {
namespace fs = std::filesystem;
constexpr std::uintmax_t kMaxJson = 1 << 20, kMaxIcon = 512 * 1024;

std::optional<std::string> ReadFile(const fs::path& file, std::uintmax_t limit) {
  std::error_code error;
  const auto size = fs::file_size(file, error);
  if (error || size > limit || !fs::is_regular_file(file, error)) return std::nullopt;
  std::ifstream input(file, std::ios::binary);
  std::string bytes(static_cast<size_t>(size), '\0');
  if (!input.read(bytes.data(), static_cast<std::streamsize>(size))) return std::nullopt;
  return bytes;
}
CefRefPtr<CefDictionaryValue> ReadJson(const fs::path& file) {
  auto text = ReadFile(file, kMaxJson);
  if (!text) return nullptr;
  if (text->starts_with("\xEF\xBB\xBF")) text->erase(0, 3);
  auto value = CefParseJSON(*text, JSON_PARSER_ALLOW_TRAILING_COMMAS);
  return value && value->GetType() == VTYPE_DICTIONARY ? value->GetDictionary() : nullptr;
}
std::string String(CefRefPtr<CefDictionaryValue> d, const char* key) {
  return d && d->GetType(key) == VTYPE_STRING ? d->GetString(key).ToString() : std::string();
}
CefRefPtr<CefDictionaryValue> Dict(CefRefPtr<CefDictionaryValue> d, const char* key) {
  return d && d->GetType(key) == VTYPE_DICTIONARY ? d->GetDictionary(key) : nullptr;
}
std::string UserLocale() {
  wchar_t name[LOCALE_NAME_MAX_LENGTH] = {};
  if (!GetUserDefaultLocaleName(name, LOCALE_NAME_MAX_LENGTH)) return "en";
  return CefString(name).ToString();
}
// Resolves "__MSG_name__" from _locales; message keys are case-insensitive.
std::string Localize(const fs::path& root, CefRefPtr<CefDictionaryValue> manifest, const std::string& text) {
  const auto key = MessageKey(text);
  if (key.empty()) return text;
  auto lower = [](std::string value) { std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return std::tolower(c); }); return value; };
  for (const auto& locale : LocaleCandidates(UserLocale(), String(manifest, "default_locale"))) {
    const auto messages = ReadJson(root / "_locales" / locale / "messages.json");
    if (!messages) continue;
    CefDictionaryValue::KeyList keys; messages->GetKeys(keys);
    for (const auto& candidate : keys)
      if (lower(candidate.ToString()) == lower(key)) {
        const auto message = String(Dict(messages, candidate.ToString().c_str()), "message");
        if (!message.empty()) return message;
      }
  }
  return {};
}
// PNG icon as a data URL; other formats are skipped (the UI shows a letter).
std::string Icon(const fs::path& root, CefRefPtr<CefDictionaryValue> manifest, CefRefPtr<CefDictionaryValue> action) {
  std::vector<std::pair<int, std::string>> icons;
  auto collect = [&](CefRefPtr<CefDictionaryValue> d) {
    if (!d) return;
    CefDictionaryValue::KeyList keys; d->GetKeys(keys);
    for (const auto& key : keys) {
      const int size = std::atoi(key.ToString().c_str());
      if (size > 0 && size <= 1024 && d->GetType(key) == VTYPE_STRING) icons.emplace_back(size, d->GetString(key).ToString());
    }
  };
  collect(Dict(action, "default_icon"));
  if (icons.empty() && action && action->GetType("default_icon") == VTYPE_STRING) icons.emplace_back(32, String(action, "default_icon"));
  if (icons.empty()) collect(Dict(manifest, "icons"));
  std::vector<int> sizes; for (const auto& [size, _] : icons) sizes.push_back(size);
  const int size = PickIconSize(sizes, 32);
  const auto chosen = std::find_if(icons.begin(), icons.end(), [&](const auto& icon) { return icon.first == size; });
  if (chosen == icons.end()) return {};
  const auto relative = ExtensionRelativePath(chosen->second);
  if (!relative) return {};
  const auto file = root / fs::path(*relative);
  if (!InsideDirectory(root, file)) return {};
  const auto bytes = ReadFile(file, kMaxIcon);
  if (!bytes || !bytes->starts_with("\x89PNG\r\n\x1A\n")) return {};
  return "data:image/png;base64," + CefBase64Encode(bytes->data(), bytes->size()).ToString();
}
}  // namespace

std::vector<InstalledExtension> ExtensionCatalog::List(CefRefPtr<CefRequestContext> context) {
  latest_.clear();
  const auto value = context ? context->GetPreference("extensions.settings") : nullptr;
  if (!value || value->GetType() != VTYPE_DICTIONARY) return latest_;
  const auto settings = value->GetDictionary();
  CefDictionaryValue::KeyList keys; settings->GetKeys(keys);
  for (const auto& key : keys) {
    const auto id = key.ToString();
    const auto entry = settings->GetDictionary(key);
    if (!IsExtensionId(id) || !entry || entry->GetType("path") != VTYPE_STRING) continue;
    const int location = entry->GetType("location") == VTYPE_INT ? entry->GetInt("location") : 0;
    const auto reasons = entry->GetType("disable_reasons") == VTYPE_LIST ? entry->GetList("disable_reasons") : nullptr;
    const bool enabled = !(reasons && reasons->GetSize()) && !(entry->GetType("state") == VTYPE_INT && entry->GetInt("state") == 0);
    if (!IsListedLocation(location) || !enabled) continue;
    fs::path root(CefString(entry->GetString("path")).ToWString());
    if (root.is_relative()) {
      // Store installs: "<id>/<version>" under the profile's Extensions folder.
      root = profile_ / "Extensions" / root;
      if (!InsideDirectory(profile_ / "Extensions", root)) continue;
    }
    root = root.lexically_normal();
    auto cached = packages_.find(root);
    if (cached == packages_.end()) {
      const auto manifest = ReadJson(root / "manifest.json");
      if (!manifest) continue;
      InstalledExtension item; item.id = id;
      item.name = Localize(root, manifest, String(manifest, "name")).substr(0, 80);
      if (item.name.empty()) item.name = id;
      auto action = Dict(manifest, "action");
      if (!action) action = Dict(manifest, "browser_action");  // Manifest V2.
      if (!action) action = Dict(manifest, "page_action");
      item.action = action != nullptr;
      if (const auto popup = ExtensionRelativePath(String(action, "default_popup"), true)) item.popup = *popup;
      auto options = String(Dict(manifest, "options_ui"), "page");
      if (options.empty()) options = String(manifest, "options_page");
      if (const auto page = ExtensionRelativePath(options, true)) item.options = *page;
      item.icon = Icon(root, manifest, action);
      cached = packages_.emplace(root, std::move(item)).first;
    }
    latest_.push_back(cached->second);
    latest_.back().id = id;
  }
  std::sort(latest_.begin(), latest_.end(), [](const auto& a, const auto& b) { return a.name < b.name; });
  return latest_;
}
const InstalledExtension* ExtensionCatalog::Find(const std::string& id) const {
  const auto found = std::find_if(latest_.begin(), latest_.end(), [&](const auto& item) { return item.id == id; });
  return found == latest_.end() ? nullptr : &*found;
}
}  // namespace red
