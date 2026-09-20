// SPDX-License-Identifier: Apache-2.0
// Monotonic clock + the FrameScheduler surface. The Win32 target drives frames
// from its own message loop, so the scheduler is the same degenerate
// single-threaded implementation the macOS target uses: runFrame calls the
// callback, the event queue is inert.
#include "services/frame_scheduler.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace {

int monotonicMillis()
{
	static LARGE_INTEGER frequency = [] {
		LARGE_INTEGER value{};
		QueryPerformanceFrequency(&value);
		return value;
	}();
	static LARGE_INTEGER start = [] {
		LARGE_INTEGER value{};
		QueryPerformanceCounter(&value);
		return value;
	}();
	LARGE_INTEGER now{};
	QueryPerformanceCounter(&now);
	return static_cast<int>((now.QuadPart - start.QuadPart) * 1000 / frequency.QuadPart);
}

int g_frame_interval_ms = gea::framework::services::FrameScheduler::kDefaultFrameIntervalMs;

}  // namespace

extern "C" int gea_embedded_now_ms(void)
{
	return monotonicMillis();
}

namespace gea::framework::services {

EventQueue FrameScheduler::createEventQueue() { return EventQueue{}; }
EventQueue FrameScheduler::eventQueue() { return EventQueue{}; }
bool FrameScheduler::sendEvent(const gea::framework::events::Event &, int) { return true; }
bool FrameScheduler::receiveEvent(gea::framework::events::Event *) { return false; }
void FrameScheduler::start(EventQueue) {}
void FrameScheduler::runFrame(const FrameCallbacks &callbacks)
{
	if (callbacks.frame) callbacks.frame(monotonicMillis(), callbacks.context);
}
void FrameScheduler::setFrameIntervalMs(int intervalMs)
{
	if (intervalMs < kMinFrameIntervalMs) intervalMs = kMinFrameIntervalMs;
	if (intervalMs > kMaxFrameIntervalMs) intervalMs = kMaxFrameIntervalMs;
	g_frame_interval_ms = intervalMs;
}
int FrameScheduler::frameIntervalMs() { return g_frame_interval_ms; }
void FrameScheduler::setFrameRate(double fps)
{
	if (fps <= 0.0) return;
	setFrameIntervalMs(static_cast<int>(1000.0 / fps + 0.5));
}
double FrameScheduler::frameRate() { return 1000.0 / static_cast<double>(g_frame_interval_ms); }
int FrameScheduler::nowMs() { return monotonicMillis(); }

}  // namespace gea::framework::services
