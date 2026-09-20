// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "win32_widgets.h"

#include <cstdint>
#include <string>

namespace gea::win32 {

// The DPI scale the renderer draws at; fonts are created at font_size * scale
// device pixels and measurements are reported back in layout (logical) px.
void setFontScale(double scale);
double fontScale();

// An HFONT for the framework's font id at `sizePx` layout pixels and the
// given CSS weight (0 = regular). Cached for the process lifetime.
HFONT fontForId(int fontId, int sizePx, int weight = 0);
// A font by face name (the Controls bridge resolves family strings directly).
HFONT fontForFamily(const std::wstring &family, int sizePx, int weight = 0);
// Maps a CSS family name (or Apple system-font spelling) to a Windows face.
std::wstring faceForFamily(const std::string &family);
// Registers every .ttf/.otf under `directory` privately for this process and
// keeps their bytes for the engine's runtime canvas rasterizer.
int registerFontDirectory(const std::wstring &directory);
// The framework family id for a CSS family name (creates one on first use).
int familyIdFor(const std::string &family);
// Height in layout px of a line of `font` (ascent + descent).
int fontLineHeight(HFONT font);
// Measures `text` wrapped at `maxWidth` layout px (0 = unbounded).
void measureText(const std::wstring &text, HFONT font, int maxWidth, bool singleLine, int *outWidth, int *outHeight);
// Drops every cached font and measurement (DPI change).
void resetFontCaches();

}  // namespace gea::win32
