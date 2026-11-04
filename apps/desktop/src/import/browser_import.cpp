#include "import/browser_import.h"
#include "import/cookie_writer.h"
#include "import/password_sink.h"
#include "import/zen_reader.h"
#include "import/formats/chromium_profile.h"
#include "import/formats/favicons.h"
#include "history/favicon_cache.h"
#include "import/formats/firefox_logins.h"
#include "import/formats/import_tree.h"
#include "import/formats/password_csv.h"
#include "history/history_service.h"
#include "workspace/controller.h"
#include "include/cef_parser.h"
#include "include/cef_thread.h"
#include "include/base/cef_callback.h"
#include "include/wrapper/cef_closure_task.h"
#include <shobjidl.h>
#include <algorithm>
#include <chrono>
#include <cwctype>
#include <map>
#include <set>

namespace red {
namespace {
enum class Source { Zen, Chrome, Edge, Arc };
struct Candidate { Source source; std::filesystem::path path; std::string name; };
struct Types { bool tabs = false, bookmarks = false, history = false, cookies = false, passwords = false; };
constexpr int64_t kHistoryWindowMs = 90LL * 24 * 60 * 60 * 1000;

const char* Key(Source s) { return s == Source::Zen ? "zen" : s == Source::Chrome ? "chrome" : s == Source::Edge ? "edge" : "arc"; }
std::string Label(Source s) { return s == Source::Zen ? "Zen" : s == Source::Chrome ? "Chrome" : s == Source::Edge ? "Edge" : "Arc"; }
std::optional<Source> ParseSource(const std::string& key) {
  for (auto s : {Source::Zen, Source::Chrome, Source::Edge, Source::Arc}) if (key == Key(s)) return s;
  return std::nullopt;
}
std::string ExportHint(Source s) {
  switch (s) {
    case Source::Chrome: return "In Chrome open Settings \u2192 Passwords and autofill \u2192 Google Password Manager \u2192 Settings \u2192 Export passwords, then choose \u201cImport a password file\u201d here.";
    case Source::Edge: return "In Edge open Settings \u2192 Passwords \u2192 \u2026 \u2192 Export passwords, then choose \u201cImport a password file\u201d here.";
    case Source::Arc: return "In Arc open arc://password-manager/settings \u2192 Export passwords, then choose \u201cImport a password file\u201d here.";
    case Source::Zen: return "In Zen open about:logins \u2192 \u2026 \u2192 Export passwords, then choose \u201cImport a password file\u201d here.";
  }
  return "";
}
std::filesystem::path KnownFolder(const wchar_t* variable) {
  wchar_t value[32768]{}; return GetEnvironmentVariableW(variable, value, static_cast<DWORD>(std::size(value))) ? std::filesystem::path(value) : std::filesystem::path();
}
std::string Utf8(const std::wstring& w) { return CefString(w).ToString(); }
// Where each Chromium browser keeps its "User Data" folder. Arc ships as an
// MSIX package (Packages\TheBrowserCompany.Arc_<publisher id>\LocalCache\...);
// a non-packaged %LOCALAPPDATA%\Arc is checked too.
std::vector<std::filesystem::path> ChromiumRoots(Source source) {
  std::vector<std::filesystem::path> roots; const auto local = KnownFolder(L"LOCALAPPDATA"); if (local.empty()) return roots;
  if (source == Source::Chrome) roots.push_back(local / "Google" / "Chrome" / "User Data");
  else if (source == Source::Edge) roots.push_back(local / "Microsoft" / "Edge" / "User Data");
  else if (source == Source::Arc) {
    std::error_code ec;
    for (std::filesystem::directory_iterator it(local / "Packages", ec), end; !ec && it != end; it.increment(ec))
      if (it->path().filename().wstring().starts_with(L"TheBrowserCompany.Arc_")) roots.push_back(it->path() / "LocalCache" / "Local" / "Arc" / "User Data");
    roots.push_back(local / "Arc" / "User Data");
  }
  return roots;
}
// Profile folders plus the names the browser shows for them (Local State's
// profile list; nothing else is kept). Profile contents are read only after
// the person confirms.
std::vector<Candidate> ChromiumProfiles(Source source) {
  std::vector<Candidate> out; std::error_code ec;
  auto number = [](const std::filesystem::path& p) { const auto n = p.filename().wstring(); return n == L"Default" ? -1 : _wtoi(n.c_str() + std::min<size_t>(8, n.size())); };
  for (const auto& root : ChromiumRoots(source)) {
    std::vector<std::filesystem::path> dirs;
    for (std::filesystem::directory_iterator it(root, ec), end; !ec && it != end && dirs.size() < 50; it.increment(ec)) {
      const auto name = it->path().filename().wstring();
      if ((name == L"Default" || name.starts_with(L"Profile ")) && std::filesystem::is_regular_file(it->path() / "Preferences", ec)) dirs.push_back(it->path());
    }
    if (dirs.empty()) continue;
    std::string json; std::map<std::string, std::string> names;
    if (import::ReadSmallFile(root / "Local State", 16 * 1024 * 1024, json)) names = import::ChromiumProfileNames(json);
    import::Wipe(json);  // Also holds the wrapped password key.
    std::sort(dirs.begin(), dirs.end(), [&](const auto& a, const auto& b) { return number(a) < number(b); });
    std::map<std::string, int> seen; for (const auto& dir : dirs) { const auto n = names.find(Utf8(dir.filename().wstring())); if (n != names.end()) ++seen[n->second]; }
    for (const auto& dir : dirs) {
      const auto folder = Utf8(dir.filename().wstring()); const auto found = names.find(folder);
      std::string label = found != names.end() ? found->second : folder == "Default" ? "Default profile" : folder;
      if (found != names.end() && seen[found->second] > 1) label += " (" + folder + ")";  // Same display name twice.
      out.push_back({source, dir, label});
    }
  }
  return out;
}
// A picked Chromium folder may be "User Data" (Local State + Default) or a profile.
std::filesystem::path ChromiumProfileFolder(const std::filesystem::path& picked) {
  std::error_code ec;
  if (std::filesystem::is_regular_file(picked / "Local State", ec) && std::filesystem::is_directory(picked / "Default", ec)) return picked / "Default";
  return picked;
}
bool LooksLikeProfile(Source source, const std::filesystem::path& p) {
  std::error_code ec; auto has = [&](const char* name) { return std::filesystem::exists(p / name, ec); };
  if (source == Source::Zen) return has("prefs.js") || has("places.sqlite") || has("logins.json") || has("sessionstore.jsonlz4");
  const auto profile = ChromiumProfileFolder(p);
  return std::filesystem::exists(profile / "Preferences", ec) || std::filesystem::exists(profile / "Login Data", ec) || std::filesystem::exists(profile / "History", ec);
}
std::string Normalized(const std::filesystem::path& p) {
  std::error_code ec; auto w = std::filesystem::weakly_canonical(p, ec).wstring(); if (ec) w = p.wstring();
  std::transform(w.begin(), w.end(), w.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
  return Utf8(w);
}
int64_t NowMs() { return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count(); }

struct ImportData {
  std::string label;
  std::string source_key;  // e.g. "chrome"; "file" for user-picked exports.
  TreeSnapshot tree; int tabs = 0, bookmarks = 0, spaces = 0;
  std::vector<Visit> history;
  std::vector<ZenCookie> cookies; int skipped_cookies = 0;
  std::vector<import::Login> logins; import::PasswordCounts passwords;
  import::Favicons favicons;  // Host -> PNG for the imported tabs and bookmarks.
  std::vector<std::string> notes;
  bool needs_primary = false, wrong_primary = false;
  std::string error;
  ~ImportData() { import::WipeAll(logins); }
};
void AddLogins(ImportData& out, std::vector<import::Login>&& logins, const import::PasswordCounts& counts) {
  std::set<std::pair<std::string, std::string>> seen; for (const auto& l : out.logins) seen.insert({l.url, l.username});
  for (auto& l : logins) if (seen.insert({l.url, l.username}).second) out.logins.push_back(std::move(l));
  import::WipeAll(logins);
  out.passwords.found += counts.found; out.passwords.decrypted = static_cast<int>(out.logins.size());
  out.passwords.app_bound += counts.app_bound; out.passwords.unreadable += counts.unreadable; out.passwords.skipped += counts.skipped;
}
void UseTree(ImportData& out, import::TreeOutput tree) {
  if (!tree.error.empty()) { out.error = tree.error; return; }
  out.tabs = tree.tabs; out.bookmarks = tree.bookmarks;
  if (!tree.tree.nodes.empty()) { out.tree = std::move(tree.tree); out.spaces = static_cast<int>(out.tree.spaces.size()); }
}
// Hosts of the imported tabs/bookmarks (icons for other sites are not read).
std::set<std::string> TreeHosts(const TreeSnapshot& tree) {
  std::set<std::string> hosts;
  for (const auto& n : tree.nodes) if (!n.folder) if (auto host = import::UrlHost(n.url); !host.empty() && hosts.size() < 500) hosts.insert(std::move(host));
  return hosts;
}
std::string TypeKey(const Types& t) { return std::string(t.tabs ? "t" : "") + (t.bookmarks ? "b" : ""); }

// Runs on the reader thread. Source files are untrusted and opened read-only.
std::shared_ptr<ImportData> ReadSource(const Candidate& c, Types types, std::string primary) {
  auto out = std::make_shared<ImportData>(); out->label = Label(c.source); out->source_key = Key(c.source);
  if (c.source == Source::Zen) {
    if (types.tabs || types.history || types.cookies) {
      auto zen = ReadZenProfile(c.path);
      if (!zen.error.empty() && types.tabs) { out->error = zen.error; import::Wipe(primary); return out; }
      if (!zen.error.empty()) out->notes.push_back("Zen tabs and history: " + zen.error);
      if (types.tabs && zen.error.empty()) {
        out->tree = std::move(zen.tree); out->tabs = zen.tabs; out->bookmarks = zen.bookmarks; out->spaces = static_cast<int>(out->tree.spaces.size());
        out->favicons = import::ReadFirefoxFavicons(c.path / "favicons.sqlite", TreeHosts(out->tree));
      }
      if (types.history) out->history = std::move(zen.history);
      if (types.cookies) { out->cookies = std::move(zen.cookies); out->skipped_cookies = zen.skipped_cookies; }
    }
    if (types.passwords) {
      auto logins = import::ReadFirefoxLogins(c.path, primary);
      switch (logins.status) {
        case import::FirefoxStatus::PrimaryPasswordRequired: out->needs_primary = true; break;
        case import::FirefoxStatus::WrongPrimaryPassword: out->needs_primary = out->wrong_primary = true; break;
        case import::FirefoxStatus::Unsupported: case import::FirefoxStatus::Error: out->notes.push_back(logins.error + " " + ExportHint(c.source)); break;
        case import::FirefoxStatus::Ok: case import::FirefoxStatus::NoLogins: AddLogins(*out, std::move(logins.logins), logins.counts); break;
      }
    }
    import::Wipe(primary);
    return out;
  }
  import::Wipe(primary);
  const auto profile = ChromiumProfileFolder(c.path);
  import::TreeInput tree; tree.source = Normalized(profile) + "|" + TypeKey(types); tree.label = out->label;
  if (types.tabs) {
    auto session = import::ReadChromiumSession(profile);
    if (!session.error.empty()) out->notes.push_back("Open tabs: " + session.error);
    tree.tabs = std::move(session.tabs);
  }
  if (types.bookmarks) {
    std::string json; std::error_code ec;
    if (import::ReadSmallFile(profile / "Bookmarks", 64 * 1024 * 1024, json)) {
      auto marks = import::ParseChromiumBookmarks(json);
      if (!marks.error.empty()) out->notes.push_back("Bookmarks: " + marks.error); else tree.bookmarks = std::move(marks.items);
    } else if (std::filesystem::exists(profile / "Bookmarks", ec)) out->notes.push_back("Bookmarks could not be read");
  }
  if (types.tabs || types.bookmarks) UseTree(*out, import::BuildImportTree(tree));
  if (!out->error.empty()) return out;
  out->favicons = import::ReadChromiumFavicons(profile / "Favicons", TreeHosts(out->tree));
  if (types.history) {
    auto history = import::ReadChromiumHistory(profile / "History", NowMs() - kHistoryWindowMs, 50000);
    if (!history.error.empty()) out->notes.push_back("History: " + history.error);
    for (auto& v : history.visits) out->history.push_back({0, std::move(v.url), std::move(v.title), v.visited_ms});
  }
  if (types.passwords) {
    std::string state_json, key, error; bool app_bound = false;
    const auto local_state = std::filesystem::exists(profile.parent_path() / "Local State") ? profile.parent_path() / "Local State" : profile / "Local State";
    if (!import::ReadSmallFile(local_state, 16 * 1024 * 1024, state_json) || !import::ChromiumMasterKey(state_json, key, app_bound, error)) {
      out->notes.push_back("Passwords: " + (error.empty() ? std::string("this browser's Local State file was not found next to the profile.") : error) + " " + ExportHint(c.source));
    } else {
      for (const char* store : {"Login Data", "Login Data For Account"}) {
        auto logins = import::ReadChromiumLogins(profile / store, key);
        if (!logins.error.empty()) { out->notes.push_back("Passwords: " + logins.error); continue; }
        AddLogins(*out, std::move(logins.logins), logins.counts);
      }
      if (out->passwords.app_bound)
        out->notes.push_back(std::to_string(out->passwords.app_bound) + " passwords use " + out->label + "'s app-bound encryption, which only " + out->label +
                             " itself can unlock. " + ExportHint(c.source));
    }
    import::Wipe(key);
  }
  return out;
}

std::filesystem::path PickPath(HWND owner, bool folder, const wchar_t* title, const wchar_t* filter_name, const wchar_t* filter) {
  const HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  IFileOpenDialog* dialog = nullptr; std::filesystem::path path;
  if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) {
    DWORD flags = 0; dialog->GetOptions(&flags);
    dialog->SetOptions(flags | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST | (folder ? FOS_PICKFOLDERS : FOS_FILEMUSTEXIST));
    dialog->SetTitle(title);
    if (!folder) { const COMDLG_FILTERSPEC spec[] = {{filter_name, filter}}; dialog->SetFileTypes(1, spec); }
    if (SUCCEEDED(dialog->Show(owner))) {
      IShellItem* item = nullptr;
      if (SUCCEEDED(dialog->GetResult(&item))) { PWSTR value = nullptr; if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &value))) { path = value; CoTaskMemFree(value); } item->Release(); }
    }
    dialog->Release();
  }
  if (SUCCEEDED(init)) CoUninitialize();
  return path;
}
std::string Json(CefRefPtr<CefDictionaryValue> d) { auto v = CefValue::Create(); v->SetDictionary(d); return CefWriteJSON(v, JSON_WRITER_DEFAULT); }
CefRefPtr<CefDictionaryValue> Preview(const ImportData& d) {
  auto r = CefDictionaryValue::Create();
  r->SetString("label", d.label); r->SetInt("tabs", d.tabs); r->SetInt("bookmarks", d.bookmarks); r->SetInt("spaces", d.spaces);
  r->SetInt("history", static_cast<int>(d.history.size())); r->SetInt("cookies", static_cast<int>(d.cookies.size()));
  r->SetInt("favicons", static_cast<int>(d.favicons.size()));
  auto p = CefDictionaryValue::Create();
  p->SetInt("found", d.passwords.found); p->SetInt("ready", static_cast<int>(d.logins.size())); p->SetInt("appBound", d.passwords.app_bound);
  p->SetInt("unreadable", d.passwords.unreadable); p->SetInt("skipped", d.passwords.skipped);
  r->SetDictionary("passwords", p);
  auto notes = CefListValue::Create(); for (size_t i = 0; i < d.notes.size() && i < 12; ++i) notes->SetString(i, d.notes[i]);
  r->SetList("notes", notes);
  r->SetBool("needsPrimaryPassword", d.needs_primary); r->SetBool("wrongPrimaryPassword", d.wrong_primary);
  return r;
}
}  // namespace

class BrowserImport : public std::enable_shared_from_this<BrowserImport> {
 public:
  ~BrowserImport() { pending.reset(); if (worker) worker->Stop(); }
  std::vector<Candidate> candidates;
  std::shared_ptr<ImportData> pending;
  CefRefPtr<CefThread> worker;
  bool busy = false;
  int generation = 0;  // Invalidates reads that finish after a cancel.

  // Reads on a worker thread, then hands the result to `done` on the UI thread.
  void Read(std::function<std::shared_ptr<ImportData>()> read, std::function<void(std::shared_ptr<ImportData>)> done) {
    if (!worker) worker = CefThread::CreateThread("Browser profile reader");
    if (!worker) { done(nullptr); return; }
    busy = true; const int ticket = ++generation;
    worker->GetTaskRunner()->PostTask(CefCreateClosureTask(base::BindOnce([](std::weak_ptr<BrowserImport> weak, int ticket,
        std::function<std::shared_ptr<ImportData>()> read, std::function<void(std::shared_ptr<ImportData>)> done) {
      auto data = read();
      CefPostTask(TID_UI, base::BindOnce([](std::weak_ptr<BrowserImport> weak, int ticket, std::shared_ptr<ImportData> data, std::function<void(std::shared_ptr<ImportData>)> done) {
        auto self = weak.lock(); if (!self) return;
        self->busy = false;
        if (ticket != self->generation) { done(nullptr); return; }
        done(std::move(data));
      }, weak, ticket, std::move(data), std::move(done)));
    }, weak_from_this(), ticket, std::move(read), std::move(done))));
  }
  // Writes the confirmed preview into Red: workspace, history, cookies, passwords.
  void Commit(std::weak_ptr<State> weak_state, std::function<void(CefRefPtr<CefDictionaryValue>)> done) {
    auto data = std::move(pending); busy = true;
    auto report = CefDictionaryValue::Create(); auto errors = CefListValue::Create();
    report->SetString("label", data->label);
    auto self = shared_from_this();
    auto finish = [self, report, errors, done] { self->busy = false; report->SetList("errors", errors); done(report); };
    auto error = [errors](const std::string& text) { errors->SetString(errors->GetSize(), text); };
    auto passwords = [self, weak_state, data, report, error, finish] {
      auto p = CefDictionaryValue::Create(); p->SetInt("added", 0); p->SetInt("existing", 0); p->SetInt("failed", 0); report->SetDictionary("passwords", p);
      if (data->logins.empty()) { finish(); return; }
      SavePasswords(std::move(data->logins), [report, error, finish, weak_state](PasswordSinkResult r) {
        auto p = CefDictionaryValue::Create(); p->SetInt("added", r.added); p->SetInt("existing", r.existing); p->SetInt("failed", r.failed);
        report->SetDictionary("passwords", p);
        if (!r.error.empty()) error("Passwords: " + r.error);
        if (auto s = weak_state.lock()) {
          auto record = CefDictionaryValue::Create(); record->SetInt("added", r.added); record->SetInt("existing", r.existing); record->SetInt("failed", r.failed);
          record->SetBool("store_error", !r.error.empty()); s->Record("browser_import_passwords", record);
        }
        finish();
      });
    };
    auto cookies = [data, report, passwords] {
      if (data->cookies.empty()) { passwords(); return; }
      WriteCookies(std::move(data->cookies), [report, passwords](CookieWriteResult r) {
        auto c = CefDictionaryValue::Create(); c->SetInt("written", r.written); c->SetInt("kept", r.kept); c->SetInt("failed", r.failed);
        report->SetDictionary("cookies", c); passwords();
      });
    };
    auto history = [weak_state, data, report, error, cookies] {
      auto s = weak_state.lock();
      if (data->history.empty()) { cookies(); return; }
      if (!s || !s->history) { error("History could not be imported"); cookies(); return; }
      s->history->ImportVisits(std::move(data->history), [report, cookies](int count) { report->SetInt("history", count); cookies(); });
    };
    auto s = weak_state.lock();
    if (s && s->favicons) {
      int added = 0;
      for (auto& [host, png] : data->favicons) {
        const auto key = FaviconHost(host);
        if (key.empty() || s->favicons->Png(key)) continue;  // Red's own icon wins.
        s->favicons->Put(key, png);
        if (s->history && !s->private_window) s->history->SaveFavicon(key, png);
        ++added;
      }
      report->SetInt("favicons", added);
    }
    if (data->tree.nodes.empty()) { history(); return; }
    if (!s || !s->workspace) { error("Tabs and bookmarks could not be imported"); history(); return; }
    s->workspace->ImportZen(data->tree, [data, report, error, history](int code, const std::string& message, CefRefPtr<CefDictionaryValue>) {
      if (code) error(message);
      else { report->SetInt("tabs", data->tabs); report->SetInt("bookmarks", data->bookmarks); report->SetInt("spaces", data->spaces); }
      history();
    });
  }
};

void CancelBrowserImport(std::shared_ptr<BrowserImport>& session) {
  if (!session) return;
  session->pending.reset(); ++session->generation;
}

bool HandleImportQuery(const std::shared_ptr<State>& state, HWND owner, std::shared_ptr<BrowserImport>& session,
                       const std::string& method, CefRefPtr<CefDictionaryValue> data, ImportCallback callback) {
  if (!method.starts_with("import.")) return false;
  if (!session) session = std::make_shared<BrowserImport>();
  if (state->standalone_window) { callback->Failure(403, "Import is available in the main window"); return true; }
  if (method == "import.cancel") { CancelBrowserImport(session); callback->Success("{}"); return true; }
  if (session->busy) { callback->Failure(409, "An import is already in progress"); return true; }
  auto source_of = [&]() -> std::optional<Source> { return data->GetType("source") == VTYPE_STRING ? ParseSource(data->GetString("source")) : std::nullopt; };
  auto add_candidate = [&](Candidate c) {
    for (size_t i = 0; i < session->candidates.size(); ++i) if (session->candidates[i].source == c.source && session->candidates[i].path == c.path) return static_cast<int>(i);
    session->candidates.push_back(std::move(c)); return static_cast<int>(session->candidates.size() - 1);
  };
  if (method == "import.sources" && data->GetSize() == 1) {
    session->candidates.clear(); CancelBrowserImport(session);
    for (auto& z : DiscoverZenProfiles()) add_candidate({Source::Zen, z.path, Utf8(z.name)});
    for (auto& c : ChromiumProfiles(Source::Chrome)) add_candidate(c);
    for (auto& c : ChromiumProfiles(Source::Edge)) add_candidate(c);
    for (auto& c : ChromiumProfiles(Source::Arc)) add_candidate(c);
    auto result = CefDictionaryValue::Create(); auto list = CefListValue::Create();
    for (auto s : {Source::Zen, Source::Chrome, Source::Edge, Source::Arc}) {
      auto item = CefDictionaryValue::Create(); item->SetString("id", Key(s)); item->SetString("name", Label(s));
      auto profiles = CefListValue::Create();
      for (size_t i = 0; i < session->candidates.size(); ++i) if (session->candidates[i].source == s) {
        auto p = CefDictionaryValue::Create(); p->SetString("id", std::to_string(i)); p->SetString("name", session->candidates[i].name); profiles->SetDictionary(profiles->GetSize(), p);
      }
      item->SetList("profiles", profiles); list->SetDictionary(list->GetSize(), item);
    }
    result->SetList("sources", list); callback->Success(Json(result)); return true;
  }
  if (method == "import.chooseFolder" && data->GetSize() == 2) {
    const auto source = source_of(); if (!source) { callback->Failure(400, "Unknown browser"); return true; }
    // Leave the query callback before the modal picker runs its own message loop.
    CefPostTask(TID_UI, base::BindOnce([](std::weak_ptr<BrowserImport> weak, HWND owner, Source source, ImportCallback callback) {
      const auto title = L"Choose your " + CefString(Label(source)).ToWString() + (source == Source::Zen ? L" profile folder" : L" profile or User Data folder");
      const auto path = PickPath(owner, true, title.c_str(), nullptr, nullptr);
      auto self = weak.lock(); if (!self) { callback->Failure(410, "Settings closed"); return; }
      if (path.empty()) { callback->Success("{}"); return; }
      if (!LooksLikeProfile(source, path)) { callback->Failure(400, "This folder doesn't look like a " + Label(source) + " profile"); return; }
      Candidate c{source, path, Utf8(path.filename().wstring())};
      int id = -1;
      for (size_t i = 0; i < self->candidates.size(); ++i) if (self->candidates[i].source == source && self->candidates[i].path == path) id = static_cast<int>(i);
      if (id < 0) { self->candidates.push_back(c); id = static_cast<int>(self->candidates.size() - 1); }
      auto result = CefDictionaryValue::Create(); auto p = CefDictionaryValue::Create();
      p->SetString("id", std::to_string(id)); p->SetString("name", c.name); result->SetDictionary("profile", p);
      callback->Success(Json(result));
    }, std::weak_ptr<BrowserImport>(session), owner, *source, callback));
    return true;
  }
  if (method == "import.read" && data->GetType("profile") == VTYPE_STRING && data->GetType("types") == VTYPE_DICTIONARY) {
    const auto id = data->GetString("profile").ToString();
    if (id.empty() || id.size() > 3 || id.find_first_not_of("0123456789") != std::string::npos || std::stoul(id) >= session->candidates.size()) {
      callback->Failure(400, "Choose a profile first"); return true;
    }
    const auto candidate = session->candidates[std::stoul(id)];
    const auto t = data->GetDictionary("types"); Types types;
    auto flag = [&](const char* key) { return t->GetType(key) == VTYPE_BOOL && t->GetBool(key); };
    types.tabs = flag("tabs"); types.bookmarks = flag("bookmarks"); types.history = flag("history"); types.passwords = flag("passwords");
    types.cookies = candidate.source == Source::Zen && flag("cookies");
    if (!types.tabs && !types.bookmarks && !types.history && !types.cookies && !types.passwords) { callback->Failure(400, "Choose what to import"); return true; }
    std::string primary = data->GetType("primaryPassword") == VTYPE_STRING ? data->GetString("primaryPassword").ToString() : "";
    if (primary.size() > 1024) { import::Wipe(primary); callback->Failure(400, "Invalid Primary Password"); return true; }
    CancelBrowserImport(session);
    state->Record("browser_import_read", [&] { auto d = CefDictionaryValue::Create(); d->SetString("source", Key(candidate.source)); return d; }());
    session->Read([candidate, types, primary]() mutable { auto data = ReadSource(candidate, types, std::move(primary)); import::Wipe(primary); return data; },
        [weak = std::weak_ptr<BrowserImport>(session), callback](std::shared_ptr<ImportData> data) {
      auto self = weak.lock();
      if (!self || !data) { callback->Failure(410, "Import cancelled"); return; }
      if (!data->error.empty()) { callback->Failure(422, data->error); return; }
      const auto preview = Preview(*data);
      if (!data->needs_primary && (data->tabs || data->bookmarks || !data->history.empty() || !data->cookies.empty() || !data->logins.empty())) self->pending = data;
      callback->Success(Json(preview));
    });
    return true;
  }
  if (method == "import.chooseFile" && data->GetType("kind") == VTYPE_STRING) {
    const auto kind = data->GetString("kind").ToString(); const auto source = source_of();
    if ((kind != "passwords" && kind != "bookmarks") || !source) { callback->Failure(400, "Invalid file import"); return true; }
    CancelBrowserImport(session);
    CefPostTask(TID_UI, base::BindOnce([](std::weak_ptr<BrowserImport> weak, HWND owner, Source source, std::string kind, ImportCallback callback) {
      const bool passwords = kind == "passwords";
      const auto path = PickPath(owner, false, passwords ? L"Choose an exported password file" : L"Choose an exported bookmarks file",
                                 passwords ? L"Password export (*.csv)" : L"Bookmarks (*.html; *.htm)", passwords ? L"*.csv" : L"*.html;*.htm");
      auto self = weak.lock(); if (!self) { callback->Failure(410, "Settings closed"); return; }
      if (path.empty()) { callback->Success("{}"); return; }
      if (self->busy) { callback->Failure(409, "An import is already in progress"); return; }
      self->Read([path, passwords, source] {
        auto out = std::make_shared<ImportData>(); out->label = Label(source); out->source_key = "file";
        std::string text;
        if (!import::ReadSmallFile(path, 32 * 1024 * 1024, text)) { out->error = "The file could not be read (it must be smaller than 32 MB)"; return out; }
        if (passwords) {
          auto csv = import::ParsePasswordCsv(text); import::Wipe(text);
          if (!csv.error.empty()) { out->error = csv.error; return out; }
          AddLogins(*out, std::move(csv.logins), csv.counts);
        } else {
          auto marks = import::ParseBookmarksHtml(text);
          if (!marks.error.empty()) { out->error = marks.error; return out; }
          import::TreeInput tree; tree.source = Normalized(path) + "|html"; tree.label = Label(source); tree.bookmarks = std::move(marks.items);
          UseTree(*out, import::BuildImportTree(tree));
        }
        return out;
      }, [weak, callback](std::shared_ptr<ImportData> data) {
        auto self = weak.lock();
        if (!self || !data) { callback->Failure(410, "Import cancelled"); return; }
        if (!data->error.empty()) { callback->Failure(422, data->error); return; }
        self->pending = data; callback->Success(Json(Preview(*data)));
      });
    }, std::weak_ptr<BrowserImport>(session), owner, *source, kind, callback));
    return true;
  }
  if (method == "import.commit" && data->GetSize() == 1) {
    if (!session->pending) { callback->Failure(409, "Nothing to import. Read a profile first."); return true; }
    session->Commit(state, [callback, weak_state = std::weak_ptr<State>(state)](CefRefPtr<CefDictionaryValue> report) {
      if (auto s = weak_state.lock()) {
        auto record = CefDictionaryValue::Create();
        for (const char* key : {"tabs", "bookmarks", "history"}) if (report->GetType(key) == VTYPE_INT) record->SetInt(key, report->GetInt(key));
        record->SetInt("errors", static_cast<int>(report->GetList("errors")->GetSize()));
        s->Record("browser_import_completed", record);
      }
      callback->Success(Json(report));
    });
    return true;
  }
  callback->Failure(400, "Unsupported import request"); return true;
}

void RunImportProbe(const std::shared_ptr<State>& state) {
  // <source>|<path>|<types>, e.g. chrome|C:\fixture\Default|tabs,bookmarks,history,passwords
  const auto spec = state->import_probe; const auto a = spec.find('|'), b = spec.rfind('|');
  auto fail = [&](const char* why) { auto d = CefDictionaryValue::Create(); d->SetString("error", why); state->Record("browser_import_probe", d); state->import_done = true; };
  if (a == std::string::npos || a == b) { fail("bad spec"); return; }
  const auto kind = spec.substr(0, a); const std::filesystem::path path = CefString(spec.substr(a + 1, b - a - 1)).ToWString(); const auto list = spec.substr(b + 1);
  Types types; types.tabs = list.find("tabs") != std::string::npos; types.bookmarks = list.find("bookmarks") != std::string::npos;
  types.history = list.find("history") != std::string::npos; types.passwords = list.find("passwords") != std::string::npos; types.cookies = list.find("cookies") != std::string::npos;
  auto session = std::make_shared<BrowserImport>();
  std::function<std::shared_ptr<ImportData>()> read;
  if (kind == "csv") read = [path] {
    auto out = std::make_shared<ImportData>(); out->label = "CSV"; std::string text;
    if (!import::ReadSmallFile(path, 32 * 1024 * 1024, text)) { out->error = "unreadable"; return out; }
    auto csv = import::ParsePasswordCsv(text); import::Wipe(text); out->error = csv.error; AddLogins(*out, std::move(csv.logins), csv.counts); return out;
  };
  else if (const auto source = ParseSource(kind)) read = [c = Candidate{*source, path, "probe"}, types] { return ReadSource(c, types, ""); };
  else { fail("bad source"); return; }
  std::weak_ptr<State> weak = state;
  session->Read(read, [session, weak](std::shared_ptr<ImportData> data) {
    auto s = weak.lock(); if (!s) return;
    auto d = CefDictionaryValue::Create();
    if (!data || !data->error.empty()) { d->SetString("error", data ? data->error : "cancelled"); s->Record("browser_import_probe", d); s->import_done = true; return; }
    d->SetDictionary("preview", Preview(*data));
    session->pending = data;
    session->Commit(s, [session, weak, d](CefRefPtr<CefDictionaryValue> report) {
      auto s = weak.lock(); if (!s) return;
      d->SetDictionary("report", report); s->Record("browser_import_probe", d); s->import_done = true;
    });
  });
}
}  // namespace red
