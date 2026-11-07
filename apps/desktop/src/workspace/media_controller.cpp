#include "workspace/controller.h"
#include "windows/auto_pip.h"
#include "include/cef_devtools_message_observer.h"
#include "include/cef_parser.h"
#include "include/cef_image.h"
#include "include/base/cef_callback.h"
#include "include/wrapper/cef_closure_task.h"
#include <algorithm>
#include <cmath>

namespace red {
namespace {
// The page's main media element: playing first, then the longest. Read-only
// summary plus the Media Session metadata the page publishes (the title and
// artist Windows' own media overlay shows).
constexpr char kPick[] =
    "const pick=()=>[...document.querySelectorAll('video,audio')].filter(m=>m.currentSrc||m.srcObject)"
    ".sort((a,b)=>(a.paused-b.paused)||((b.duration||0)-(a.duration||0)))[0];";
constexpr char kRead[] =
    "(()=>{const m=pick();const d=navigator.mediaSession&&navigator.mediaSession.metadata;"
    "return {has:!!m,paused:m?m.paused:true,time:m?m.currentTime:0,"
    "duration:m&&isFinite(m.duration)?m.duration:0,video:!!(m&&m.tagName==='VIDEO'&&m.videoWidth>0),"
    "title:String(d&&d.title||'').slice(0,240),artist:String(d&&d.artist||'').slice(0,240)};})()";
// Players shown at once (a stack in the sidebar).
constexpr size_t kMaxPlayers = 3;

class MediaObserver final : public CefDevToolsMessageObserver {
 public:
  explicit MediaObserver(std::weak_ptr<WorkspaceController> owner) : owner_(std::move(owner)) {}
  void OnDevToolsMethodResult(CefRefPtr<CefBrowser> browser, int id, bool success, const void* data, size_t size) override {
    std::string result; if (success && size && size <= 65536) result.assign(static_cast<const char*>(data), size);
    CefPostTask(TID_UI, base::BindOnce([](std::weak_ptr<WorkspaceController> weak, int browser, int id, std::string json) {
      if (const auto self = weak.lock()) self->OnMediaResult(browser, id, json);
    }, owner_, browser->GetIdentifier(), id, std::move(result)));
  }
 private:
  std::weak_ptr<WorkspaceController> owner_;
  IMPLEMENT_REFCOUNTING(MediaObserver);
};
// The session's artwork, downscaled by Chromium to at most 128 px, as a PNG data URL for the UI
// (its CSP allows data: images only).
class ArtworkDone final : public CefDownloadImageCallback {
 public:
  ArtworkDone(std::weak_ptr<WorkspaceController> owner, int browser, std::string url)
      : owner_(std::move(owner)), browser_(browser), url_(std::move(url)) {}
  void OnDownloadImageFinished(const CefString&, int, CefRefPtr<CefImage> image) override {
    std::string png;
    if (image && !image->IsEmpty()) {
      int width = 0, height = 0;
      if (const auto data = image->GetAsPNG(1.0f, true, width, height); data && data->GetSize() && data->GetSize() <= 96 * 1024) {
        png.resize(data->GetSize()); data->GetData(png.data(), png.size(), 0);
      }
    }
    if (const auto self = owner_.lock()) self->OnMediaArtwork(browser_, url_, png);
  }
 private:
  std::weak_ptr<WorkspaceController> owner_;
  int browser_;
  std::string url_;
  IMPLEMENT_REFCOUNTING(ArtworkDone);
};
}  // namespace

void WorkspaceController::OnMediaSession(CefRefPtr<CefBrowser> browser, CefRefPtr<CefDictionaryValue> info) {
  if (!browser || shutting_down_) return;
  const int id = browser->GetIdentifier();
  auto& session = media_sessions_[id];
  const auto actions = info ? info->GetList("actions") : nullptr;
  session.previous = session.next = false;
  for (size_t i = 0; actions && i < actions->GetSize(); ++i) {
    const auto action = actions->GetString(i).ToString();
    session.previous = session.previous || action == "previoustrack";
    session.next = session.next || action == "nexttrack";
  }
  const auto url = info ? info->GetString("artwork").ToString() : "";
  const bool web = (url.starts_with("https://") || url.starts_with("http://")) && url.size() <= 4096;
  if (url != session.artwork_url) {
    session.artwork_url = url; session.artwork.clear();
    if (web) browser->GetHost()->DownloadImage(url, false, 128, false, new ArtworkDone(weak_from_this(), id, url));
  }
  Publish();
}
void WorkspaceController::OnMediaArtwork(int browser, const std::string& url, const std::string& png) {
  const auto found = media_sessions_.find(browser);
  if (found == media_sessions_.end() || found->second.artwork_url != url) return;
  found->second.artwork = png.empty() ? "" : "data:image/png;base64," + CefBase64Encode(png.data(), png.size()).ToString();
  Publish();
}

// Arc/Zen-style sidebar players: the tabs that played sound most recently
// (up to three, newest first), shown while not on screen. Each keeps its
// player while its page still has media (paused counts) until dismissed.
// Their state is polled once a second while any player exists.
void WorkspaceController::UpdateMedia() {
  const auto state = state_.lock(); if (!state || shutting_down_) return;
  const auto now = GetTickCount64();
  std::set<std::string> audible;
  for (const auto& [browser, frames] : state->audible_frames)
    if (const auto node = browser_nodes_.find(browser); node != browser_nodes_.end()) {
      audible.insert(node->second);
      // A dismissed player returns only when its tab starts playing again
      // after silence (it is still audible for a moment after dismissing).
      if (media_dismissed_.contains(node->second)) {
        if (media_audible_.contains(node->second)) continue;
        media_dismissed_.erase(node->second);
      }
      // Order is by when a tab started playing, so pausing never reshuffles.
      if (!media_heard_.contains(node->second)) media_started_[node->second] = now;
      media_heard_[node->second] = now;
    }
  media_audible_ = std::move(audible);
  std::erase_if(media_heard_, [&](const auto& entry) {
    return !sessions_.contains(entry.first) || IsPeekPage(entry.first) || media_dismissed_.contains(entry.first);
  });
  std::erase_if(media_started_, [&](const auto& entry) { return !media_heard_.contains(entry.first); });
  std::erase_if(media_, [&](const auto& entry) {
    return !media_heard_.contains(entry.first) || (entry.second.polled && !entry.second.has);
  });
  std::vector<std::pair<unsigned long long, std::string>> order;
  for (const auto& [node, tick] : media_heard_) order.emplace_back(media_started_[node], node);
  std::sort(order.rbegin(), order.rend());
  media_nodes_.clear();
  for (const auto& [tick, node] : order) {
    if (media_nodes_.size() == kMaxPlayers) break;
    // A tab whose page no longer has media drops out until it plays again.
    if (const auto known = media_.find(node); known == media_.end() || !known->second.polled || known->second.has) media_nodes_.push_back(node);
  }
  for (const auto& node : media_nodes_) media_.try_emplace(node);
  std::erase_if(media_, [&](const auto& entry) { return std::find(media_nodes_.begin(), media_nodes_.end(), entry.first) == media_nodes_.end(); });
  if (!media_nodes_.empty() && !media_polling_) { media_polling_ = true; PollMedia(); }
}
void WorkspaceController::PollMedia() {
  if (media_nodes_.empty() || shutting_down_) { media_polling_ = false; return; }
  for (const auto& node : media_nodes_) {
    const auto browser = LiveBrowser(node); auto& entry = media_[node];
    if (!browser) continue;
    if (entry.browser != browser->GetIdentifier()) {
      entry.observer = browser->GetHost()->AddDevToolsMessageObserver(new MediaObserver(weak_from_this()));
      entry.browser = browser->GetIdentifier();
    }
    auto params = CefDictionaryValue::Create();
    params->SetString("expression", std::string("(()=>{") + kPick + "return " + kRead + ";})()");
    params->SetBool("returnByValue", true);
    entry.query = browser->GetHost()->ExecuteDevToolsMethod(0, "Runtime.evaluate", params);
  }
  CefPostDelayedTask(TID_UI, base::BindOnce([](std::weak_ptr<WorkspaceController> weak) {
    if (const auto self = weak.lock()) self->PollMedia();
  }, weak_from_this()), 1000);
}
void WorkspaceController::OnMediaResult(int browser, int id, const std::string& json) {
  const auto found = std::find_if(media_.begin(), media_.end(), [&](const auto& entry) { return entry.second.browser == browser && entry.second.query == id; });
  if (found == media_.end() || json.empty()) return;
  const auto parsed = CefParseJSON(json, JSON_PARSER_RFC);
  if (!parsed || parsed->GetType() != VTYPE_DICTIONARY) return;
  const auto outer = parsed->GetDictionary()->GetDictionary("result");
  const auto value = outer ? outer->GetDictionary("value") : nullptr;
  if (!value) return;
  // JSON whole numbers (a 20 s duration, a seek to 3 s) arrive as integers.
  const auto number = [&](const char* key) {
    const double v = value->GetType(key) == VTYPE_INT ? value->GetInt(key) : value->GetDouble(key);
    return std::isfinite(v) && v >= 0 ? std::min(v, 1e7) : 0.;
  };
  auto& media = found->second;
  MediaState next;
  next.has = value->GetBool("has"); next.paused = value->GetBool("paused"); next.video = value->GetBool("video");
  next.time = number("time"); next.duration = number("duration");
  next.title = value->GetString("title").ToString().substr(0, 240); next.artist = value->GetString("artist").ToString().substr(0, 240);
  next.polled = GetTickCount64(); next.observer = media.observer; next.browser = media.browser; next.query = media.query;
  const bool changed = next.has != media.has || next.paused != media.paused || next.title != media.title || next.artist != media.artist ||
      next.video != media.video || std::abs(next.duration - media.duration) > .5 || std::abs(next.time - media.time) > 1.5 || !media.polled;
  media = next;
  if (!media.has) UpdateMedia();
  if (changed) Publish();
}
CefRefPtr<CefListValue> WorkspaceController::DescribeMedia() const {
  const auto state = state_.lock(); if (!state) return nullptr;
  auto list = CefListValue::Create();
  for (const auto& node : media_nodes_) {
    const auto entry = media_.find(node); const auto browser = LiveBrowser(node);
    if (entry == media_.end() || !browser || !entry->second.polled || !entry->second.has) continue;
    const auto& media = entry->second;
    // Shown only while its tab is not on screen (as in Arc).
    if (IsWindowVisible(browser->GetHost()->GetWindowHandle()) && state->window && !state->window->IsMinimized()) continue;
    auto result = CefDictionaryValue::Create();
    const auto title = page_titles_.find(node);
    result->SetString("id", node);
    result->SetString("title", !media.title.empty() ? media.title : title != page_titles_.end() ? title->second.text : "");
    result->SetString("artist", media.artist);
    result->SetBool("playing", !media.paused); result->SetBool("video", media.video);
    result->SetBool("muted", browser->GetHost()->IsAudioMuted());
    result->SetDouble("time", media.time); result->SetDouble("duration", media.duration);
    if (const auto session = media_sessions_.find(browser->GetIdentifier()); session != media_sessions_.end()) {
      result->SetBool("previous", session->second.previous); result->SetBool("next", session->second.next);
      if (!session->second.artwork.empty()) result->SetString("artwork", session->second.artwork);
    }
    list->SetDictionary(list->GetSize(), result);
  }
  return list->GetSize() ? list : nullptr;
}
bool WorkspaceController::HandleMedia(CefRefPtr<CefDictionaryValue> request, Reply done) {
  if (request->GetString("method") != "media.control") return false;
  const auto action = request->GetString("action").ToString();
  const auto id = request->GetType("id") == VTYPE_STRING ? request->GetString("id").ToString() : "";
  const auto browser = LiveBrowser(id);
  if (std::find(media_nodes_.begin(), media_nodes_.end(), id) == media_nodes_.end() || !browser) {
    done(409, "Nothing is playing", nullptr); return true;
  }
  const auto run = [&](const std::string& body, bool gesture) {
    auto params = CefDictionaryValue::Create();
    params->SetString("expression", std::string("(()=>{") + kPick + "const m=pick();if(!m)return;" + body + "})()");
    params->SetBool("userGesture", gesture);
    browser->GetHost()->ExecuteDevToolsMethod(0, "Runtime.evaluate", params);
  };
  if (action == "toggle") run("m.paused?m.play().catch(()=>{}):m.pause();", true);
  else if (action == "seek") {
    const auto type = request->GetType("value");
    const double to = type == VTYPE_DOUBLE ? request->GetDouble("value") : type == VTYPE_INT ? request->GetInt("value") : -1;
    if (!std::isfinite(to) || to < 0 || to > 1e7) { done(400, "Invalid position", nullptr); return true; }
    run("m.currentTime=" + std::to_string(to) + ";", false);
  } else if (action == "mute") browser->GetHost()->SetAudioMuted(!browser->GetHost()->IsAudioMuted());
  else if (action == "dismiss") { run("m.pause();", false); media_dismissed_.insert(id); }
  else if (action == "pip") TogglePictureInPicture(browser);
#if CEF_API_ADDED(CEF_EXPERIMENTAL)
  // The page's own previous/next handlers, as Chrome's media controls press them (engine patch 09).
  else if (action == "previous" || action == "next") browser->GetHost()->MediaSessionAction(action == "previous" ? "previoustrack" : "nexttrack");
#endif
  else if (action == "open") { TreeCommand command{TreeAction::Activate}; command.id = id; Dispatch(command, {}, true); }
  else { done(400, "Unknown media action", nullptr); return true; }
  UpdateMedia(); Publish(); done(0, "", Snapshot()); return true;
}
}  // namespace red
