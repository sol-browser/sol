#pragma once
#include "import/formats/import_tree.h"
#include <string_view>

namespace red::importer {
struct ArcSidebar {
  ArcSidebar() = default;
  ArcSidebar(const ArcSidebar&) = default;
  ArcSidebar& operator=(const ArcSidebar&) = default;
  // Moved, never copied, on the way out of the parser. MSVC's std::map move allocates a sentinel
  // and is not declared noexcept; running out of memory there ends the process anyway.
  ArcSidebar(ArcSidebar&&) noexcept = default;
  ArcSidebar& operator=(ArcSidebar&&) noexcept = default;
  ~ArcSidebar() = default;
  TreeOutput workspace;
  std::map<std::string, std::string> profile_directories;  // Sol profile id -> Arc's directory basename.
  std::map<std::string, std::string> favicon_item_ids;      // Sol tab id -> Arc sidebar or archive item id.
  int skipped = 0;
  int favorites = 0;
  int splits = 0;
  int archived = 0;
  bool archive_incomplete = false;
};

// Parse Arc's local main sidebar. The optional window state supplies expanded
// folder ids when importing from an installed copy of Arc. Its archive is
// separate from both the live sidebar and Chromium browsing history.
ArcSidebar ParseArcSidebar(std::string_view json, const std::string& source,
                           std::string_view windows = {}, std::string_view archive = {});
}  // namespace red::importer
