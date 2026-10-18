// SPDX-License-Identifier: Apache-2.0
// Host extensions: native code a package (or an app) compiles into a Windows
// build to own part of the main window -- typically a rendering surface such
// as a GL or GPU swap chain that the generated program draws into.
//
// The target calls every registered extension at fixed points of its life
// cycle; an extension registers itself from a static initializer:
//
//   static const gea_win32_host_extension kSurface = {
//     GEA_WIN32_HOST_EXTENSION_VERSION, "my-surface", attach, layout, begin, end, message, detach };
//   static const bool kRegistered = (gea_win32_register_host_extension(&kSurface), true);
//
// Every callback may be null. Areas are client-area rectangles of the main
// window in device pixels; `scale` is device pixels per layout pixel.
#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GEA_WIN32_HOST_EXTENSION_VERSION 1u

typedef struct gea_win32_host_extension {
	unsigned version;  // GEA_WIN32_HOST_EXTENSION_VERSION
	const char *name;  // for diagnostics
	// Once, after the main window exists and before the app's top level runs.
	// Returning zero stops the app with an error naming the extension.
	int (*attach)(HWND window, RECT contentArea, double scale);
	// The content area or the scale changed.
	void (*layout)(RECT contentArea, double scale);
	// Around the app's initialization and around every frame. A zero return
	// from begin_frame skips that frame; end_frame runs whenever begin_frame ran.
	int (*begin_frame)(void);
	void (*end_frame)(void);
	// Every message of the main window, before the target handles it. A
	// nonzero return means handled, with *result as the window procedure's result.
	int (*window_message)(HWND window, UINT message, WPARAM wParam, LPARAM lParam, LRESULT *result);
	// Once, after the message loop ends.
	void (*detach)(void);
} gea_win32_host_extension;

void gea_win32_register_host_extension(const gea_win32_host_extension *extension);

// How the target paces its frames. TIMER, the default, runs one frame per
// display refresh from a high-resolution timer and waits for input in between.
// PRESENT runs frames back to back while the window is visible: for an
// extension whose presentation paces the loop itself (a swap chain presenting
// with vsync blocks until the next refresh) or that renders unthrottled.
#define GEA_WIN32_FRAME_PACING_TIMER 0
#define GEA_WIN32_FRAME_PACING_PRESENT 1
void gea_win32_set_frame_pacing(int pacing);

#ifdef __cplusplus
}
#endif
