// SPDX-License-Identifier: Apache-2.0
// Sensors, memory diagnostics, the mouse-backed touchscreen and battery.
#include "imu.h"
#include "memory.h"
#include "power.h"
#include "touch.h"
#include "win32_press_bridge.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstdint>

// A desktop has no IMU; tilt reads flat.
namespace gea::platform::sensors {

void Accelerometer::init() {}
void Accelerometer::close() {}
void Accelerometer::calibrateBias() {}
int Accelerometer::tiltX() { return 0; }
int Accelerometer::tiltY() { return 0; }
double Accelerometer::accelerationX() { return 0.0; }
double Accelerometer::accelerationY() { return 0.0; }
double Accelerometer::accelerationZ() { return 9.80665; }
double Accelerometer::gyroscopeX() { return 0.0; }
double Accelerometer::gyroscopeY() { return 0.0; }
double Accelerometer::gyroscopeZ() { return 0.0; }
void Accelerometer::setWebTilt(int, int) {}

}  // namespace gea::platform::sensors

// The diagnostics views show embedded heap metrics; report the machine's real
// available memory where the concept maps, zero where it does not.
namespace gea::platform::memory {

std::uint32_t Memory::internalFree()
{
	MEMORYSTATUSEX status{};
	status.dwLength = sizeof(status);
	if (!GlobalMemoryStatusEx(&status)) return 0;
	return static_cast<std::uint32_t>(status.ullAvailPhys > 0xFFFFFFFFull ? 0xFFFFFFFFull : status.ullAvailPhys);
}
std::uint32_t Memory::internalLargestFreeBlock() { return internalFree(); }
std::uint32_t Memory::internalMinimumFree() { return 0; }
std::uint32_t Memory::psramFree() { return 0; }
std::uint32_t Memory::currentTaskStackHighWaterMark() { return 0; }
std::uint32_t Memory::geaMainStackBytes() { return 0; }
std::uint32_t Memory::geaInitStackBytes() { return 0; }
std::uint32_t Memory::appFrameStackWords() { return 0; }
std::uint32_t Memory::appFrameStackBytes() { return 0; }
std::uint32_t Memory::displayFlushConfiguredRows() { return 0; }
std::uint32_t Memory::displayFlushConfiguredDepth() { return 0; }
std::uint32_t Memory::displayFlushBufferMaxBytes() { return 0; }
std::uint32_t Memory::displayFlushRows() { return 0; }
std::uint32_t Memory::displayFlushDepth() { return 0; }
std::uint32_t Memory::displayFlushBufferBytes() { return 0; }
void *Memory::reserveInternalDma(std::size_t) { return nullptr; }
void Memory::releaseInternalDma(void *) {}

}  // namespace gea::platform::memory

// Touch is the mouse: the canvas widget feeds this cache so immediate-mode
// apps polling read()/readCached() and the framework's Move re-reads see the
// real pointer. Coordinates are panel-native.
namespace {
bool g_touching = false;
int g_touch_x = 0;
int g_touch_y = 0;
}  // namespace

namespace gea::platform::touch {

void Touchscreen::setObserver(Observer) {}
bool Touchscreen::init() { return true; }
int Touchscreen::read(int *x, int *y)
{
	if (x) *x = g_touch_x;
	if (y) *y = g_touch_y;
	return g_touching ? 1 : 0;
}
int Touchscreen::readCached(int *x, int *y)
{
	if (x) *x = g_touch_x;
	if (y) *y = g_touch_y;
	return g_touching ? 1 : 0;
}
void Touchscreen::consumeLatestMove(int *x, int *y)
{
	if (x) *x = g_touch_x;
	if (y) *y = g_touch_y;
}

}  // namespace gea::platform::touch

extern "C" void gea_win32_touch_set_state(int touching, int x, int y)
{
	g_touching = touching != 0;
	g_touch_x = x;
	g_touch_y = y;
}

namespace gea::platform::power {

bool Power::init() { return true; }

int Power::batteryPercent()
{
	SYSTEM_POWER_STATUS status{};
	if (!GetSystemPowerStatus(&status)) return 100;
	// No battery (a desktop) reports unknown; "on external power" is the
	// honest answer the embedded callers expect there.
	if ((status.BatteryFlag & 128) || status.BatteryLifePercent == 255) return 100;
	return status.BatteryLifePercent;
}

}  // namespace gea::platform::power
