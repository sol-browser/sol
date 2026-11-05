#pragma once
#include "include/cef_values.h"

namespace red {
// The icons of the browsers Sol can import from (Chrome, Edge, Arc, Zen), taken
// from their own installed programs as Windows shows them, as PNG data URLs
// keyed "chrome", "edge", "arc", "zen". A browser that is not installed, or
// whose icon cannot be read, is left out. UI thread (COM).
CefRefPtr<CefDictionaryValue> InstalledBrowserIcons(int size);
}  // namespace red
