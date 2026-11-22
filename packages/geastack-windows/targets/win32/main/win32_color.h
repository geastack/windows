// SPDX-License-Identifier: Apache-2.0
// Convert framework pixels into the channels expected by GDI.
#pragma once

#include "pixel.h"
#include "win32_widgets.h"

#include <cstdint>

namespace gea::win32 {

inline Color colorFromStyle(gea::framework::graphics::pixel::native_t color, std::uint8_t alpha = 255)
{
	using namespace gea::framework::graphics;
	int r = 0, g = 0, b = 0;
#if GEA_EMBEDDED_PIXEL_FORMAT == GEA_PIXEL_RGB565
	pixel::unpackRgb565(color, &r, &g, &b);
	return Color::rgb((r * 255 + 15) / 31, (g * 255 + 31) / 63, (b * 255 + 15) / 31, alpha);
#else
	int pixelAlpha = 0;
	pixel::unpackNative8(color, &r, &g, &b, &pixelAlpha);
	return Color::rgb(r, g, b, alpha);
#endif
}

}  // namespace gea::win32
