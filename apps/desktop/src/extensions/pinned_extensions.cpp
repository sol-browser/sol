#include "extensions/pinned_extensions.h"
#include <algorithm>
#include <fstream>
#include <windows.h>
#include "extensions/extension_catalog.h"
#include "include/cef_parser.h"
namespace red {
bool ValidExtensionId(const std::string& id) {
  return id.size() == 32 && std::all_of(id.begin(), id.end(), [](char c) { return c >= 'a' && c <= 'p'; });
}

void PinnedExtensions::Load(const std::filesystem::path& file) {
  file_ = file; ids_.clear(); stale_ = true;
  std::ifstream in(file, std::ios::binary);
  if (!in) return;
  std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  if (text.size() > 64 * 1024) return;
  const auto root = CefParseJSON(text, JSON_PARSER_RFC);
  const auto list = root && root->GetType() == VTYPE_DICTIONARY ? root->GetDictionary()->GetList("ids") : nullptr;
  for (size_t i = 0; list && i < list->GetSize() && ids_.size() < kMax; ++i)
    if (list->GetType(i) == VTYPE_STRING && ValidExtensionId(list->GetString(i)) && !Contains(list->GetString(i))) ids_.push_back(list->GetString(i));
}

bool PinnedExtensions::Contains(const std::string& id) const { return std::find(ids_.begin(), ids_.end(), id) != ids_.end(); }

bool PinnedExtensions::Set(const std::string& id, bool pinned) {
  if (!ValidExtensionId(id) || Contains(id) == pinned || (pinned && ids_.size() >= kMax)) return false;
  const auto before = ids_;
  if (pinned) ids_.push_back(id); else std::erase(ids_, id);
  if (!Save()) { ids_ = before; return false; }
  stale_ = true;
  return true;
}

bool PinnedExtensions::Save() const {
  if (file_.empty()) return false;
  auto list = CefListValue::Create();
  for (size_t i = 0; i < ids_.size(); ++i) list->SetString(i, ids_[i]);
  auto root = CefDictionaryValue::Create(); root->SetInt("version", 1); root->SetList("ids", list);
  auto value = CefValue::Create(); value->SetDictionary(root);
  std::error_code ignored; std::filesystem::create_directories(file_.parent_path(), ignored);
  const auto temporary = std::filesystem::path(file_).concat(L".tmp");
  { std::ofstream out(temporary, std::ios::binary | std::ios::trunc); out << CefWriteJSON(value, JSON_WRITER_DEFAULT).ToString(); if (!out) return false; }
  return MoveFileExW(temporary.c_str(), file_.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
}

CefRefPtr<CefListValue> PinnedExtensions::Items(ExtensionCatalog& catalog, CefRefPtr<CefRequestContext> context) {
  if (!stale_ && items_) return items_->Copy();
  items_ = CefListValue::Create(); stale_ = false;
  if (ids_.empty()) return items_->Copy();
  const auto installed = catalog.List(context);
  for (const auto& id : ids_) {
    const auto found = std::find_if(installed.begin(), installed.end(), [&](const auto& item) { return item.id == id; });
    if (found == installed.end()) continue;
    auto item = CefDictionaryValue::Create();
    item->SetString("id", id); item->SetString("name", found->name.substr(0, 80));
    if (found->icon.size() <= 64 * 1024) item->SetString("icon", found->icon);
    items_->SetDictionary(items_->GetSize(), item);
  }
  return items_->Copy();
}
}  // namespace red
