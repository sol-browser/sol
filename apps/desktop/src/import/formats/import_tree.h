#pragma once
#include "import/formats/import_types.h"
#include "storage/tree_store.h"

namespace red::import {
// Stable RFC 4122-shaped id from SHA-256(source || key): re-importing the
// same source yields the same ids, which TreeStore::Import refuses twice.
std::string StableId(const std::string& source, const std::string& key);

// One new Space named `label` holding the open tabs (pinned tabs → Pinned,
// others → Today) and a collapsed "<label> bookmarks" Pinned folder.
struct TreeInput {
  std::string source;  // Normalised source path + data selection.
  std::string label;   // "Chrome", "Edge", "Arc"...
  std::vector<OpenTab> tabs;
  std::vector<Bookmark> bookmarks;
};
struct TreeOutput { TreeSnapshot tree; int tabs = 0, bookmarks = 0, folders = 0; std::string error; };
TreeOutput BuildImportTree(const TreeInput& input);
}  // namespace red::import
