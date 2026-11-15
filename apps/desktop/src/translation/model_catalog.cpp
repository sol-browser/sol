#include "translation/model_catalog.h"

#include <array>

namespace red {
namespace {

constexpr TranslationModel kModels[] = {
#include "translation/model_catalog.inc"
};

}  // namespace

std::span<const TranslationModel> TranslationModels() { return kModels; }

bool HasTranslationTarget(std::string_view code) {
  for (const auto& model : kModels) if (model.to == code) return true;
  return false;
}

const TranslationModel* FindTranslationModel(std::string_view from, std::string_view to) {
  for (const auto& model : kModels)
    if (model.from == from && model.to == to) return &model;
  return nullptr;
}

std::vector<const TranslationModel*> TranslationRoute(std::string_view from, std::string_view to) {
  if (from.empty() || to.empty() || from == to) return {};
  if (const auto direct = FindTranslationModel(from, to)) return {direct};
  if (from == "en" || to == "en") return {};
  const auto first = FindTranslationModel(from, "en"), second = FindTranslationModel("en", to);
  if (!first || !second) return {};
  return {first, second};
}

}  // namespace red
