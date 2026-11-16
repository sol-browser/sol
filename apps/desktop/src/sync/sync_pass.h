#pragma once
#include "sync/sync_folder.h"
#include "sync/sync_model.h"
#include <filesystem>
#include <set>
#include <string>

// One PC's sync setup and the sync pass (docs/sync.md, "On this PC" and
// "Records and merging"). CEF-free; the caller supplies the local values and
// applies the changes (workspace tree, password store).
namespace red::sync {
enum class JoinMode { Merge, UseSynced };

// <profile root>/Sync: sync.json (device, folder, vault id, pending join
// mode), vault-key.bin (protected for this OS user), base.sol (encrypted).
struct LocalSync {
  std::filesystem::path dir;
  std::string device;
  std::filesystem::path folder;  // The cloud folder chosen by the user.
  std::string vault;             // Raw vault id it was set up with.
  std::string join;              // "merge", "synced", or "" once the first pass committed.
  std::filesystem::path SyncDir() const { return SyncDirectory(folder); }
};

enum class SetupStatus { Ok, WrongPassphrase, WeakPassphrase, Corrupt, Io };
// Creates the vault in |cloud_folder| or joins the one there. |mode| matters
// only when joining. Stores the key and settings in |dir|.
SetupStatus SetUp(const std::filesystem::path& dir, const std::filesystem::path& cloud_folder, std::string_view passphrase,
                  JoinMode mode, LocalSync& out, OpenedVault& vault, const KdfParams& params = {});
// Removes this PC's device file and its local sync files. Other PCs keep their data.
// Removes this PC's sync files. If the folder is unavailable, its device file
// is remembered (pending-removal.json) and true is still returned.
bool TurnOff(const LocalSync& local);
// Deletes a device file TurnOff could not reach, once its folder is back.
// True when nothing is pending any more.
bool RemovePendingDeviceFile(const std::filesystem::path& dir);
bool IsSetUp(const std::filesystem::path& dir);

enum class LoadStatus { Ok, NotSetUp, KeyUnavailable, Corrupt };
LoadStatus Load(const std::filesystem::path& dir, LocalSync& out, OpenedVault& vault);

enum class PassStatus { Ok, FolderMissing, VaultChanged, Io };
struct Prepared {
  Prepared() = default;
  Prepared(const Prepared&) = delete;
  Prepared(Prepared&&) noexcept = default;
  Prepared& operator=(const Prepared&) = delete;
  Prepared& operator=(Prepared&&) noexcept = default;
  ~Prepared();
  State merged;      // Everything, with this PC's new changes stamped.
  State base;        // The base before this pass.
  Changes changes;   // What this PC must apply (Diff of merged against local).
  Values live;       // Live values of |merged| (what the PC should end up with).
  size_t devices = 0, unreadable = 0;  // Other PCs' files read, and those that failed.
  bool own_file_current = false;       // This PC's file already holds |merged|.
  bool use_synced_tree = false;        // Joining with UseSynced: replace this PC's tree part.
};
// Reads everything and merges. Nothing is written. With a pending UseSynced
// join, this PC's tree keys are not stamped (|IsTree| tells which keys are
// tree keys) and the tree comes only from other PCs; if they have none, the
// pass merges instead.
// Record kinds this version reads and writes: profile/, space/, node/,
// split/, password/, icon/. A record of any other kind came from a newer Sol;
// Prepare carries it unchanged instead of treating it as deleted here.
bool IsKnownRecordKey(std::string_view key);
PassStatus Prepare(const LocalSync& local, const OpenedVault& vault, const Values& local_values, int64_t now_ms,
                   bool (*is_tree)(std::string_view), Prepared& out);
// Live values of this PC's base (what the last committed pass left). Lets a
// caller that cannot read one kind of record (sol-sync and passwords) carry
// those records through unchanged.
bool ReadBaseValues(const LocalSync& local, const OpenedVault& vault, Values& out);
// After the caller applied |changes|: keys it could not apply keep their old
// base record, so the next pass delivers them again instead of turning them
// into deletions. Writes this PC's device file (only when changed) and the base.
PassStatus Commit(LocalSync& local, const OpenedVault& vault, Prepared& prepared, const std::set<std::string>& failed, bool* wrote = nullptr);
}  // namespace red::sync
