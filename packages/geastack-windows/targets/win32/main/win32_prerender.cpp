// SPDX-License-Identifier: Apache-2.0
#include "win32_prerender.h"
#include "win32_widgets.h"

#include "app.h"
#include "canvas.h"
#include "css/declarative.h"
#include "css/engine.h"
#include "display.h"
#include "host/timers.h"
#include "platform/prerender_worker.h"
#include "ui/style.h"
#include "ui/tree_internal.h"

#include <gdiplus.h>
#include <shellapi.h>

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cwchar>
#include <cstdlib>
#include <stdexcept>
#include <vector>

namespace gea::win32 {
namespace {

using platform::prerender::Request;

void writePrerenderPng(const Request &request)
{
	// Built-in GDI+ PNG encoder. A bitmap over memory requires no HWND or DC.
	constexpr CLSID pngEncoder{0x557cf406, 0x1a04, 0x11d3, {0x9a, 0x73, 0x00, 0x00, 0xf8, 0x1e, 0xf3, 0x2e}};
	const auto *canvas = platform::display::Display::canvas();
	const auto *source = canvas->pixels();
	const int stride = canvas->strideBytes() / sizeof(*source);
	std::vector<std::uint8_t> bgra(static_cast<std::size_t>(request.width) * request.height * 4);
	auto *target = bgra.data();

	for (int y = 0; y < request.height; ++y) {
		for (int x = 0; x < request.width; ++x) {
			int r, g, b, a;
			framework::graphics::pixel::unpackNative8(source[static_cast<std::size_t>(y) * stride + x], &r, &g, &b, &a);
			*target++ = static_cast<std::uint8_t>(b);
			*target++ = static_cast<std::uint8_t>(g);
			*target++ = static_cast<std::uint8_t>(r);
			*target++ = static_cast<std::uint8_t>(a);
		}
	}

	Gdiplus::Bitmap bitmap(request.width, request.height, request.width * 4, PixelFormat32bppARGB, bgra.data());
	if (bitmap.Save(toWide(request.out).c_str(), &pngEncoder, nullptr) != Gdiplus::Ok) {
		throw std::runtime_error("Could not write the PNG");
	}
}

void loadPage(const Request &request)
{
	if (!preparePrerenderDisplay(request.width, request.height)) {
		throw std::runtime_error("Could not allocate the headless framebuffer");
	}
	if (!request.input.empty() && _putenv_s("GEA_PRERENDER_INPUT", request.input.c_str()) != 0) {
		throw std::runtime_error("Could not set the prerender input");
	}

	setPrerenderTime(0);
	host::resetAnimationFrameCallbacks();
	host::resetScheduledTimers();
	host::runAnimationFrameCallbacks(0);
	framework::app::Application::init(request.width, request.height, request.dpr);
	if (embedded::ui::Tree::instance().mountedRoot() < 0) {
		throw std::runtime_error("Could not mount the page");
	}
	css::DeclarativeAnimations::scanAndStart(0);
}

void paintFrame(int timeMs)
{
	setPrerenderTime(timeMs);
	embedded::ui::StyleSheet::instance().startCssAnimations(timeMs);
	css::AnimationEngine::instance().tick(static_cast<std::uint32_t>(timeMs));
	framework::app::Application::frame(timeMs);
}

double parseNumber(const wchar_t *argument)
{
	wchar_t *end = nullptr;
	const double value = std::wcstod(argument, &end);
	if (end == argument || *end != L'\0') throw std::invalid_argument("Invalid prerender number");
	return value;
}

int runPrerenderCommand(int argc, wchar_t **argv)
{
	if (argc == 2 && std::wcscmp(argv[1], L"--gea-prerender-worker") == 0) {
		return platform::prerender::runWorker([](const Request &request, bool reuse) {
			if (!reuse) loadPage(request);
			paintFrame(0);
			writePrerenderPng(request);
		});
	}
	if (argc != 8) {
		std::fputs("Expected --gea-prerender <out> <width> <height> <dpr> <fps> <timestamp-ms>\n", stderr);
		return 2;
	}

	Request request;
	request.out = fromWide(argv[2]);
	const double width = parseNumber(argv[3]);
	const double height = parseNumber(argv[4]);
	request.dpr = parseNumber(argv[5]);
	const double fps = parseNumber(argv[6]);
	const double timestamp = parseNumber(argv[7]);
	if (request.out.empty() || !platform::prerender::validViewport(width, height, request.dpr) ||
	    !(fps >= 1 && fps <= 1000 && std::trunc(fps) == fps) ||
	    !(timestamp >= 0 && timestamp <= INT_MAX && std::trunc(timestamp) == timestamp)) {
		std::fputs("Invalid prerender path, viewport, frame rate or timestamp\n", stderr);
		return 2;
	}

	request.width = static_cast<int>(width);
	request.height = static_cast<int>(height);
	loadPage(request);
	const int frameRate = static_cast<int>(fps);
	const int targetMs = static_cast<int>(timestamp);
	for (std::int64_t frame = 0; ; ++frame) {
		const std::int64_t tick = (frame * 1000 + frameRate / 2) / frameRate;
		const int timeMs = static_cast<int>(std::min<std::int64_t>(tick, targetMs));
		paintFrame(timeMs);
		if (timeMs == targetMs) break;
	}
	writePrerenderPng(request);
	return 0;
}

}  // namespace

int runPrerender()
{
	int argc = 0;
	wchar_t **argv = CommandLineToArgvW(GetCommandLineW(), &argc);
	if (!argv) return 1;

	Gdiplus::GdiplusStartupInput input;
	ULONG_PTR token = 0;
	if (Gdiplus::GdiplusStartup(&token, &input, nullptr) != Gdiplus::Ok) {
		LocalFree(argv);
		return 1;
	}
	usePrerenderFonts();

	int result = 1;
	try {
		result = runPrerenderCommand(argc, argv);
	} catch (const std::exception &error) {
		std::fprintf(stderr, "%s\n", error.what());
	}
	Gdiplus::GdiplusShutdown(token);
	LocalFree(argv);
	return result;
}

}  // namespace gea::win32
