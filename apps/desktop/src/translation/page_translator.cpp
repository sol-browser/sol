#include "translation/page_translator.h"

#include <algorithm>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <utility>
#include <vector>

#include "app/session.h"
#include "interface_strings.h"
#include "include/base/cef_callback.h"
#include "include/cef_devtools_message_observer.h"
#include "include/cef_parser.h"
#include "include/wrapper/cef_closure_task.h"
#include "translation/model_catalog.h"
#include "translation/model_store.h"
#include "translation/page_translate_script.h"
#include "translation/page_translation_model.h"
#include "translation/translator_host.h"
#include "site/site_settings.h"
#include "windows/embedded_content.h"
#include "web_url.h"
#include "workspace/controller.h"

namespace red {
namespace {

constexpr size_t kMaxReply = 4 * 1024 * 1024;
constexpr int kEvaluateTimeoutMs = 20000;
constexpr int kPollMs = 2000;           // New text the page adds after translation.
constexpr int kBatchUnits = 40, kBatchChars = 16000;
constexpr size_t kMaxUnitHtml = 24000;
// Below this much text (bytes of UTF-8, about 300 Latin letters) a page's minor language gets no model download.
constexpr size_t kMinorLanguageChars = 300;

std::string& Target() {
  static std::string target = "en";
  return target;
}

std::string Json(CefRefPtr<CefValue> value) { return CefWriteJSON(value, JSON_WRITER_DEFAULT).ToString(); }

std::wstring Names(const std::set<std::string>& codes) {
  std::wstring out;
  for (const auto& code : codes) out += (out.empty() ? L"" : L", ") + LanguageDisplayName(code);
  return out;
}

// Runtime.evaluate on one website page, in the isolated world the job made.
class PageCalls final : public CefDevToolsMessageObserver {
 public:
  using Reply = std::function<void(CefRefPtr<CefValue>)>;
  explicit PageCalls(CefRefPtr<CefBrowser> page) : page_(page) { registration_ = page->GetHost()->AddDevToolsMessageObserver(this); }
  void Detach() { registration_ = nullptr; page_ = nullptr; auto pending = std::move(pending_); pending_.clear(); for (auto& [id, reply] : pending) reply(nullptr); }
  // Creates the "sol-translate" world in the main frame; |done| gets its context id or 0.
  void World(std::function<void(int)> done) {
    Send("Page.getFrameTree", nullptr, [this, done](CefRefPtr<CefValue> reply) {
      const auto tree = reply && reply->GetType() == VTYPE_DICTIONARY ? reply->GetDictionary()->GetDictionary("frameTree") : nullptr;
      const auto frame = tree ? tree->GetDictionary("frame") : nullptr;
      if (!frame || frame->GetType("id") != VTYPE_STRING) { done(0); return; }
      auto params = CefDictionaryValue::Create();
      params->SetString("frameId", frame->GetString("id")); params->SetString("worldName", "sol-translate");
      Send("Page.createIsolatedWorld", params, [done](CefRefPtr<CefValue> reply) {
        const auto object = reply && reply->GetType() == VTYPE_DICTIONARY ? reply->GetDictionary() : nullptr;
        done(object && object->GetType("executionContextId") == VTYPE_INT ? object->GetInt("executionContextId") : 0);
      });
    });
  }
  // kPageTranslateScript + |call| in |context|; the returned value, or nullptr.
  void Run(int context, const std::string& call, Reply reply) {
    auto params = CefDictionaryValue::Create();
    params->SetString("expression", std::string(kPageTranslateScript) + "." + call);
    params->SetInt("contextId", context); params->SetBool("returnByValue", true);
    Send("Runtime.evaluate", params, [reply = std::move(reply)](CefRefPtr<CefValue> result) {
      const auto object = result && result->GetType() == VTYPE_DICTIONARY ? result->GetDictionary() : nullptr;
      const auto inner = object && !object->HasKey("exceptionDetails") ? object->GetDictionary("result") : nullptr;
      reply(inner && inner->HasKey("value") ? inner->GetValue("value")->Copy() : nullptr);  // Its own copy: |result| goes away.
    });
  }
  void OnDevToolsMethodResult(CefRefPtr<CefBrowser>, int id, bool success, const void* data, size_t size) override {
    std::string json;
    if (success && size <= kMaxReply) json.assign(static_cast<const char*>(data), size);
    CefPostTask(TID_UI, base::BindOnce(&PageCalls::Result, CefRefPtr<PageCalls>(this), id, std::move(json)));
  }

 private:
  void Send(const char* method, CefRefPtr<CefDictionaryValue> params, Reply reply) {
    if (!registration_ || !page_->IsValid()) { reply(nullptr); return; }
    const int id = page_->GetHost()->ExecuteDevToolsMethod(0, method, params);
    if (id <= 0) { reply(nullptr); return; }
    pending_[id] = std::move(reply);
    CefPostDelayedTask(TID_UI, base::BindOnce(&PageCalls::Result, CefRefPtr<PageCalls>(this), id, std::string()), kEvaluateTimeoutMs);
  }
  void Result(int id, std::string json) {
    const auto found = pending_.find(id);
    if (found == pending_.end()) return;
    auto reply = std::move(found->second); pending_.erase(found);
    reply(json.empty() ? nullptr : CefParseJSON(json, JSON_PARSER_RFC));
  }
  CefRefPtr<CefBrowser> page_;
  CefRefPtr<CefRegistration> registration_;
  std::map<int, Reply> pending_;
  IMPLEMENT_REFCOUNTING(PageCalls);
};

struct Unit { int id; std::string html, text; bool visible; };

struct TranslationDocument {
  int id;
  std::string url, language, target, phase;
  unsigned long long undo_until = 0;
  int download = -1;
  bool detection_started = false, offered = false;
};

struct TranslationTab {
  std::optional<TranslationDocument> document;
  std::string language;
  bool popup_opened = false, checking_navigation = false;
};

std::map<int, TranslationTab>& Tabs() {
  static std::map<int, TranslationTab> tabs;
  return tabs;
}

TranslationTab* FindTab(CefRefPtr<CefBrowser> page) {
  if (!page || !page->IsValid()) return nullptr;
  const auto found = Tabs().find(page->GetIdentifier());
  return found == Tabs().end() ? nullptr : &found->second;
}

TranslationDocument* CurrentDocument(CefRefPtr<CefBrowser> page) {
  const auto tab = FindTab(page);
  if (!tab || !tab->document) return nullptr;
  const auto frame = page->GetMainFrame();
  return frame && frame->IsValid() && frame->GetURL() == tab->document->url ? &*tab->document : nullptr;
}

TranslationDocument* PendingLanguageCheck(CefRefPtr<CefBrowser> page, int id) {
  const auto doc = CurrentDocument(page);
  return doc && doc->id == id && doc->phase.empty() ? doc : nullptr;
}

TranslationDocument& TrackDocument(CefRefPtr<CefBrowser> page, std::string url) {
  static int sequence = 0;
  if (sequence == std::numeric_limits<int>::max()) sequence = 0;
  return Tabs()[page->GetIdentifier()].document.emplace(TranslationDocument{++sequence, std::move(url), {}, {}, {}});
}

void RefreshTranslation(const std::shared_ptr<State>& state, CefRefPtr<CefBrowser> page) {
  if (state && state->workspace && state->content && state->content->GetIdentifier() == page->GetIdentifier())
    state->workspace->Refresh();
}

void UpdateModelDownload(const std::shared_ptr<State>& state, CefRefPtr<CefBrowser> page, int id, int percent) {
  const auto doc = CurrentDocument(page);
  if (!doc || doc->id != id || doc->download == percent) return;
  doc->download = percent;
  RefreshTranslation(state, page);
  if (state && state->probe) {
    auto details = CefDictionaryValue::Create();
    details->SetInt("percent", percent);
    state->Record("translation_model_progress", details);
  }
}

bool OffersEnabled(const std::shared_ptr<State>& state, CefRefPtr<CefBrowser> page) {
  if (!state->session || !state->session->preferences) return true;
  const auto& prefs = state->session->preferences->Get();
  return prefs.translation_offers && !prefs.translation_excluded_sites.contains(SiteOrigin(page));
}

bool CanOffer(const std::shared_ptr<State>& state, CefRefPtr<CefBrowser> page, const std::string& from, const std::string& to) {
  const auto preferences = state->session ? state->session->preferences : nullptr;
  return OffersEnabled(state, page) && (!preferences || !preferences->Get().translation_known_languages.contains(from)) &&
      !from.empty() && !TranslationRoute(from, to).empty();
}

void EndLanguageCheck(const std::shared_ptr<State>& state, CefRefPtr<CefBrowser> page, int id, std::string source = {}) {
  if (!state) return;
  const auto doc = PendingLanguageCheck(page, id);
  if (!doc) return;
  auto& tab = Tabs().at(page->GetIdentifier());
  tab.checking_navigation = false;
  if (ValidLanguageCode(source)) {
    if (tab.language != source) {
      tab.language = source;
      tab.popup_opened = false;
    }
    doc->language = std::move(source);
    doc->phase = "offer";
  }
  RefreshTranslation(state, page);
}

void DetectSampleLanguage(CefRefPtr<CefValue> value, PageCalls::Reply reply) {
  const auto sample = value && value->GetType() == VTYPE_DICTIONARY ? value->GetDictionary() : nullptr;
  if (!sample || sample->GetType("lang") != VTYPE_STRING || sample->GetType("text") != VTYPE_STRING) {
    reply(nullptr);
    return;
  }
  const auto lang = sample->GetString("lang");
  const auto text = sample->GetString("text");
  if (lang.length() > 40 || text.length() < 35 || text.length() > 2001) {
    reply(nullptr);
    return;
  }
  auto argument = CefValue::Create();
  argument->SetString(text);
  CallTranslator("window.solTranslator.detectPage(" + Json(argument) + ",'" + CatalogLanguage(lang.ToString()) + "')",
                 std::move(reply));
}

// One page's translation, from "Translate page" until "Show original", navigation or close.
class Job final : public std::enable_shared_from_this<Job> {
 public:
  Job(std::weak_ptr<State> state, CefRefPtr<CefBrowser> page, int document, std::string target)
      : state_(std::move(state)), page_(page), id_(page->GetIdentifier()), document_(document), target_(std::move(target)),
      calls_(new PageCalls(page)) {}
  // Still translating, or something on the page is translated (so "Show original" applies).
  bool active() const { return !stopped_ || translated_; }
  void Start() {
    auto self = shared_from_this();
    calls_->World([self](int context) {
      if (self->stopped_) return;
      self->Note("world", std::to_string(context));
      if (!context) { self->Fail(); return; }
      self->context_ = context;
      self->Collect();
    });
  }
  void Stop() { stopped_ = true; ++generation_; }
  void Restore(std::function<void(bool)> done) {
    Stop();
    if (!context_) { calls_->Detach(); done(true); return; }
    auto self = shared_from_this();
    calls_->Run(context_, "restore()", [self, done = std::move(done)](CefRefPtr<CefValue> value) {
      self->calls_->Detach();
      done(value && value->GetType() == VTYPE_DICTIONARY && value->GetDictionary()->GetString("state") == "restored");
    });
  }
  void Detach() { Stop(); calls_->Detach(); }

 private:
  std::shared_ptr<State> Owner() const { return state_.lock(); }
  void Note(const std::string& step, const std::string& detail = {}) {
    if (const auto state = Owner(); state && state->probe) {
      auto data = CefDictionaryValue::Create(); data->SetString("step", step); data->SetString("detail", detail.substr(0, 600));
      state->Record("page_translation", data);
    }
  }
  void Toast(const std::wstring& text, int duration_ms = 2500) {
    Note("toast", CefString(text).ToString());
    if (const auto state = Owner(); state && state->embedded) state->embedded->Notify("translate", text, {}, L"", duration_ms, id_);
  }
  void Fail() {
    if (stopped_) return;
    Stop();
    if (!translated_) Update("error");
    Toast(translated_ ? L"Part of the page could not be translated" : L"The page could not be translated");
  }
  void Update(std::string phase, const std::string& source = {}) {
    const auto doc = CurrentDocument(page_);
    if (!doc || doc->id != document_) return;
    if (doc->language.empty()) doc->language = source;
    doc->phase = std::move(phase);
    RefreshTranslation(Owner(), page_);
  }

  void Collect() {
    if (stopped_) return;
    auto self = shared_from_this();
    calls_->Run(context_, "collect({units:" + std::to_string(kBatchUnits) + ",chars:" + std::to_string(kBatchChars) + "})",
                [self](CefRefPtr<CefValue> value) { self->Collected(value); });
  }
  void Collected(CefRefPtr<CefValue> value) {
    if (stopped_) return;
    Note("collected", value ? Json(value).substr(0, 600) : "null");
    const auto reply = value && value->GetType() == VTYPE_DICTIONARY ? value->GetDictionary() : nullptr;
    const auto list = reply && reply->GetType("units") == VTYPE_LIST ? reply->GetList("units") : nullptr;
    if (!list || list->GetSize() > static_cast<size_t>(kBatchUnits)) { Fail(); return; }
    page_lang_ = CatalogLanguage(reply->GetType("lang") == VTYPE_STRING ? reply->GetString("lang").ToString() : "");
    more_ = reply->GetType("more") == VTYPE_BOOL && reply->GetBool("more");
    units_.clear();
    for (size_t i = 0; i < list->GetSize(); ++i) {
      const auto item = list->GetType(i) == VTYPE_DICTIONARY ? list->GetDictionary(i) : nullptr;
      if (!item || item->GetType("id") != VTYPE_INT || item->GetType("html") != VTYPE_STRING || item->GetType("text") != VTYPE_STRING) { Fail(); return; }
      Unit unit{item->GetInt("id"), item->GetString("html").ToString(), item->GetString("text").ToString(),
                item->GetType("visible") == VTYPE_BOOL && item->GetBool("visible")};
      if (unit.html.size() > kMaxUnitHtml) continue;  // Left as it is: one huge run of text.
      units_.push_back(std::move(unit));
    }
    if (units_.empty()) { Settle(); return; }
    auto texts = CefListValue::Create();
    for (size_t i = 0; i < units_.size(); ++i) texts->SetString(i, units_[i].text);
    auto wrapped = CefValue::Create(); wrapped->SetList(texts);
    auto self = shared_from_this();
    // page_lang_ is a validated catalog code (letters and "_"), safe inside the quotes.
    CallTranslator("window.solTranslator.detect(" + Json(wrapped) + ",'" + page_lang_ + "')",
                   [self](CefRefPtr<CefValue> value) { self->Detected(value); });
  }
  void Detected(CefRefPtr<CefValue> value) {
    if (stopped_) return;
    Note("detected", value ? Json(value) : "null");
    const auto langs = value && value->GetType() == VTYPE_LIST ? value->GetList() : nullptr;
    if (!langs || langs->GetSize() != units_.size()) { Fail(); return; }
    groups_.clear();
    auto skipped = CefListValue::Create();
    const auto skip = [&skipped](const Unit& unit) { skipped->SetInt(skipped->GetSize(), unit.id); };
    for (size_t i = 0; i < units_.size(); ++i) {
      std::string lang = langs->GetType(i) == VTYPE_STRING ? langs->GetString(i).ToString() : "";
      if (!ValidLanguageCode(lang)) lang.clear();  // Unsure: the translator page already fell back to the page's lang.
      if (lang.empty()) { unknown_ = true; skip(units_[i]); continue; }
      if (lang == target_) { same_ = true; skip(units_[i]); continue; }
      if (TranslationRoute(lang, target_).empty()) { unsupported_.insert(lang); skip(units_[i]); continue; }
      groups_[lang].push_back(units_[i]);
      amount_[lang] += units_[i].text.size();
    }
    if (main_.empty()) {
      // The page's language: its lang attribute, else the language with the most text so far.
      main_ = page_lang_;
      if (main_.empty() || main_ == target_)
        for (const auto& [lang, amount] : amount_) if (main_.empty() || amount > amount_[main_]) main_ = lang;
    }
    // A few foreign words (a list of language names, a quote) are not worth a model download:
    // they are translated only when that model is already here or the language has real text
    // (kMinorLanguageChars, or a tenth of the page's text so far).
    size_t total = 0;
    for (const auto& [lang, amount] : amount_) total += amount;
    for (auto it = groups_.begin(); it != groups_.end();) {
      bool installed = true;
      for (const auto* model : TranslationRoute(it->first, target_)) installed &= TranslationModelInstalled(*model);
      const size_t amount = amount_[it->first];
      if (installed || it->first == main_ || amount >= kMinorLanguageChars || amount * 10 >= total) { ++it; continue; }
      for (const auto& unit : it->second) skip(unit);
      it = groups_.erase(it);
    }
    if (skipped->GetSize()) {
      auto wrapped = CefValue::Create(); wrapped->SetList(skipped);
      calls_->Run(context_, "skip(" + Json(wrapped) + ")", [](CefRefPtr<CefValue>) {});
    }
    NextGroup();
  }
  void NextGroup() {
    if (stopped_) return;
    if (groups_.empty()) { if (more_) Collect(); else Settle(); return; }
    const auto lang = groups_.begin()->first;
    auto route = TranslationRoute(lang, target_);
    bool installed = true;
    for (const auto* model : route) installed &= TranslationModelInstalled(*model);
    if (installed) { TranslateGroup(lang); return; }
    UpdateModelDownload(Owner(), page_, document_, 0);
    auto self = shared_from_this();
    const unsigned generation = generation_;
    WithTranslatorContext([self, route, lang, generation](CefRefPtr<CefRequestContext> context) {
      if (self->stopped_ || generation != self->generation_) return;
      auto last = std::make_shared<int>(-1);
      DownloadTranslationModels(route, context,
          [self, last](int percent) {
            if (self->stopped_ || percent / 10 == *last) return;
            *last = percent / 10;
            UpdateModelDownload(self->Owner(), self->page_, self->document_, percent);
          },
          [self, lang](bool ok) {
            if (self->stopped_) return;
            UpdateModelDownload(self->Owner(), self->page_, self->document_, -1);
            if (!ok) { self->Fail(); return; }
            self->TranslateGroup(lang);
          });
    });
  }
  void TranslateGroup(const std::string& lang) {
    if (stopped_) return;
    const auto units = groups_[lang];
    auto texts = CefListValue::Create();
    bool visible = false;
    for (size_t i = 0; i < units.size(); ++i) { texts->SetString(i, units[i].html); visible |= units[i].visible; }
    auto request = CefDictionaryValue::Create();
    request->SetString("from", lang); request->SetString("to", target_); request->SetList("texts", texts);
    request->SetBool("html", true); request->SetInt("priority", visible ? 0 : 1);
    auto wrapped = CefValue::Create(); wrapped->SetDictionary(request);
    auto self = shared_from_this();
    CallTranslator("window.solTranslator.translate(" + Json(wrapped) + ")", [self, lang, units](CefRefPtr<CefValue> value) {
      if (self->stopped_) return;
      self->Note("translated", value ? Json(value).substr(0, 600) : "null");
      const auto reply = value && value->GetType() == VTYPE_DICTIONARY ? value->GetDictionary() : nullptr;
      if (!reply || !reply->GetBool("ok")) {
        if (reply && reply->GetBool("integrity") && reply->GetType("pair") == VTYPE_STRING) {
          // A damaged or replaced file: remove that model so the next attempt downloads it again.
          const auto pair = reply->GetString("pair").ToString();
          const auto dash = pair.find('-');
          if (dash != std::string::npos)
            if (const auto* model = FindTranslationModel(pair.substr(0, dash), pair.substr(dash + 1))) RemoveTranslationModel(*model);
        }
        self->Fail();
        return;
      }
      const auto out = reply->GetType("texts") == VTYPE_LIST ? reply->GetList("texts") : nullptr;
      if (!out || out->GetSize() != units.size()) { self->Fail(); return; }
      auto results = CefListValue::Create();
      for (size_t i = 0; i < units.size(); ++i) {
        if (out->GetType(i) != VTYPE_STRING) continue;
        const auto html = out->GetString(i).ToString();
        if (html.size() > units[i].html.size() * 4 + 2000) continue;
        auto item = CefDictionaryValue::Create(); item->SetInt("id", units[i].id); item->SetString("html", html);
        results->SetDictionary(results->GetSize(), item);
      }
      auto wrapped_results = CefValue::Create(); wrapped_results->SetList(results);
      self->calls_->Run(self->context_, "apply(" + Json(wrapped_results) + ")", [self, lang](CefRefPtr<CefValue> applied) {
        if (self->stopped_) return;
        if (!applied) { self->Fail(); return; }
        self->sources_.insert(lang);
        if (!self->translated_) {
          self->translated_ = true;
          self->Update("translated", lang);
          self->Toast(L"Translated from " + Names(self->sources_));
        }
        self->groups_.erase(lang);
        self->NextGroup();
      });
    });
  }
  // Everything collected so far is done: say what happened the first time, then look for new text.
  void Settle() {
    if (stopped_) return;
    if (!settled_) {
      settled_ = true;
      if (translated_ && sources_.size() > 1) Toast(L"Translated from " + Names(sources_));
      if (!translated_) {
        if (!unsupported_.empty()) Toast(L"Translation from " + Names(unsupported_) + L" is not available yet");
        else if (same_) Toast(L"This page is already in " + LanguageDisplayName(target_));
        else if (unknown_) Toast(L"Sol could not tell the language of this page");
        else Toast(L"There is no text to translate on this page");
        Stop();
        Update("error");
        return;
      }
    }
    auto self = shared_from_this();
    const unsigned generation = generation_;
    CefPostDelayedTask(TID_UI, base::BindOnce([](std::weak_ptr<Job> weak, unsigned generation) {
      if (const auto job = weak.lock(); job && !job->stopped_ && job->generation_ == generation) job->Collect();
    }, std::weak_ptr<Job>(self), generation), kPollMs);
  }

  std::weak_ptr<State> state_;
  CefRefPtr<CefBrowser> page_;
  int id_;
  int document_;
  std::string target_;
  CefRefPtr<PageCalls> calls_;
  int context_ = 0;
  unsigned generation_ = 0;
  bool stopped_ = false, translated_ = false, settled_ = false, more_ = false, same_ = false, unknown_ = false;
  std::string page_lang_, main_;
  std::map<std::string, size_t> amount_;  // Text seen per language, over the whole page.
  std::vector<Unit> units_;
  std::map<std::string, std::vector<Unit>> groups_;
  std::set<std::string> sources_, unsupported_;
};

std::map<int, std::shared_ptr<Job>>& Jobs() {
  static std::map<int, std::shared_ptr<Job>> jobs;
  return jobs;
}

void StartTranslation(const std::shared_ptr<State>& state, CefRefPtr<CefBrowser> page,
                      TranslationDocument& doc, std::string target) {
  Tabs().at(page->GetIdentifier()).checking_navigation = false;
  doc.phase = "working";
  doc.target = std::move(target);
  doc.undo_until = 0;
  doc.download = -1;
  auto start = [weak = std::weak_ptr<State>(state), page, id = doc.id](bool restored) {
    const auto state = weak.lock();
    const auto current = CurrentDocument(page);
    if (!state || !current || current->id != id) return;
    if (restored) {
      auto job = std::make_shared<Job>(state, page, id, current->target);
      Jobs()[page->GetIdentifier()] = job;
      job->Start();
    } else current->phase = "error";
    RefreshTranslation(state, page);
  };
  if (const auto found = Jobs().find(page->GetIdentifier()); found != Jobs().end()) {
    auto previous = found->second;
    Jobs().erase(found);
    RefreshTranslation(state, page);
    previous->Restore(std::move(start));
  } else start(true);
}

class SelectionJob : public std::enable_shared_from_this<SelectionJob> {
 public:
  SelectionJob(CefString text, std::string target, SelectionTranslationUpdate update)
      : text_(std::move(text)), view_{std::move(target)}, update_(std::move(update)) {}

  void Start(const std::string& hint) {
    auto texts = CefListValue::Create();
    texts->SetString(0, text_);
    auto argument = CefValue::Create(); argument->SetList(texts);
    CallTranslator("window.solTranslator.detect(" + Json(argument) + ",'" + hint + "')",
                   [self = shared_from_this()](CefRefPtr<CefValue> value) { self->Detected(value); });
  }

 private:
  void Detected(CefRefPtr<CefValue> value) {
    if (!update_(view_)) return;
    const auto list = value && value->GetType() == VTYPE_LIST ? value->GetList() : nullptr;
    if (!list || list->GetSize() != 1 || list->GetType(0) != VTYPE_STRING) { Finish("error"); return; }
    const auto source = list->GetString(0).ToString();
    if (!ValidLanguageCode(source)) { Finish("unavailable"); return; }
    view_.source = source;
    if (source == view_.target) { Finish("same"); return; }
    auto route = TranslationRoute(source, view_.target);
    if (route.empty()) { Finish("unavailable"); return; }
    if (std::ranges::all_of(route, [](const TranslationModel* model) { return TranslationModelInstalled(*model); })) {
      Translate();
      return;
    }
    view_.download = 0;
    if (!update_(view_)) return;
    WithTranslatorContext([self = shared_from_this(), route = std::move(route)](CefRefPtr<CefRequestContext> context) {
      if (!self->update_(self->view_)) return;
      DownloadTranslationModels(route, context, [self](int percent) {
        if (self->view_.download / 10 == percent / 10) return;
        self->view_.download = percent;
        self->update_(self->view_);
      }, [self](bool ok) {
        if (!ok) { self->Finish("error"); return; }
        self->view_.download = -1;
        if (self->update_(self->view_)) self->Translate();
      });
    });
  }

  void Translate() {
    auto texts = CefListValue::Create(); texts->SetString(0, text_);
    auto request = CefDictionaryValue::Create();
    request->SetString("from", view_.source); request->SetString("to", view_.target);
    request->SetList("texts", texts); request->SetBool("html", false);
    auto wrapped = CefValue::Create(); wrapped->SetDictionary(request);
    CallTranslator("window.solTranslator.translate(" + Json(wrapped) + ")", [self = shared_from_this()](CefRefPtr<CefValue> value) {
      if (!self->update_(self->view_)) return;
      const auto response = value && value->GetType() == VTYPE_DICTIONARY ? value->GetDictionary() : nullptr;
      const auto result = response && response->GetBool("ok") ? response->GetList("texts") : nullptr;
      if (!result || result->GetSize() != 1 || result->GetType(0) != VTYPE_STRING) { self->Finish("error"); return; }
      auto translated = result->GetString(0);
      if (translated.empty() || translated.length() > kMaxTranslationSelection * 4 + 2000) { self->Finish("error"); return; }
      self->view_.text = std::move(translated);
      self->Finish("translated");
    });
  }

  void Finish(const char* phase) {
    view_.phase = phase;
    view_.download = -1;
    update_(view_);
  }

  CefString text_;
  SelectionTranslation view_;
  SelectionTranslationUpdate update_;
};

}  // namespace

void SetPageTranslationTarget(const std::string& chromium_locale) { Target() = TranslationTargetFromLocale(chromium_locale); }
std::string PageTranslationTarget(const std::shared_ptr<State>& state) {
  if (!state->session || !state->session->preferences) return Target();
  const auto& configured = state->session->preferences->Get().translation_target;
  return configured == "system" ? Target() : configured;
}
CefRefPtr<CefListValue> PageTranslationLanguages(std::string_view from) {
  std::set<std::string_view> codes;
  for (const auto& model : TranslationModels()) codes.insert(model.to);
  auto list = CefListValue::Create();
  for (const auto code : codes)
    if (from.empty() || !TranslationRoute(from, code).empty()) list->SetString(list->GetSize(), std::string(code));
  return list;
}
CefRefPtr<CefListValue> PageTranslationSourceLanguages() {
  std::set<std::string_view> codes;
  for (const auto& model : TranslationModels()) codes.insert(model.from);
  auto list = CefListValue::Create();
  for (const auto code : codes) list->SetString(list->GetSize(), std::string(code));
  return list;
}
bool PageTranslated(int browser_id) {
  const auto found = Jobs().find(browser_id);
  return found != Jobs().end() && found->second->active();
}
void CheckPageTranslationOffer(const std::shared_ptr<State>& state, CefRefPtr<CefBrowser> page) {
  if (!state->workspace || state->little_window || !page || !page->IsValid() || PageTranslated(page->GetIdentifier())) return;
  const auto frame = page->GetMainFrame();
  const auto url = frame ? frame->GetURL().ToString() : std::string{};
  if (!HasWebScheme(url) || !OffersEnabled(state, page)) {
    CancelPageTranslationCheck(state, page);
    return;
  }
  auto doc = CurrentDocument(page);
  if (!doc) doc = &TrackDocument(page, url);
  if (doc->detection_started || !doc->phase.empty()) return;
  doc->detection_started = true;
  const int id = doc->id;
  const CefRefPtr<PageCalls> calls = new PageCalls(page);
  calls->World([calls, page, id, weak = std::weak_ptr<State>(state)](int context) {
    if (!context || !PendingLanguageCheck(page, id)) {
      calls->Detach();
      EndLanguageCheck(weak.lock(), page, id);
      return;
    }
    calls->Run(context, "sample()", [calls, page, id, weak](CefRefPtr<CefValue> value) {
      calls->Detach();
      if (!PendingLanguageCheck(page, id)) return;
      DetectSampleLanguage(value, [page, id, weak](CefRefPtr<CefValue> result) {
        const auto source = result && result->GetType() == VTYPE_STRING ? result->GetString().ToString() : "";
        EndLanguageCheck(weak.lock(), page, id, source);
      });
    });
  });
}
CefRefPtr<CefDictionaryValue> DescribePageTranslationOffer(const std::shared_ptr<State>& state, CefRefPtr<CefBrowser> page) {
  const auto doc = CurrentDocument(page);
  if (!doc || doc->phase.empty()) return nullptr;
  const auto to = doc->target.empty() ? PageTranslationTarget(state) : doc->target;
  if (doc->phase == "offer") {
    if (!CanOffer(state, page, doc->language, to)) return nullptr;
    doc->offered = true;
  }
  auto value = CefDictionaryValue::Create();
  value->SetInt("id", doc->id); value->SetString("from", doc->language); value->SetString("to", to);
  value->SetString("phase", doc->phase);
  value->SetBool("toolbar", doc->offered);
  if (doc->phase == "offer" && !Tabs().at(page->GetIdentifier()).popup_opened) value->SetBool("automatic", true);
  if (doc->download >= 0) value->SetInt("download", doc->download);
  return value;
}
bool AcceptPageTranslationOffer(const std::shared_ptr<State>& state, int offer_id, const std::string& target) {
  const auto page = state->content;
  const auto doc = CurrentDocument(page);
  const auto view = DescribePageTranslationOffer(state, page);
  if (!doc || !view || doc->id != offer_id || doc->phase == "working") return false;
  const auto to = target.empty() ? view->GetString("to").ToString() : target;
  if (!HasTranslationTarget(to) || (!doc->language.empty() && TranslationRoute(doc->language, to).empty()) ||
      (doc->phase == "translated" && doc->target == to)) return false;
  StartTranslation(state, page, *doc, to);
  return true;
}
bool MarkAutomaticTranslationPopup(CefRefPtr<CefBrowser> page, int id) {
  const auto doc = CurrentDocument(page);
  if (!doc || doc->id != id || doc->phase != "offer" || !doc->offered) return false;
  auto& tab = Tabs().at(page->GetIdentifier());
  if (tab.popup_opened) return false;
  tab.popup_opened = true;
  return true;
}
bool BeginPageTranslationNavigation(int browser_id, bool web) {
  if (const auto job = Jobs().find(browser_id); job != Jobs().end()) { job->second->Detach(); Jobs().erase(job); }
  const auto found = Tabs().find(browser_id);
  if (found == Tabs().end()) return false;
  if (!web) {
    Tabs().erase(found);
  } else {
    auto& tab = found->second;
    tab.checking_navigation = tab.checking_navigation || (tab.document && tab.document->offered);
    tab.document.reset();
  }
  return true;
}
void ForgetPageTranslation(int browser_id) {
  BeginPageTranslationNavigation(browser_id, false);
}
void CancelPageTranslationCheck(const std::shared_ptr<State>& state, CefRefPtr<CefBrowser> page) {
  const auto tab = FindTab(page);
  if (!tab || !tab->checking_navigation) return;
  tab->checking_navigation = false;
  RefreshTranslation(state, page);
}
bool KeepTranslationGlobeWhileChecking(const std::shared_ptr<State>& state, CefRefPtr<CefBrowser> page) {
  const auto tab = FindTab(page);
  if (!tab || !(tab->checking_navigation || (tab->document && tab->document->offered && page->IsLoading()))) return false;
  const auto frame = page->GetMainFrame();
  return frame && HasWebScheme(frame->GetURL().ToString()) && CanOffer(state, page, tab->language, PageTranslationTarget(state));
}
int PageTranslationDocumentId(CefRefPtr<CefBrowser> page) {
  const auto doc = CurrentDocument(page);
  return doc ? doc->id : 0;
}
void PrefetchPageTranslationModel(const std::shared_ptr<State>& state) {
  CefPostDelayedTask(TID_UI, base::BindOnce([](std::weak_ptr<State> weak) {
    const auto owner = weak.lock();
    if (!owner) return;
    const auto* model = FindTranslationModel("en", PageTranslationTarget(owner));
    if (!model || TranslationModelInstalled(*model)) return;
    WithTranslatorContext([model](CefRefPtr<CefRequestContext> context) {
      if (context) DownloadTranslationModels({model}, context, nullptr, [](bool) {});
    });
  }, std::weak_ptr<State>(state)), 90000);
}
void ForgetAllPageTranslations() {
  Tabs().clear();
  auto jobs = std::move(Jobs()); Jobs().clear();
  for (auto& [id, job] : jobs) job->Detach();
}
void TranslatePage(const std::shared_ptr<State>& state, CefRefPtr<CefBrowser> page) {
  if (!page || !page->IsValid()) return;
  auto doc = CurrentDocument(page);
  if (!doc) doc = &TrackDocument(page, page->GetMainFrame()->GetURL().ToString());
  if (doc->phase == "working" || doc->phase == "translated") return;
  StartTranslation(state, page, *doc, PageTranslationTarget(state));
}
void ShowOriginalPage(const std::shared_ptr<State>& state, CefRefPtr<CefBrowser> page) {
  const auto doc = CurrentDocument(page);
  if (!doc) return;
  const auto found = Jobs().find(page->GetIdentifier());
  if (found == Jobs().end()) return;
  auto job = found->second; Jobs().erase(found);
  const bool translated = doc->phase == "translated";
  doc->phase = "working";
  doc->download = -1;
  RefreshTranslation(state, page);
  job->Restore([page, id = doc->id, translated, weak = std::weak_ptr<State>(state)](bool restored) {
    const auto owner = weak.lock();
    const auto doc = CurrentDocument(page);
    if (!owner || !doc || doc->id != id) return;
    doc->phase = restored ? "original" : "error";
    if (restored && translated && owner->embedded) {
      constexpr int undo_ms = 4000;
      doc->undo_until = GetTickCount64() + undo_ms;
      const auto locale = owner->session ? owner->session->interface_locale : "en";
      owner->embedded->Notify("translate", CefString(InterfaceText(locale, InterfaceMessage::MenuTranslationRestored)).ToWString(),
          {L"Ctrl+Z"}, CefString(InterfaceText(locale, InterfaceMessage::MenuTranslationUndo)).ToWString(), undo_ms, page->GetIdentifier());
    }
    RefreshTranslation(owner, page);
  });
}

bool RestorePageTranslation(const std::shared_ptr<State>& state, int id) {
  const auto doc = CurrentDocument(state->content);
  if (!doc || doc->id != id || doc->phase != "translated") return false;
  ShowOriginalPage(state, state->content);
  return true;
}

bool UndoPageTranslationRestore(const std::shared_ptr<State>& state, CefRefPtr<CefBrowser> page) {
  const auto doc = CurrentDocument(page);
  if (!doc || doc->phase != "original" || doc->undo_until < GetTickCount64() || doc->target.empty()) return false;
  StartTranslation(state, page, *doc, doc->target);
  return true;
}

void TranslateSelection(const std::shared_ptr<State>& state, CefRefPtr<CefBrowser> page,
                        const CefString& text, SelectionTranslationUpdate update) {
  if (!page || !page->IsValid() || text.empty() || text.length() > kMaxTranslationSelection) return;
  const auto frame = page->GetMainFrame();
  if (!frame || !HasWebScheme(frame->GetURL().ToString())) return;
  auto doc = CurrentDocument(page);
  if (!doc) doc = &TrackDocument(page, frame->GetURL().ToString());
  const auto guarded = [page, id = doc->id, update = std::move(update)](const SelectionTranslation& next) {
    const auto current = CurrentDocument(page);
    return current && current->id == id && update(next);
  };
  auto job = std::make_shared<SelectionJob>(text, PageTranslationTarget(state), guarded);
  job->Start(doc->language);
}

}  // namespace red
