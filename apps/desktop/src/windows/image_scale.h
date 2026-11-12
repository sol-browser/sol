#pragma once
#include <optional>
#include <string>

namespace red {
// Small Windows Imaging Component helpers for page pictures. Any thread; each call initialises COM for
// its own use. Inputs and outputs are encoded image bytes (PNG or JPEG), never pixels.
//
// Page pictures are always taken at the page's own size (Page.captureScreenshot without a clip): in the
// pinned Chromium a clipped or scaled screenshot resizes the page's render widget until it answers, which
// shows as the page shrinking into the top-left corner for a moment (docs/page-transitions.md). Scaling
// and cropping happen here instead.

// The picture scaled down to at most |max_width| pixels wide (never up), as JPEG of |quality| (0..1).
std::optional<std::string> ScaledJpeg(const std::string& encoded, unsigned max_width, float quality);
// A rectangle of the picture given as fractions of its size (0..1), as PNG. Empty when nothing is left.
std::optional<std::string> CroppedPng(const std::string& encoded, double left, double top, double width, double height);
// The picture as a packed Windows bitmap for the clipboard (CF_DIB): a BITMAPINFOHEADER, then 32-bit BGRA rows
// bottom-up. Empty for pictures over |max_pixels|.
std::optional<std::string> PackedDib(const std::string& encoded, unsigned long long max_pixels);
}  // namespace red
