#include "app/sync_service.h"
#include "app/browsing_profiles.h"
#include "app/session.h"
#include "history/favicon_cache.h"
#include "history/history_service.h"
#include "import/password_sink.h"
#include "include/base/cef_callback.h"
#include "include/base/cef_logging.h"
#include "include/wrapper/cef_closure_task.h"
#include "sync/sync_passwords.h"
#include "sync/sync_tree.h"
#include "workspace/controller.h"
#include <chrono>
#include <map>
#include <set>
#include <system_error>

namespace red {
namespace fs = std::filesystem;
using namespace sync;

struct SyncWorkerState {
  LocalSync local;
  OpenedVault vault;
  bool loaded = false;
  std::string load_error;
  ~SyncWorkerState() { vault.Close(); }
};

struct PassJob {
  PassJob() = default;
  PassJob(const PassJob&) = delete;
  PassJob(PassJob&&) noexcept = default;
  PassJob& operator=(const PassJob&) = delete;
  PassJob& operator=(PassJob&&) noexcept = default;
  ~PassJob() = default;
  TreeSnapshot tree;               // The shared tree the pass started from.
  std::vector<std::string> profiles;
  Prepared prepared;
  std::set<std::string> failed;    // Keys this PC could not apply.
  PassStatus status = PassStatus::Ok;
  std::string error;               // Settings message key.
  int passwords = 0, records = 0;
  bool wrote = false;
  // Site icons of synced Pinned and Favorites: host -> browsing profiles, and
  // the PNGs this PC has for them (read on the UI thread before the pass).
  std::map<std::string, std::set<std::string>> icon_hosts;
  std::map<std::string, std::string> icon_pngs;
};

namespace {
constexpr int64_t kStartDelayMs = 10000, kTreeDebounceMs = 30000, kPollMs = 60000, kPeriodicMs = 10 * 60000, kRetryMs = 15000;
int64_t NowMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}
std::string Utf8(const fs::path& path) {
  const auto text = path.u8string();
  return std::string(text.begin(), text.end());
}
const char* PassError(PassStatus status) {
  switch (status) {
    case PassStatus::Ok: return "";
    case PassStatus::FolderMissing: return "folderMissing";
    case PassStatus::VaultChanged: return "vaultChanged";
    case PassStatus::Io: return "io";
  }
  return "io";
}
bool TreeTouched(const Prepared& prepared) {
  return prepared.use_synced_tree ||
         std::any_of(prepared.changes.upsert.begin(), prepared.changes.upsert.end(), [](const auto& item) { return IsTreeKey(item.first); }) ||
         std::any_of(prepared.changes.remove.begin(), prepared.changes.remove.end(), [](const auto& key) { return IsTreeKey(key); });
}
void FailTree(PassJob& job) {
  for (const auto& [key, value] : job.prepared.changes.upsert) if (IsTreeKey(key)) job.failed.insert(key);
  for (const auto& key : job.prepared.changes.remove) if (IsTreeKey(key)) job.failed.insert(key);
}
}  // namespace

SyncService::SyncService(std::weak_ptr<AppSession> session, fs::path root)
    : session_(std::move(session)), root_(std::move(root)), dir_(root_ / "Sync"), worker_state_(std::make_shared<SyncWorkerState>()) {}

std::shared_ptr<SyncService> SyncService::Start(const std::shared_ptr<AppSession>& session, fs::path profile_root) {
  std::shared_ptr<SyncService> service(new SyncService(session, std::move(profile_root)));
  service->worker_ = CefThread::CreateThread("Cloud sync");
  if (!service->worker_) { service->error_ = "io"; return service; }
  const auto dir = service->dir_;
  service->worker_->GetTaskRunner()->PostTask(CefCreateClosureTask(base::BindOnce(
      [](std::weak_ptr<SyncService> weak, std::shared_ptr<SyncWorkerState> worker, fs::path dir) {
        RemovePendingDeviceFile(dir);
        if (IsSetUp(dir)) {
          const auto status = Load(dir, worker->local, worker->vault);
          worker->loaded = status == LoadStatus::Ok;
          worker->load_error = status == LoadStatus::Ok ? "" : status == LoadStatus::KeyUnavailable ? "keyUnavailable" : "corrupt";
        }
        CefPostTask(TID_UI, base::BindOnce([](std::weak_ptr<SyncService> weak, bool loaded, std::string error, fs::path folder,
                                              std::string device, std::string join, bool set_up) {
          const auto self = weak.lock();
          if (!self || self->stopped_) return;
          self->configured_ = set_up;
          self->error_ = error;
          // cef.log (warning level and up): codes only, never the passphrase or keys.
          if (set_up && !error.empty()) LOG(WARNING) << "Cloud sync: this PC's sync state could not be loaded (" << error << ")";
          if (loaded) { self->folder_ = folder; self->device_ = device; self->join_ = join; self->Schedule(kStartDelayMs); }
          self->Poll();
          self->Notify();
        }, weak, worker->loaded, worker->load_error, worker->local.folder, worker->local.device, worker->local.join, IsSetUp(dir)));
      }, std::weak_ptr<SyncService>(service), service->worker_state_, dir)));
  return service;
}

void SyncService::Stop() {
  stopped_ = true;
  ++schedule_generation_;
  if (worker_) worker_->Stop();
}

std::shared_ptr<red::State> SyncService::MainWindow() const {
  const auto session = session_.lock();
  if (!session) return nullptr;
  for (const auto& window : session->Windows())
    if (window->workspace_data == session->workspace && window->workspace && !window->private_window && !window->standalone_window &&
        !window->probe && window->workspace->ready()) return window;
  return nullptr;
}

void SyncService::Schedule(int64_t delay_ms) {
  if (stopped_ || !configured_) return;
  const auto at = NowMs() + delay_ms;
  // Keep an earlier pass that is already due; a debounce only moves a later one.
  if (scheduled_at_ && scheduled_at_ > NowMs() && scheduled_at_ <= at && delay_ms != kTreeDebounceMs) return;
  scheduled_at_ = at;
  const auto generation = ++schedule_generation_;
  CefPostDelayedTask(TID_UI, base::BindOnce([](std::weak_ptr<SyncService> weak, int64_t generation) {
    const auto self = weak.lock();
    if (!self || self->stopped_ || generation != self->schedule_generation_) return;
    self->scheduled_at_ = 0;
    self->RunPass();
  }, weak_from_this(), generation), delay_ms);
}

void SyncService::TreeChanged() {
  if (stopped_ || !configured_) return;
  const auto session = session_.lock();
  if (!session || !session->workspace->ready) return;
  std::string all;
  for (const auto& [key, value] : TreeValues(session->workspace->tree)) all.append(key).append(1, '\0').append(value).append(1, '\0');
  const auto hash = std::hash<std::string>{}(all);
  if (hash == tree_hash_) return;
  tree_hash_ = hash;
  Schedule(kTreeDebounceMs);
}

// Every minute: notice other PCs' files and saved-password changes by size
// and time only, and run the periodic pass.
void SyncService::Poll() {
  if (stopped_) return;
  CefPostDelayedTask(TID_UI, base::BindOnce([](std::weak_ptr<SyncService> weak) { if (const auto self = weak.lock()) self->Poll(); }, weak_from_this()), kPollMs);
  if (!worker_) return;
  if (!configured_) {
    // A file turning sync off could not delete (the folder was away).
    worker_->GetTaskRunner()->PostTask(CefCreateClosureTask(base::BindOnce([](fs::path dir) { RemovePendingDeviceFile(dir); }, dir_)));
    return;
  }
  if (last_success_ms_ && NowMs() - last_success_ms_ > kPeriodicMs) { Schedule(1000); return; }
  std::vector<std::string> profiles;
  if (const auto session = session_.lock()) for (const auto& profile : session->workspace->tree.profiles) profiles.push_back(profile.id);
  worker_->GetTaskRunner()->PostTask(CefCreateClosureTask(base::BindOnce(
      [](std::weak_ptr<SyncService> weak, std::shared_ptr<SyncWorkerState> worker, fs::path root, std::vector<std::string> profiles) {
        if (!worker->loaded) return;
        std::string signature;
        std::error_code error;
        auto add = [&](const fs::path& path) {
          const auto size = fs::file_size(path, error);
          const auto time = fs::last_write_time(path, error);
          signature += Utf8(path.filename()) + ":" + std::to_string(error ? 0 : size) + ":" + std::to_string(error ? 0 : time.time_since_epoch().count()) + ";";
        };
        for (fs::directory_iterator it(worker->local.SyncDir() / "devices", error), end; !error && it != end; it.increment(error))
          if (it->path().extension() == ".sol" && it->path().stem() != fs::path(worker->local.device)) add(it->path());
        for (const auto& profile : profiles) add(BrowsingProfileDirectory(root, profile) / "Login Data");
        CefPostTask(TID_UI, base::BindOnce([](std::weak_ptr<SyncService> weak, std::string signature) {
          const auto self = weak.lock();
          if (!self || self->stopped_) return;
          if (!self->poll_signature_.empty() && signature != self->poll_signature_) self->Schedule(3000);
          self->poll_signature_ = signature;
        }, weak, signature));
      }, weak_from_this(), worker_state_, root_, profiles)));
}

void SyncService::RunPass() {
  if (stopped_ || !configured_ || !worker_) return;
  if (running_) { again_ = true; return; }
  const auto session = session_.lock();
  if (!session || !session->workspace->ready || !MainWindow()) { Schedule(kRetryMs); return; }
  auto job = std::make_shared<PassJob>();
  job->tree = session->workspace->tree;
  for (const auto& profile : job->tree.profiles) job->profiles.push_back(profile.id);
  job->icon_hosts = SyncedIconHosts(job->tree);
  if (session->profiles)
    for (const auto& [host, profiles] : job->icon_hosts)
      for (const auto& id : profiles) {
        const auto profile = session->profiles->Get(MainWindow(), id);
        if (const auto png = profile && profile->favicons ? profile->favicons->FallbackPng(host) : std::nullopt) { job->icon_pngs.emplace(host, *png); break; }
      }
  running_ = true;
  Notify();
  worker_->GetTaskRunner()->PostTask(CefCreateClosureTask(base::BindOnce(
      [](std::weak_ptr<SyncService> weak, std::shared_ptr<SyncWorkerState> worker, std::shared_ptr<PassJob> job, fs::path root) {
        if (!worker->loaded) {
          job->status = PassStatus::Io; job->error = worker->load_error.empty() ? "io" : worker->load_error;
        } else {
          Values values = TreeValues(job->tree);
          Values base;
          bool base_read = false;
          for (const auto& profile : job->profiles) {
            auto read = ReadPasswords(root, profile);
            if (read.status == PasswordReadStatus::Ok || read.status == PasswordReadStatus::NoStore) {
              job->passwords += static_cast<int>(read.values.size());
              for (auto& item : read.values) values.insert(std::move(item));
              continue;
            }
            // Store busy or unreadable this time: carry its records unchanged.
            if (!base_read) { base_read = ReadBaseValues(worker->local, worker->vault, base); }
            const auto prefix = "password/" + profile + "/";
            for (const auto& [key, value] : base) if (key.starts_with(prefix)) values.emplace(key, value);
          }
          // Icons: an icon already synced keeps its record; new hosts send this PC's.
          if (!job->icon_hosts.empty() && !base_read) base_read = ReadBaseValues(worker->local, worker->vault, base);
          AddIconValues(job->icon_hosts, job->icon_pngs, base, values);
          job->icon_pngs.clear();
          WipeValues(base);
          job->status = Prepare(worker->local, worker->vault, values, NowMs(), &IsTreeKey, job->prepared);
          job->error = PassError(job->status);
          job->records = static_cast<int>(job->prepared.live.size());
          WipeValues(values);
        }
        CefPostTask(TID_UI, base::BindOnce([](std::weak_ptr<SyncService> weak, std::shared_ptr<PassJob> job) {
          const auto self = weak.lock();
          if (!self) return;
          if (job->status != PassStatus::Ok) { self->Finish(job, job->status, job->error); return; }
          self->Applied(job);
        }, weak, job));
      }, weak_from_this(), worker_state_, job, root_)));
}

void SyncService::Applied(std::shared_ptr<PassJob> job) {
  const auto session = session_.lock();
  const auto window = MainWindow();
  if (stopped_ || !session || !window) { Finish(job, PassStatus::Io, "io"); return; }
  // 1. The workspace, all at once, only if nobody changed it during the pass.
  auto passwords = [weak = weak_from_this(), job, window]() {
    const auto self = weak.lock();
    if (!self || self->stopped_) return;
    self->ApplyIcons(*job, window);
    std::map<std::string, std::vector<std::pair<std::string, PasswordOp>>> by_profile;
    std::string profile, realm, username, url, password;
    for (const auto& [key, value] : job->prepared.changes.upsert) {
      if (!IsPasswordKey(key)) continue;
      if (!ParsePasswordKey(key, profile, realm, username) || !ParsePasswordValue(value, url, password)) { job->failed.insert(key); continue; }
      PasswordOp op; op.kind = PasswordOp::Kind::Upsert; op.url = url; op.realm = realm; op.username = username; op.password = std::move(password);
      by_profile[profile].emplace_back(key, std::move(op));
    }
    for (const auto& key : job->prepared.changes.remove) {
      if (!IsPasswordKey(key)) continue;
      if (!ParsePasswordKey(key, profile, realm, username)) { job->failed.insert(key); continue; }
      PasswordOp op; op.kind = PasswordOp::Kind::Remove; op.realm = realm; op.username = username;
      by_profile[profile].emplace_back(key, std::move(op));
    }
    // 2. Passwords, one browsing profile after another.
    auto queue = std::make_shared<std::vector<std::pair<std::string, std::vector<std::pair<std::string, PasswordOp>>>>>();
    for (auto& item : by_profile) queue->emplace_back(item.first, std::move(item.second));
    auto next = std::make_shared<std::function<void(size_t)>>();
    *next = [weak, job, window, queue, next](size_t index) {
      const auto self = weak.lock();
      if (!self || self->stopped_) return;
      if (index >= queue->size()) {
        // 3. Commit: this PC's file and base.
        self->worker_->GetTaskRunner()->PostTask(CefCreateClosureTask(base::BindOnce(
            [](std::weak_ptr<SyncService> weak, std::shared_ptr<SyncWorkerState> worker, std::shared_ptr<PassJob> job) {
              const auto status = Commit(worker->local, worker->vault, job->prepared, job->failed, &job->wrote);
              CefPostTask(TID_UI, base::BindOnce([](std::weak_ptr<SyncService> weak, std::shared_ptr<PassJob> job, PassStatus status) {
                if (const auto self = weak.lock()) self->Finish(job, status, PassError(status));
              }, weak, job, status));
            }, weak, self->worker_state_, job)));
        return;
      }
      auto& [profile_id, items] = (*queue)[index];
      const auto session = self->session_.lock();
      const auto profile = session && session->profiles ? session->profiles->Get(window, profile_id) : nullptr;
      std::vector<std::string> keys;
      std::vector<PasswordOp> ops;
      for (auto& [key, op] : items) { keys.push_back(key); ops.push_back(std::move(op)); }
      if (!profile || !profile->initialized || !profile->context) {
        for (const auto& key : keys) job->failed.insert(key);
        (*next)(index + 1);
        return;
      }
      ApplyPasswordOps(profile->context, std::move(ops), [job, keys, next, index](PasswordOpsResult result) {
        for (size_t i = 0; i < keys.size(); ++i) if (i >= result.applied.size() || !result.applied[i]) job->failed.insert(keys[i]);
        (*next)(index + 1);
      });
    };
    (*next)(0);
  };
  if (!TreeTouched(job->prepared)) { passwords(); return; }
  const auto& current = session->workspace->tree;
  if (current.revision != job->tree.revision) { FailTree(*job); passwords(); return; }
  Values tree_values;
  for (const auto& [key, value] : job->prepared.live) if (IsTreeKey(key)) tree_values.emplace(key, value);
  auto applied = ApplyTreeValues(current, tree_values);
  if (!applied.ok) { FailTree(*job); passwords(); return; }
  if (!applied.changed) { passwords(); return; }
  window->workspace->ApplySyncedTree(std::move(applied.tree), job->tree.revision, [job, passwords](bool ok) {
    if (!ok) FailTree(*job);
    passwords();
  });
}

// Received site icons go into each browsing profile that shows the host (all
// profiles if none does yet): its icon store and cache, never replacing an
// icon the profile already has. Not tracked as failed: a record stays in the
// folder, and a profile that lacks the icon gets it with a later pass.
void SyncService::ApplyIcons(const PassJob& job, const std::shared_ptr<red::State>& window) {
  const auto session = session_.lock();
  if (!session || !session->profiles) return;
  const auto hosts = SyncedIconHosts(session->workspace->tree);
  std::map<std::string, std::map<std::string, std::string>> by_profile;
  std::string png;
  for (const auto& [key, value] : job.prepared.changes.upsert) {
    const auto host = IconKeyHost(key);
    if (host.empty() || !ParseIconValue(value, png)) continue;
    const auto found = hosts.find(host);
    if (found != hosts.end()) for (const auto& id : found->second) by_profile[id][host] = png;
    else for (const auto& id : job.profiles) by_profile[id][host] = png;
  }
  for (auto& [id, icons] : by_profile) {
    const auto profile = session->profiles->Get(window, id);
    if (!profile || !profile->history || !profile->favicons) continue;
    profile->history->ImportFavicons(std::move(icons), [cache = profile->favicons, weak = session_](std::optional<std::map<std::string, std::string>> saved) {
      if (!saved || saved->empty()) return;
      for (const auto& [host, icon] : *saved) cache->Seed(host, icon);
      if (const auto live = weak.lock())
        for (const auto& state : live->Windows()) if (state->workspace && state->favicons == cache) state->workspace->Refresh();
    });
  }
}

void SyncService::Finish(std::shared_ptr<PassJob> job, PassStatus status, const std::string& error) {
  running_ = false;
  // Changes only, so a pass every few minutes does not fill cef.log.
  if (error != error_) {
    if (error.empty()) LOG(WARNING) << "Cloud sync: passes work again";
    else LOG(WARNING) << "Cloud sync: pass failed (" << error << ")";
  }
  if (status == PassStatus::Ok && static_cast<int>(job->prepared.unreadable) != unreadable_ && job->prepared.unreadable)
    LOG(WARNING) << "Cloud sync: " << job->prepared.unreadable << " PC file(s) in the folder could not be opened with this vault key";
  if (status == PassStatus::Ok && !job->failed.empty()) {
    // Record kinds only ("password", "node"...), never keys or values.
    std::set<std::string> kinds;
    for (const auto& key : job->failed) kinds.insert(key.substr(0, key.find('/')));
    std::string list;
    for (const auto& kind : kinds) list += (list.empty() ? "" : ", ") + kind;
    LOG(WARNING) << "Cloud sync: " << job->failed.size() << " change(s) could not be applied here (" << list << "); retrying";
  }
  error_ = error;
  if (status == PassStatus::Ok) {
    last_success_ms_ = NowMs();
    devices_ = static_cast<int>(job->prepared.devices);
    unreadable_ = static_cast<int>(job->prepared.unreadable);
    passwords_ = job->passwords;
    records_ = job->records;
    join_.clear();
    if (job->failed.size()) Schedule(kRetryMs);  // Deliver what could not be applied.
  }
  Notify();
  if (again_) { again_ = false; Schedule(1000); }
}

void SyncService::Notify() { if (on_status) on_status(); }

CefRefPtr<CefDictionaryValue> SyncService::Status() const {
  auto status = CefDictionaryValue::Create();
  status->SetBool("configured", configured_);
  status->SetBool("running", running_);
  status->SetString("folder", Utf8(folder_));
  status->SetString("error", error_);
  status->SetString("join", join_);
  status->SetDouble("lastSync", static_cast<double>(last_success_ms_));
  status->SetInt("devices", devices_);
  status->SetInt("unreadable", unreadable_);
  status->SetInt("passwords", passwords_);
  auto detected = CefListValue::Create();
  const auto folders = DetectCloudFolders(GatherDetectionInputs());
  // Whether each cloud app runs: without it nothing reaches other PCs. Omitted
  // when the process list cannot be read.
  const auto running = platform::RunningProcessNames();
  for (const auto& folder : folders) {
    auto item = CefDictionaryValue::Create();
    item->SetString("provider", ProviderName(folder.provider));
    item->SetString("path", Utf8(folder.path));
    item->SetBool("hasVault", folder.available && HasVault(folder.path));
    item->SetBool("available", folder.available);
    if (running) item->SetBool("clientRunning", ClientRunning(folder.provider, *running));
    detected->SetDictionary(detected->GetSize(), item);
  }
  const auto provider = ProviderForFolder(folders, folder_);
  status->SetString("provider", provider ? ProviderName(*provider) : "");
  if (provider && running) status->SetBool("clientRunning", ClientRunning(*provider, *running));
  status->SetList("detected", detected);
  return status;
}

bool SyncService::HasVault(const fs::path& folder) const { return PeekVaultId(SyncDirectory(folder)).has_value(); }

void SyncService::SetUp(fs::path folder, std::string passphrase, JoinMode mode, Done done) {
  if (stopped_ || !worker_) { Wipe(passphrase); done("io"); return; }
  if (configured_ || running_) { Wipe(passphrase); done("busy"); return; }
  running_ = true;
  Notify();
  worker_->GetTaskRunner()->PostTask(CefCreateClosureTask(base::BindOnce(
      [](std::weak_ptr<SyncService> weak, std::shared_ptr<SyncWorkerState> worker, fs::path dir, fs::path folder, std::string passphrase,
         JoinMode mode, Done done) {
        worker->vault.Close();
        const auto status = sync::SetUp(dir, folder, passphrase, mode, worker->local, worker->vault);
        Wipe(passphrase);
        worker->loaded = status == SetupStatus::Ok;
        worker->load_error.clear();
        std::string error;
        switch (status) {
          case SetupStatus::Ok: break;
          case SetupStatus::WrongPassphrase: error = "wrongPassphrase"; break;
          case SetupStatus::WeakPassphrase: error = "weakPassphrase"; break;
          case SetupStatus::Corrupt: error = "corrupt"; break;
          default: error = "io";
        }
        CefPostTask(TID_UI, base::BindOnce([](std::weak_ptr<SyncService> weak, std::string error, fs::path folder, std::string device,
                                              std::string join, Done done) {
          const auto self = weak.lock();
          if (!self) { done("io"); return; }
          self->running_ = false;
          if (error.empty()) LOG(WARNING) << "Cloud sync: turned on (" << (join.empty() ? "new" : join) << ")";
          else LOG(WARNING) << "Cloud sync: turning on failed (" << error << ")";
          if (error.empty()) {
            self->configured_ = true; self->folder_ = folder; self->device_ = device; self->join_ = join; self->error_.clear();
            self->tree_hash_ = 0; self->poll_signature_.clear();
            self->Schedule(500);
          }
          self->Notify();
          done(error);
        }, weak, error, worker->local.folder, worker->local.device, worker->local.join, std::move(done)));
      }, weak_from_this(), worker_state_, dir_, std::move(folder), std::move(passphrase), mode, std::move(done))));
}

void SyncService::TurnOff(Done done) {
  if (stopped_ || !worker_) { done("io"); return; }
  if (running_) { done("busy"); return; }
  ++schedule_generation_;
  running_ = true;
  worker_->GetTaskRunner()->PostTask(CefCreateClosureTask(base::BindOnce(
      [](std::weak_ptr<SyncService> weak, std::shared_ptr<SyncWorkerState> worker, fs::path dir, Done done) {
        bool ok = true;
        if (worker->loaded) ok = sync::TurnOff(worker->local);
        else for (const char* name : {"base.sol", "vault-key.bin", "sync.json"}) { std::error_code error; fs::remove(dir / name, error); ok = ok && !error; }
        worker->vault.Close();
        worker->loaded = false;
        worker->local = LocalSync{};
        CefPostTask(TID_UI, base::BindOnce([](std::weak_ptr<SyncService> weak, bool ok, Done done) {
          const auto self = weak.lock();
          if (!self) { done("io"); return; }
          self->running_ = false;
          self->configured_ = false; self->folder_.clear(); self->device_.clear(); self->join_.clear();
          self->last_success_ms_ = 0; self->devices_ = self->unreadable_ = self->passwords_ = self->records_ = 0;
          self->error_ = ok ? "" : "io";
          LOG(WARNING) << (ok ? "Cloud sync: turned off" : "Cloud sync: turning off failed (io)");
          self->Notify();
          done(ok ? "" : "io");
        }, weak, ok, std::move(done)));
      }, weak_from_this(), worker_state_, dir_, std::move(done))));
}

void SyncService::SyncNow() { if (configured_) Schedule(0); }
}  // namespace red
