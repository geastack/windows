// SPDX-License-Identifier: Apache-2.0
// The registry of host extensions (gea_win32_host.h). Extensions register from
// static initializers, so the list lives in a function-local static that
// exists before any of them runs.
#include "win32_host_extensions.h"

#include <cstdio>

namespace gea::win32 {

std::vector<const gea_win32_host_extension *> &hostExtensions()
{
	static std::vector<const gea_win32_host_extension *> extensions;
	return extensions;
}

namespace {
int g_framePacing = GEA_WIN32_FRAME_PACING_TIMER;
}

int framePacing() { return g_framePacing; }

}  // namespace gea::win32

extern "C" void gea_win32_set_frame_pacing(int pacing)
{
	gea::win32::g_framePacing = pacing == GEA_WIN32_FRAME_PACING_PRESENT ? GEA_WIN32_FRAME_PACING_PRESENT : GEA_WIN32_FRAME_PACING_TIMER;
}

extern "C" void gea_win32_register_host_extension(const gea_win32_host_extension *extension)
{
	if (!extension) return;
	if (extension->version != GEA_WIN32_HOST_EXTENSION_VERSION) {
		std::fprintf(stderr, "[gea-windows] ignoring host extension %s: interface version %u, this target implements %u\n",
		             extension->name ? extension->name : "(unnamed)", extension->version, GEA_WIN32_HOST_EXTENSION_VERSION);
		return;
	}
	gea::win32::hostExtensions().push_back(extension);
}
