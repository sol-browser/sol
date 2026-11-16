#pragma once
#include "storage/tree_store.h"
#include "sync/sync_model.h"
#include <map>
#include <set>

// Workspace records for cloud-folder sync (docs/sync.md, "What syncs").
// Pure functions over TreeSnapshot; no store, CEF or file access.
namespace red::sync {
// profile/, space/, node/ and split/ values of |tree|: every Space and
// profile, every Pinned or Favorite entry with its folders, and splits made
// only of those. Today and archived tabs, splits with a Today tab, archived
// splits, activity and folder expansion stay on this PC.
Values TreeValues(const TreeSnapshot& tree);
bool IsTreeKey(std::string_view key);

// icon/<host>: the site icon (PNG, base64) of synced Pinned and Favorites, so
// a new PC shows them before their pages are opened (docs/sync.md).
bool IsIconKey(std::string_view key);
std::string IconKeyHost(std::string_view key);  // "" if not a valid icon key.
// The host whose icon a page shows: the lowercase DNS host of an http(s)
// address, as the favicon cache keys it; "" for anything else.
std::string SyncedIconHost(std::string_view url);
// Hosts of synced pages (Pinned and Favorites, not archived), each with the
// browsing profiles whose Spaces show it.
std::map<std::string, std::set<std::string>> SyncedIconHosts(const TreeSnapshot& tree);
// Adds this PC's icon records for |hosts| to |out|. A host whose icon is
// already in |base| keeps that record unchanged: an icon travels once, so two
// PCs holding different versions never send theirs back and forth. Otherwise
// this PC's PNG from |pngs|, if valid. Hosts no synced page shows get no
// record, so theirs are removed.
void AddIconValues(const std::map<std::string, std::set<std::string>>& hosts, const std::map<std::string, std::string>& pngs,
                   const Values& base, Values& out);
// The PNG of an icon record; false unless it is a PNG of at most 16 KiB.
bool ParseIconValue(std::string_view value, std::string& png);

struct TreeApply {
  TreeApply() = default;
  TreeApply(const TreeApply&) = default;
  TreeApply(TreeApply&&) noexcept = default;
  TreeApply& operator=(const TreeApply&) = default;
  TreeApply& operator=(TreeApply&&) noexcept = default;
  ~TreeApply() = default;
  bool ok = false;
  bool changed = false;  // |tree| differs from the local snapshot.
  TreeSnapshot tree;
};
// The local snapshot with its synced part replaced by |values| (tree keys of
// the merged state; other keys are ignored). Local-only entries are kept
// unless their Space is gone. Invalid or conflicting records are repaired
// deterministically: an entry whose folder is gone moves to its Space's
// Pinned root, Favorites beyond Sol's limit become Pinned in a Space of the
// same profile, and anything that cannot be placed is dropped. Profiles are
// never removed. ok=false (nothing to apply) only if no valid tree results.
TreeApply ApplyTreeValues(const TreeSnapshot& local, const Values& values);
}  // namespace red::sync
