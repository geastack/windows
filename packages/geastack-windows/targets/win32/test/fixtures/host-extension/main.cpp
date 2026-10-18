// Stands in for the target's frame loop: runs every registered extension.
#include "win32_host_extensions.h"

#include <cstdio>

extern "C" int package_support_value(void);

int main()
{
	for (const auto *extension : gea::win32::hostExtensions()) {
		std::printf("extension %s\n", extension->name);
		if (extension->attach && !extension->attach(nullptr, RECT{0, 0, 640, 480}, 1.5)) return 1;
		if (extension->begin_frame && extension->begin_frame() && extension->end_frame) extension->end_frame();
	}
	std::printf("pacing %d\n", gea::win32::framePacing());
	std::printf("support %d\n", package_support_value());
	return 0;
}
