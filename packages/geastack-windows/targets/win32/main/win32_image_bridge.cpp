// SPDX-License-Identifier: Apache-2.0
#include "win32_image_bridge.h"

#include "image.h"
#include "pixel.h"

#include <cstring>
#include <unordered_map>

namespace gea::win32 {

namespace {
struct CachedBitmap {
	HBITMAP bitmap = nullptr;
	int width = 0;
	int height = 0;
	const void *pixels = nullptr;  // source pointer, to detect a rebound slot
	int frame = -1;
};
std::unordered_map<int, CachedBitmap> &cache()
{
	static std::unordered_map<int, CachedBitmap> entries;
	return entries;
}
}  // namespace

HBITMAP bitmapForImageId(int imageId, int *outWidth, int *outHeight)
{
	using namespace gea::framework::graphics;
	if (outWidth) *outWidth = 0;
	if (outHeight) *outHeight = 0;
	if (imageId < 0) return nullptr;
	auto &store = ImageStore::instance();
	const int width = store.width(imageId);
	const int height = store.height(imageId);
	const std::uint16_t *source = store.currentPixels(imageId);
	const std::uint8_t *alpha = store.currentAlpha(imageId);
	if (width <= 0 || height <= 0 || !source) return nullptr;
	CachedBitmap &entry = cache()[imageId];
	const int frame = store.isAnimated(imageId) ? store.currentFrame(imageId) : 0;
	if (!entry.bitmap || entry.width != width || entry.height != height || entry.pixels != source || entry.frame != frame) {
		if (!entry.bitmap || entry.width != width || entry.height != height) {
			if (entry.bitmap) DeleteObject(entry.bitmap);
			void *bits = nullptr;
			entry.bitmap = createBgraBitmap(width, height, &bits);
			entry.width = width;
			entry.height = height;
		}
		BITMAP info{};
		GetObjectW(entry.bitmap, sizeof(info), &info);
		auto *dst = static_cast<std::uint8_t *>(info.bmBits);
		const int count = width * height;
		for (int i = 0; i < count; i++) {
			int r = 0, g = 0, b = 0;
			pixel::unpackRgb565(source[i], &r, &g, &b);
			const int a = alpha ? alpha[i] : 255;
			// Premultiplied BGRA for AlphaBlend.
			dst[i * 4 + 0] = static_cast<std::uint8_t>(((b * 255 + 15) / 31) * a / 255);
			dst[i * 4 + 1] = static_cast<std::uint8_t>(((g * 255 + 31) / 63) * a / 255);
			dst[i * 4 + 2] = static_cast<std::uint8_t>(((r * 255 + 15) / 31) * a / 255);
			dst[i * 4 + 3] = static_cast<std::uint8_t>(a);
		}
		entry.pixels = source;
		entry.frame = frame;
	}
	if (outWidth) *outWidth = width;
	if (outHeight) *outHeight = height;
	return entry.bitmap;
}

void dropImageBitmaps()
{
	for (auto &entry : cache()) {
		if (entry.second.bitmap) DeleteObject(entry.second.bitmap);
	}
	cache().clear();
}

}  // namespace gea::win32
