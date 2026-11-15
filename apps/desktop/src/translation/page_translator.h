#pragma once

#include <memory>
#include <string>

#include "app/state.h"
#include "include/cef_browser.h"

namespace red {

// On-device page translation (docs/page-translation.md). UI thread only.
// The target is the primary subtag of the Chromium UI locale, set once at startup.
void SetPageTranslationTarget(const std::string& chromium_locale);
std::string PageTranslationTarget(const std::shared_ptr<State>& state);
CefRefPtr<CefListValue> PageTranslationLanguages(std::string_view from = {});
CefRefPtr<CefListValue> PageTranslationSourceLanguages();
// Whether the page in |browser_id| currently shows Sol's translation (page menu: "Show original").
bool PageTranslated(int browser_id);
// A language check samples one loaded main document and never downloads translation models.
void CheckPageTranslationOffer(const std::shared_ptr<State>& state, CefRefPtr<CefBrowser> page);
CefRefPtr<CefDictionaryValue> DescribePageTranslationOffer(const std::shared_ptr<State>& state, CefRefPtr<CefBrowser> page);
int PageTranslationDocumentId(CefRefPtr<CefBrowser> page);
bool MarkAutomaticTranslationPopup(CefRefPtr<CefBrowser> page, int id);
bool AcceptPageTranslationOffer(const std::shared_ptr<State>& state, int id, const std::string& target = {});
bool RestorePageTranslation(const std::shared_ptr<State>& state, int id);
bool UndoPageTranslationRestore(const std::shared_ptr<State>& state, CefRefPtr<CefBrowser> page);
// Invalidate commands for the old document, retaining the most recent language on web navigations.
bool BeginPageTranslationNavigation(int browser_id, bool web);
void CancelPageTranslationCheck(const std::shared_ptr<State>& state, CefRefPtr<CefBrowser> page);
bool KeepTranslationGlobeWhileChecking(const std::shared_ptr<State>& state, CefRefPtr<CefBrowser> page);
void ForgetPageTranslation(int browser_id);
// Shutdown: every page's translation stops.
void ForgetAllPageTranslations();
// Once, a minute and a half after start: download the English model for the target language in the
// background (about 24 MB), so English pages, the most common case, translate without waiting.
void PrefetchPageTranslationModel(const std::shared_ptr<State>& state);
// "Translate page" / "Show original" from the page menu. Outcomes are shown as Sol's toast.
void TranslatePage(const std::shared_ptr<State>& state, CefRefPtr<CefBrowser> page);
void ShowOriginalPage(const std::shared_ptr<State>& state, CefRefPtr<CefBrowser> page);

inline constexpr size_t kMaxTranslationSelection = 5000;  // UTF-16 code units.
struct SelectionTranslation {
  std::string target, source;
  std::string phase = "working";
  CefString text;
  int download = -1;
};
using SelectionTranslationUpdate = std::function<bool(const SelectionTranslation&)>;
// The update returns false when the owning popover or source document is gone.
void TranslateSelection(const std::shared_ptr<State>& state, CefRefPtr<CefBrowser> page,
                        const CefString& text, SelectionTranslationUpdate update);

}  // namespace red
