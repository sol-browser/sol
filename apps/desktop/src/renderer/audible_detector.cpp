#include "renderer/audible_detector.h"
#include <windows.h>
#include <map>
#include <string>
#include "include/cef_parser.h"
#include "include/cef_process_message.h"

namespace red {
namespace {
// Built-ins are captured at context creation, before any page script, so later
// page changes to globals cannot reach |report|. A page may still dispatch fake
// media events at its own elements: that only fakes its own tab's indicator.
constexpr char kDetector[] = R"JS((function (report) {
  'use strict';
  const Media = HTMLMediaElement, MediaPrototype = Media.prototype, nativePlay = MediaPrototype.play;
  const later = setTimeout, every = setInterval, stop = clearInterval, listen = EventTarget.prototype.addEventListener;
  const tracked = new Set(), events = ['play', 'playing', 'pause', 'ended', 'volumechange', 'emptied', 'ratechange'];
  let last = false, pending = false, poll = 0;
  const audible = m => !m.paused && !m.ended && !m.muted && m.volume > 0 && m.playbackRate !== 0;
  function update() {
    pending = false;
    let now = false;
    for (const m of tracked) { if (audible(m)) now = true; else if (m.paused || m.ended) tracked.delete(m); }
    if (!tracked.size && poll) { stop(poll); poll = 0; }
    if (now !== last) { last = now; report(now); }
  }
  function schedule() { if (!pending) { pending = true; later(update, 200); } }
  function track(m) {
    tracked.add(m);
    // Players can change state without events (stalls, programmatic volume).
    if (!poll) poll = every(update, 2000);
    schedule();
  }
  const onEvent = e => { if (e.target instanceof Media) track(e.target); };
  for (const type of events) listen.call(document, type, onEvent, true);
  // Elements outside the document (new Audio()) or in shadow trees never reach
  // the document listener; play() is the common entry point for both.
  MediaPrototype.play = function () {
    if (this instanceof Media && !this.isConnected) for (const type of events) listen.call(this, type, onEvent, true);
    if (this instanceof Media) track(this);
    return nativePlay.apply(this, arguments);
  };
}))JS";

std::map<std::string, bool> reported;  // Frame identifier -> last reported value.

void Send(CefRefPtr<CefFrame> frame, bool audible) {
  const auto id = frame->GetIdentifier().ToString();
  if (reported[id] == audible) return;
  reported[id] = audible;
  auto message = CefProcessMessage::Create("red.audible");
  message->GetArgumentList()->SetBool(0, audible);
  frame->SendProcessMessage(PID_BROWSER, message);
}

class Report final : public CefV8Handler {
 public:
  explicit Report(CefRefPtr<CefFrame> frame) : frame_(frame) {}
  bool Execute(const CefString&, CefRefPtr<CefV8Value>, const CefV8ValueList& arguments,
               CefRefPtr<CefV8Value>&, CefString&) override {
    if (arguments.size() != 1 || !arguments[0]->IsBool() || !frame_->IsValid()) return true;
    // At most a few state changes per second reach the browser process.
    const ULONGLONG now = GetTickCount64();
    if (now - window_start_ > 1000) { window_start_ = now; count_ = 0; }
    if (++count_ > 8) return true;
    Send(frame_, arguments[0]->GetBoolValue());
    return true;
  }
 private:
  CefRefPtr<CefFrame> frame_;
  ULONGLONG window_start_ = 0;
  int count_ = 0;
  IMPLEMENT_REFCOUNTING(Report);
};

bool WebsiteFrame(CefRefPtr<CefFrame> frame) {
  CefURLParts parts;
  if (!CefParseURL(frame->GetURL(), parts)) return false;
  const auto scheme = CefString(&parts.scheme).ToString();
  return scheme == "https" || scheme == "http";
}
}  // namespace

void InstallAudibleDetector(CefRefPtr<CefFrame> frame, CefRefPtr<CefV8Context> context) {
  if (!WebsiteFrame(frame) || !context->Enter()) return;
  CefRefPtr<CefV8Value> installer;
  CefRefPtr<CefV8Exception> exception;
  if (context->Eval(kDetector, CefString(), 0, installer, exception) && installer && installer->IsFunction()) {
    CefV8ValueList arguments{CefV8Value::CreateFunction("report", new Report(frame))};
    installer->ExecuteFunction(nullptr, arguments);
  }
  context->Exit();
}

void ReleaseAudibleDetector(CefRefPtr<CefFrame> frame) {
  const auto id = frame->GetIdentifier().ToString();
  const auto found = reported.find(id);
  if (found == reported.end()) return;
  if (found->second && frame->IsValid()) Send(frame, false);
  reported.erase(id);
}
}  // namespace red
