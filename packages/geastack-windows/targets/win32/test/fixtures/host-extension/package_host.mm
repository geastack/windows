// A package's host source: a .mm file, which this target compiles as C++.
#ifndef __cplusplus
#error a .mm source must be compiled as C++ on Windows
#endif
#include "gea_win32_host.h"

#include <cstdio>

namespace {
int attach(HWND, RECT area, double scale)
{
	std::printf("attach %ldx%ld at %.2f\n", area.right - area.left, area.bottom - area.top, scale);
	gea_win32_set_frame_pacing(GEA_WIN32_FRAME_PACING_PRESENT);
	return 1;
}
int beginFrame()
{
	std::printf("begin\n");
	return 1;
}
void endFrame() { std::printf("end\n"); }

const gea_win32_host_extension kFixture = {GEA_WIN32_HOST_EXTENSION_VERSION, "fixture surface", attach, nullptr, beginFrame, endFrame, nullptr, nullptr};
const gea_win32_host_extension kFuture = {GEA_WIN32_HOST_EXTENSION_VERSION + 1, "future surface", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};
const bool kRegistered = (gea_win32_register_host_extension(&kFixture), gea_win32_register_host_extension(&kFuture), true);
}  // namespace
