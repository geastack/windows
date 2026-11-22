// SPDX-License-Identifier: Apache-2.0
// gea::platform::display::Display for a target that never rasterizes: the
// node tree is rendered through real child windows, so every drawing call is
// inert. The canvas surface still exists (canvas elements draw into their own
// pixel buffers, not this one), `present` returns false so the framework
// replays canvas batches onto the element's surface, and the orientation hook
// resizes the window so `setOrientation('landscape')` produces the viewport
// the app laid itself out for.

#include "canvas.h"
#include "display.h"
#include "pixel.h"
#include "win32_prerender.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace gea::win32 {
// Defined in win32_main.cpp: resizes the main window's client area.
void resizeMainWindowClient(int width, int height);
// Defined in win32_renderer.cpp: the logical viewport for the current orientation.
void orientationLogicalSize(int *width, int *height);
}

// The rotation hooks are declared weak in host/display_orientation.h. A TU
// that includes that header and DEFINES them turns the definitions weak too,
// and on COFF a weak definition and another TU's weak reference resolve to
// two different fallbacks that lld reports as a duplicate symbol. So this TU
// declares them itself, plainly, and stays away from the header.
namespace gea::framework::display {
enum class DisplayOrientation;
}
namespace gea::platform::display {
void applyOrientation(gea::framework::display::DisplayOrientation orientation);
bool autoRotateAllowed();
}

namespace {

namespace pixel = gea::framework::graphics::pixel;

gea::framework::graphics::Canvas g_canvas;
pixel::native_t *g_framebuffer = nullptr;
int g_canvas_w = gea::platform::display::kWidth;
int g_canvas_h = gea::platform::display::kHeight;
int g_brightness = 100;
std::uint8_t g_alpha = 255;
int g_flush_rows = 0;
int g_flush_depth = 0;
bool g_prerender = false;

void ensure_canvas()
{
	if (g_framebuffer) return;
	g_framebuffer = static_cast<pixel::native_t *>(std::calloc(static_cast<size_t>(g_canvas_w) * static_cast<size_t>(g_canvas_h), sizeof(pixel::native_t)));
	g_canvas.bindPixels(g_framebuffer, g_canvas_w, g_canvas_h);
}

}  // namespace

bool gea::win32::preparePrerenderDisplay(int width, int height)
{
	const auto pixels = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
	auto *framebuffer = static_cast<pixel::native_t *>(std::calloc(pixels, sizeof(*g_framebuffer)));
	if (!framebuffer) return false;
	std::free(g_framebuffer);
	g_framebuffer = framebuffer;
	g_canvas_w = width;
	g_canvas_h = height;
	g_canvas.bindPixels(g_framebuffer, width, height);
	g_prerender = true;
	return true;
}

namespace gea::platform::display {

bool Display::init()
{
	ensure_canvas();
	return true;
}
bool Display::start() { return true; }

gea::framework::graphics::Canvas *Display::canvas()
{
	ensure_canvas();
	return &g_canvas;
}

void Display::clear() { clearNoFlush(); }
void Display::clearNoFlush()
{
	if (g_prerender) g_canvas.clear(gea::framework::graphics::pixel::nativeColor(0, 0, 0));
}
void Display::print(const char *) {}
void Display::flush() {}
void Display::flushRects(const DisplayFlushRect *, int, bool) {}
void Display::flushRectsRasterized(const DisplayFlushRect *, int, DisplayStreamRasterFn, void *, bool) {}
void Display::rebindCanvasToFramebuffer()
{
	ensure_canvas();
	g_canvas.bindPixels(g_framebuffer, g_canvas_w, g_canvas_h);
}
bool Display::streamRect(int, int, int, int, DisplayStreamRasterFn, void *) { return false; }

// False: there is no batched hardware present here. Returning true would make
// the canvas context believe its recorded commands were drawn and skip the
// replay onto the element's own surface, freezing canvas apps on frame one.
bool Display::present(const DisplayPresentCommand *, int) { return false; }

void Display::setFlushConfig(int chunk_rows, int queue_depth)
{
	g_flush_rows = chunk_rows;
	g_flush_depth = queue_depth;
}
int Display::flushChunkRows() { return g_flush_rows; }
int Display::flushQueueDepth() { return g_flush_depth; }
int Display::flushBufferBytes() { return kWidth * g_flush_rows * sizeof(pixel::native_t) * g_flush_depth; }

void Display::pushClip(int x, int y, int w, int h) { if (g_prerender) g_canvas.pushClip(x, y, w, h); }
void Display::popClip() { if (g_prerender) g_canvas.popClip(); }
void Display::resetClip() { if (g_prerender) g_canvas.resetClip(); }
void Display::setAlpha(uint8_t a)
{
	g_alpha = a;
	if (g_prerender) g_canvas.setGlobalAlpha(a);
}
uint8_t Display::alpha() { return g_alpha; }
int Display::brightness() { return g_brightness; }
void Display::setBrightness(int b) { g_brightness = std::clamp(b, 0, 100); }

void Display::setVSync(bool) {}
void Display::invalidate() {}
bool Display::vsyncEnabled() { return false; }
void Display::vsyncWaitForFrame() {}

void Display::clip(int *x0, int *y0, int *x1, int *y1)
{
	if (x0) *x0 = 0;
	if (y0) *y0 = 0;
	if (x1) *x1 = g_canvas_w - 1;
	if (y1) *y1 = g_canvas_h - 1;
}

void Display::fillRect(int x, int y, int w, int h, pixel::native_t color) { if (g_prerender) g_canvas.fillRect(x, y, w, h, color); }
void Display::scrollRect(int x, int y, int w, int h, int dx, int dy) { if (g_prerender) g_canvas.scrollRect(x, y, w, h, dx, dy); }
void Display::resetScrollRegion() { if (g_prerender) g_canvas.setScrollRegion(0, 0, 0); }
void Display::strokeRect(int x, int y, int w, int h, pixel::native_t color) { if (g_prerender) g_canvas.strokeRect(x, y, w, h, color); }
void Display::fillCircle(int x, int y, int r, pixel::native_t color) { if (g_prerender) g_canvas.fillCircle(x, y, r, color); }
void Display::strokeCircle(int x, int y, int r, pixel::native_t color) { if (g_prerender) g_canvas.strokeCircle(x, y, r, color); }
void Display::drawLine(int x0, int y0, int x1, int y1, pixel::native_t color) { if (g_prerender) g_canvas.drawLine(x0, y0, x1, y1, color); }
void Display::drawArc(int x, int y, int r, int start, int end, pixel::native_t color) { if (g_prerender) g_canvas.drawArc(x, y, r, start, end, color); }
void Display::fillTriangle(int x0, int y0, int x1, int y1, int x2, int y2, pixel::native_t color) {
	if (g_prerender) g_canvas.fillTriangle(x0, y0, x1, y1, x2, y2, color);
}
void Display::drawText(const char *text, int x, int y, pixel::native_t color, float scale) {
	if (g_prerender) g_canvas.drawText(text, x, y, color, scale);
}
void Display::drawTextFont(const char *text, int x, int y, pixel::native_t color, int fontId) {
	if (g_prerender) g_canvas.drawTextFont(text, x, y, color, fontId);
}
void Display::drawTextFontFamily(const char *text, int x, int y, pixel::native_t color, int family, int size) {
	if (g_prerender) g_canvas.drawTextFontFamily(text, x, y, color, family, size);
}
void Display::drawTextFontFamily(const char *text, int x, int y, pixel::native_t color, int family, int size, int weight, int stretch) {
	if (g_prerender) g_canvas.drawTextFontFamily(text, x, y, color, family, size, weight, stretch);
}
void Display::setPixel(int x, int y, pixel::native_t color) { if (g_prerender) g_canvas.fillRect(x, y, 1, 1, color); }
void Display::fillRoundedRect(int x, int y, int w, int h, int tl, int tr, int br, int bl, pixel::native_t color) {
	if (g_prerender) g_canvas.fillRoundedRect(x, y, w, h, tl, tr, br, bl, color);
}
void Display::fillRoundedRectBoxesRgb565(const int16_t *xs, const int16_t *ys, int count, int w, int h, int tl, int tr, int br, int bl, const pixel::native_t *colors) {
	if (g_prerender) g_canvas.fillRoundedRectBoxesRgb565(xs, ys, count, w, h, tl, tr, br, bl, colors);
}
void Display::strokeRoundedRect(int x, int y, int w, int h, int tl, int tr, int br, int bl, int lw, pixel::native_t color) {
	if (g_prerender) g_canvas.strokeRoundedRect(x, y, w, h, tl, tr, br, bl, lw, color);
}
void Display::blitImage(const pixel::native_t *pixels, const uint8_t *alpha, int w, int h, int x, int y) {
	if (g_prerender) g_canvas.drawImage(pixels, alpha, w, h, x, y);
}
void Display::blitImageScaled(const pixel::native_t *pixels, const uint8_t *alpha, int w, int h, int x, int y, int dstWidth, int dstHeight) {
	if (g_prerender) g_canvas.drawImage(pixels, alpha, w, h, x, y, dstWidth, dstHeight);
}
void Display::setWorldOverlay(const uint16_t *, int, int, int, int) {}
void Display::setWorldScroll(int) {}
bool Display::framebufferIsPanelDirect() { return false; }
bool Display::panelScanoutSurface(uint16_t **out_buffer, int *out_width, int *out_height)
{
	if (out_buffer) *out_buffer = nullptr;
	if (out_width) *out_width = 0;
	if (out_height) *out_height = 0;
	return false;
}
bool Display::panelDirectTarget(int, int, int, int, uint16_t **, int *, int *, int *, int *, int *, int *, int *, int *) { return false; }
void Display::flipPanelToBack() {}
void Display::flipPanelToBack(uint16_t *) {}
bool Display::copySnapshotRgb565(uint16_t *dst, int pixel_capacity, int *width, int *height, bool)
{
	ensure_canvas();
	const int pixels = g_canvas_w * g_canvas_h;
	if (width) *width = g_canvas_w;
	if (height) *height = g_canvas_h;
	if (!dst || pixel_capacity < pixels) return false;
	pixel::copyNativeToRgb565(dst, g_framebuffer, pixels);
	return true;
}
bool Display::copySnapshotPacked(uint8_t *, int, int *width, int *height, bool)
{
	if (width) *width = g_canvas_w;
	if (height) *height = g_canvas_h;
	return false;
}
int Display::countNonBlackPixels(bool)
{
	ensure_canvas();
	int count = 0;
	for (int i = 0; i < g_canvas_w * g_canvas_h; i++) {
		if (g_framebuffer[i] != 0) count++;
	}
	return count;
}
void Display::reserveInternal(std::size_t) {}
void Display::applyPendingInternalReserve() {}
void Display::flushStatsRead(std::int64_t *total_us, int *call_count, int *pixel_count)
{
	if (total_us) *total_us = 0;
	if (call_count) *call_count = 0;
	if (pixel_count) *pixel_count = 0;
}
DisplayFlushPerfStats Display::flushPerfStatsRead() { return DisplayFlushPerfStats{}; }
void Display::presentPathDebug(int *calls, int *direct, int *general, int *rejected, int *tileShapeFailKind, int *tileShapeFailType, int *tileShapeFailCount)
{
	if (calls) *calls = 0;
	if (direct) *direct = 0;
	if (general) *general = 0;
	if (rejected) *rejected = 0;
	if (tileShapeFailKind) *tileShapeFailKind = 0;
	if (tileShapeFailType) *tileShapeFailType = 0;
	if (tileShapeFailCount) *tileShapeFailCount = 0;
}
void Display::landFrameDebug(int *total, int *align, int *raster, int *text, int *flip)
{
	if (total) *total = 0;
	if (align) *align = 0;
	if (raster) *raster = 0;
	if (text) *text = 0;
	if (flip) *flip = 0;
}
void Display::landPanDebug(int *detect, int *kick, int *strips, int *wait, int *interior)
{
	if (detect) *detect = 0;
	if (kick) *kick = 0;
	if (strips) *strips = 0;
	if (wait) *wait = 0;
	if (interior) *interior = 0;
}
void Display::flushStatsReset() {}
const char *Display::flushStageName() { return "idle"; }
int Display::flushStageChunk() { return 0; }
DisplayFlushStageDetail Display::flushStageDetail() { return {}; }

// Defining the rotation hook advertises rotation support; the "panel" is a
// window, so rotating means resizing it to the rotated logical size.
void applyOrientation(gea::framework::display::DisplayOrientation)
{
	int w = 0;
	int h = 0;
	gea::win32::orientationLogicalSize(&w, &h);
	if (w <= 0 || h <= 0 || (w == g_canvas_w && h == g_canvas_h)) return;
	g_canvas_w = w;
	g_canvas_h = h;
	std::free(g_framebuffer);
	g_framebuffer = static_cast<pixel::native_t *>(std::calloc(static_cast<size_t>(w) * static_cast<size_t>(h), sizeof(pixel::native_t)));
	g_canvas.bindPixels(g_framebuffer, w, h);
	gea::win32::resizeMainWindowClient(w, h);
}
bool autoRotateAllowed() { return false; }

}  // namespace gea::platform::display
