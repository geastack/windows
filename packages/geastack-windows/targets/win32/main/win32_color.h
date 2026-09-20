// SPDX-License-Identifier: Apache-2.0
// Style colours arrive as the engine's native pixel format (RGB565 on this
// target); every painter here wants 8-bit channels.
#pragma once

#include "pixel.h"
#include "win32_widgets.h"

#include <cstdint>

namespace gea::win32 {

inline Color colorFromStyle(std::uint16_t rgb565, std::uint8_t alpha = 255)
{
	int r = 0, g = 0, b = 0;
	gea::framework::graphics::pixel::unpackRgb565(rgb565, &r, &g, &b);
	return Color::rgb((r * 255 + 15) / 31, (g * 255 + 31) / 63, (b * 255 + 15) / 31, alpha);
}

}  // namespace gea::win32
