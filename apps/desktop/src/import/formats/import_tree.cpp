#include "import/formats/import_tree.h"
#include <windows.h>
#include <bcrypt.h>
#include <array>
#include <map>
#include <set>

namespace red::importer {
std::string StableId(const std::string& source, const std::string& key) {
  const auto data = source + "\n" + key; std::array<UCHAR, 32> hash{}; BCRYPT_ALG_HANDLE alg = nullptr;
  if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0) return "";
  const auto status = BCryptHash(alg, nullptr, 0, reinterpret_cast<PUCHAR>(const_cast<char*>(data.data())), static_cast<ULONG>(data.size()), hash.data(), static_cast<ULONG>(hash.size()));
  BCryptCloseAlgorithmProvider(alg, 0); if (status != 0) return "";
  hash[6] = (hash[6] & 15) | 80; hash[8] = (hash[8] & 63) | 128;
  constexpr char hex[] = "0123456789abcdef"; std::string id;
  for (size_t i = 0; i < 16; ++i) { if (i == 4 || i == 6 || i == 8 || i == 10) id += '-'; id += hex[hash[i] >> 4]; id += hex[hash[i] & 15]; }
  return id;
}

TreeOutput BuildImportTree(const TreeInput& in) {
  TreeOutput out; auto& tree = out.tree;
  Space space; space.id = StableId(in.source, "space"); space.name = CleanLabel(in.label, 40, "Imported");
  tree.spaces.push_back(space); tree.active_space = space.id;
  std::set<std::string> ids;
  for (size_t i = 0; i < in.tabs.size() && i < 5000; ++i) {
    const auto& t = in.tabs[i]; if (!IsWebUrl(t.url)) continue;
    TreeNode n; n.id = StableId(in.source, "tab:" + std::to_string(i)); n.url = t.url;
    n.page_title = CleanLabel(t.title, 240, ""); n.title = CleanLabel(t.title, 240, "Imported tab");
    n.pinned = t.pinned; n.space = space.id;
    if (ids.insert(n.id).second) { tree.nodes.push_back(std::move(n)); ++out.tabs; }
  }
  if (!in.bookmarks.empty()) {
    const auto root = StableId(in.source, "bookmarks");
    std::set<std::string> folders; for (const auto& b : in.bookmarks) if (b.folder) folders.insert(b.id);
    std::vector<TreeNode> nodes;
    for (const auto& b : in.bookmarks) {
      if (!b.folder && !IsWebUrl(b.url)) continue;
      TreeNode n; n.id = StableId(in.source, "bookmark:" + b.id); n.folder = b.folder; n.pinned = true; n.custom_title = true;
      n.title = CleanLabel(b.title, 240, b.folder ? "Folder" : "Bookmark"); n.url = b.folder ? "" : b.url; n.expanded = false;
      n.parent = folders.contains(b.parent) ? StableId(in.source, "bookmark:" + b.parent) : root;
      if (!ids.insert(n.id).second) continue;
      if (n.folder) ++out.folders; else ++out.bookmarks;
      nodes.push_back(std::move(n));
    }
    if (out.bookmarks) {
      TreeNode r; r.id = root; r.title = CleanLabel(in.label + " bookmarks", 240, "Bookmarks"); r.folder = r.pinned = true; r.expanded = false; r.space = space.id;
      tree.nodes.push_back(std::move(r));
      for (auto& n : nodes) tree.nodes.push_back(std::move(n));
    }
  }
  std::map<std::string, int> positions;
  for (auto& n : tree.nodes) n.position = positions[n.parent]++;
  if (tree.nodes.size() > 10000) out.error = "This profile exceeds Sol's limit of 10,000 tabs and bookmarks. Nothing imported.";
  else if (!tree.nodes.empty() && !TreeStore::ValidateSnapshot(tree)) out.error = "The imported tabs and bookmarks could not be arranged. Nothing imported.";
  return out;
}
}  // namespace red::importer
