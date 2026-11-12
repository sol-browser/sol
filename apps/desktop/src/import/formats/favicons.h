#pragma once
#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>

namespace red::importer {
// Site icons from another browser's icon database, so imported tabs and
// bookmarks show their icons before they are ever loaded. Result: lower-case
// host → PNG (≤ 12 KB, the size closest to 32 px). Only `hosts` are read.
using Favicons = std::map<std::string, std::string>;
// Chromium "Favicons": icon_mapping(page_url, icon_id) + favicon_bitmaps(image_data PNG, width).
Favicons ReadChromiumFavicons(const std::filesystem::path& db, const std::set<std::string>& hosts);
// Firefox/Zen "favicons.sqlite": moz_pages_w_icons + moz_icons_to_pages + moz_icons(data, width).
Favicons ReadFirefoxFavicons(const std::filesystem::path& db, const std::set<std::string>& hosts);
// Arc's extensionless PNG files are keyed by the MD5 of the original item ID.
// |directory| is the expected absolute cache directory inside Arc's data root.
std::optional<std::string> ReadArcItemFavicon(const std::filesystem::path& directory, const std::string& item_id);
// Lower-case DNS host of an http(s) URL, or empty.
std::string UrlHost(std::string_view url);
// The PNG itself, or a PNG image embedded in an ICO (the size closest to 32 px).
std::optional<std::string> IconPng(std::string_view bytes);
}  // namespace red::importer
