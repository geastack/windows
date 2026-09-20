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

gea::framework::graphics::Canvas g_canvas;
std::uint16_t *g_framebuffer = nullptr;
int g_canvas_w = gea::platform::display::kWidth;
int g_canvas_h = gea::platform::display::kHeight;
int g_brightness = 100;
std::uint8_t g_alpha = 255;
int g_flush_rows = 0;
int g_flush_depth = 0;

void ensure_canvas()
{
	if (g_framebuffer) return;
	g_framebuffer = static_cast<std::uint16_t *>(std::calloc(static_cast<size_t>(g_canvas_w) * static_cast<size_t>(g_canvas_h), sizeof(std::uint16_t)));
	g_canvas.bindPixels(g_framebuffer, g_canvas_w, g_canvas_h);
}

}  // namespace

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

void Display::clear() {}
void Display::clearNoFlush() {}
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
int Display::flushBufferBytes() { return kWidth * g_flush_rows * 2 * g_flush_depth; }

void Display::pushClip(int, int, int, int) {}
void Display::popClip() {}
void Display::resetClip() {}
void Display::setAlpha(uint8_t a) { g_alpha = a; }
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

void Display::fillRect(int, int, int, int, uint16_t) {}
void Display::scrollRect(int, int, int, int, int, int) {}
void Display::resetScrollRegion() {}
void Display::strokeRect(int, int, int, int, uint16_t) {}
void Display::fillCircle(int, int, int, uint16_t) {}
void Display::strokeCircle(int, int, int, uint16_t) {}
void Display::drawLine(int, int, int, int, uint16_t) {}
void Display::drawArc(int, int, int, int, int, uint16_t) {}
void Display::fillTriangle(int, int, int, int, int, int, uint16_t) {}
void Display::drawText(const char *, int, int, uint16_t, float) {}
void Display::drawTextFont(const char *, int, int, uint16_t, int) {}
void Display::drawTextFontFamily(const char *, int, int, uint16_t, int, int) {}
void Display::setPixel(int, int, uint16_t) {}
void Display::fillRoundedRect(int, int, int, int, int, int, int, int, uint16_t) {}
void Display::fillRoundedRectBoxesRgb565(const int16_t *, const int16_t *, int, int, int, int, int, int, int, const uint16_t *) {}
void Display::strokeRoundedRect(int, int, int, int, int, int, int, int, int, uint16_t) {}
void Display::blitImage(const gea::framework::graphics::pixel::native_t *, const uint8_t *, int, int, int, int) {}
void Display::blitImageScaled(const gea::framework::graphics::pixel::native_t *, const uint8_t *, int, int, int, int, int, int) {}
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
	std::memcpy(dst, g_framebuffer, static_cast<std::size_t>(pixels) * sizeof(std::uint16_t));
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
	g_framebuffer = static_cast<std::uint16_t *>(std::calloc(static_cast<size_t>(w) * static_cast<size_t>(h), sizeof(std::uint16_t)));
	g_canvas.bindPixels(g_framebuffer, w, h);
	gea::win32::resizeMainWindowClient(w, h);
}
bool autoRotateAllowed() { return false; }

}  // namespace gea::platform::display
