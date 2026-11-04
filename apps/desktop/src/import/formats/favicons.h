#pragma once
#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>

namespace red::import {
// Site icons from another browser's icon database, so imported tabs and
// bookmarks show their icons before they are ever loaded. Result: lower-case
// host → PNG (≤ 12 KB, the size closest to 32 px). Only `hosts` are read.
using Favicons = std::map<std::string, std::string>;
// Chromium "Favicons": icon_mapping(page_url, icon_id) + favicon_bitmaps(image_data PNG, width).
Favicons ReadChromiumFavicons(const std::filesystem::path& db, const std::set<std::string>& hosts);
// Firefox/Zen "favicons.sqlite": moz_pages_w_icons + moz_icons_to_pages + moz_icons(data, width).
Favicons ReadFirefoxFavicons(const std::filesystem::path& db, const std::set<std::string>& hosts);
// Lower-case DNS host of an http(s) URL, or empty.
std::string UrlHost(std::string_view url);
// The PNG itself, or a PNG image embedded in an ICO (the size closest to 32 px).
std::optional<std::string> IconPng(std::string_view bytes);
}  // namespace red::import
