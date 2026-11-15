#pragma once

#include <filesystem>
#include <functional>
#include <vector>

#include "include/cef_request_context.h"
#include "translation/model_catalog.h"

namespace red {

// Translation models on disk: <profile>\Translation Models\<from>-<to>\<file>.gz (docs/page-translation.md).
// The files stay gzip; the translator page unpacks them and checks every SHA-256 before use. UI thread only.
void SetTranslationModelDirectory(const std::filesystem::path& directory);
std::filesystem::path TranslationModelFilePath(const TranslationModel& model, const TranslationModelFile& file);
bool TranslationModelInstalled(const TranslationModel& model);
void RemoveTranslationModel(const TranslationModel& model);
// Downloads the files of |models| that are not on disk yet, one at a time, from Mozilla's bucket.
// |progress| gets 0-99 as data arrives; |done| gets whether every model is now on disk. Models already
// downloading for another caller are waited for, not fetched twice.
void DownloadTranslationModels(std::vector<const TranslationModel*> models, CefRefPtr<CefRequestContext> context,
                               std::function<void(int percent)> progress, std::function<void(bool ok)> done);

// Cancels the download in progress and forgets the queue (shutdown); no callbacks run.
void StopTranslationModelDownloads();

}  // namespace red
