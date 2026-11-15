#pragma once

#include <span>
#include <string_view>
#include <vector>

namespace red {

// Firefox Translations models (MPL 2.0), one direction each, from Mozilla's public bucket
// (scripts/update_translation_models.py, docs/page-translation.md).
inline constexpr std::string_view kTranslationModelBase =
    "https://storage.googleapis.com/moz-fx-translations-data--303e-prod-translations-data/";

struct TranslationModelFile {
  std::string_view path;    // Relative to kTranslationModelBase, gzip; empty when the model has no such file.
  std::string_view sha256;  // Of the uncompressed bytes.
  unsigned size;            // Uncompressed bytes.
  std::string_view name() const { return path.substr(path.rfind('/') + 1); }
  bool present() const { return !path.empty(); }
};

struct TranslationModel {
  std::string_view from, to, architecture;
  TranslationModelFile model, shortlist, vocab, target_vocab;  // target_vocab: only for split vocabularies.
};

std::span<const TranslationModel> TranslationModels();
const TranslationModel* FindTranslationModel(std::string_view from, std::string_view to);
// The models that translate |from| into |to|: the direct one, else through English
// (from→en, en→to), as Firefox does. Empty when there is no way.
std::vector<const TranslationModel*> TranslationRoute(std::string_view from, std::string_view to);
bool HasTranslationTarget(std::string_view code);

}  // namespace red
