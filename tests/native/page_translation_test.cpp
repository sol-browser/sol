#include "translation/page_translation_model.h"
#include "translation/model_catalog.h"

#include <cstdio>
#include <string>

int main() {
  int failures = 0;
  auto expect = [&](bool ok, const char* what) {
    if (!ok) { std::fprintf(stderr, "Failed: %s\n", what); ++failures; }
  };

  expect(red::TranslationTargetFromLocale("uk-UA") == "uk", "uk-UA -> uk");
  expect(red::TranslationTargetFromLocale("uk") == "uk", "uk -> uk");
  expect(red::TranslationTargetFromLocale("pt-BR") == "pt", "pt-BR -> pt");
  expect(red::TranslationTargetFromLocale("zh_TW") == "zh_hant", "zh_TW -> zh_hant");
  expect(red::TranslationTargetFromLocale("RU") == "ru", "RU -> ru");
  expect(red::TranslationTargetFromLocale("fil") == "fil", "three-letter code kept");
  expect(red::TranslationTargetFromLocale("") == "en", "empty -> en");
  expect(red::TranslationTargetFromLocale("e") == "en", "one letter -> en");
  expect(red::TranslationTargetFromLocale("x'y") == "en", "quote rejected");
  expect(red::TranslationTargetFromLocale("abcd-EF") == "en", "four letters rejected");

  expect(red::ValidLanguageCode("de"), "de valid");
  expect(red::ValidLanguageCode("zh_hant"), "zh_hant valid");
  expect(!red::ValidLanguageCode("zh_hans"), "only zh_hant has a script");
  expect(red::CatalogLanguage("zh-TW") == "zh_hant", "zh-TW -> zh_hant");
  expect(red::CatalogLanguage("zh-Hant-HK") == "zh_hant", "zh-Hant-HK -> zh_hant");
  expect(red::CatalogLanguage("zh_HK") == "zh_hant", "zh_HK -> zh_hant");
  expect(red::CatalogLanguage("zh-CN") == "zh", "zh-CN -> zh");
  expect(red::CatalogLanguage("zh-Hans") == "zh", "zh-Hans -> zh");
  expect(red::CatalogLanguage("en-US") == "en", "en-US -> en");
  expect(red::CatalogLanguage("x-default").empty(), "x-default -> nothing");
  expect(red::CatalogLanguage("").empty(), "empty -> nothing");
  expect(red::TranslationTargetFromLocale("zh-TW") == "zh_hant", "Traditional Chinese Windows -> zh_hant");
  expect(red::LanguageDisplayName("zh_hant") == L"Traditional Chinese", "zh_hant name");
  expect(red::TranslationRoute("zh_hant", "uk").size() == 2, "zh_hant -> uk through English");
  expect(red::ValidLanguageCode("haw"), "haw valid");
  expect(!red::ValidLanguageCode("DE"), "upper case invalid");
  expect(!red::ValidLanguageCode("de-AT"), "region invalid");
  expect(!red::ValidLanguageCode("d'"), "quote invalid");
  expect(!red::ValidLanguageCode(""), "empty invalid");


  expect(red::LanguageDisplayName("de") == L"German", "de -> German");
  expect(red::LanguageDisplayName("uk") == L"Ukrainian", "uk -> Ukrainian");
  expect(red::LanguageDisplayName("ru") == L"Russian", "ru -> Russian");
  expect(red::LanguageDisplayName("qq") == L"QQ", "unknown code -> upper case");
  expect(red::LanguageDisplayName("de-AT") == L"DE-AT", "invalid code not looked up");

  // The model catalog (model_catalog.inc) and routes through English.
  const auto models = red::TranslationModels();
  expect(models.size() >= 100, "catalog has the desktop release pairs");
  for (const auto& model : models) {
    for (const auto* file : {&model.model, &model.shortlist, &model.vocab}) {
      expect(file->present(), "model, shortlist and vocabulary present");
      expect(file->path.starts_with("models/") && file->path.ends_with(".gz"), "relative gzip path");
      expect(file->sha256.size() == 64 && file->sha256.find_first_not_of("0123456789abcdef") == std::string_view::npos, "sha256 hex");
      expect(file->size > 0, "uncompressed size");
    }
    expect(model.from != model.to, "one direction");
  }
  const auto de_uk = red::TranslationRoute("de", "uk");
  expect(de_uk.size() == 2 && de_uk[0]->from == "de" && de_uk[0]->to == "en" && de_uk[1]->from == "en" && de_uk[1]->to == "uk",
         "de -> uk goes through English");
  expect(red::TranslationRoute("en", "uk").size() == 1, "en -> uk direct");
  expect(red::TranslationRoute("uk", "en").size() == 1, "uk -> en direct");
  expect(red::TranslationRoute("de", "de").empty(), "same language: no route");
  expect(red::TranslationRoute("qq", "uk").empty(), "unknown language: no route");
  expect(red::TranslationRoute("en", "qq").empty(), "unknown target: no route");
  expect(red::FindTranslationModel("en", "uk")->model.name() == "model.enuk.intgemm.alphas.bin.gz", "file name from path");
  return failures ? 1 : 0;
}
