// SPDX-License-Identifier: Apache-2.0
// What the rest of the target (the Controls bridge, the shell, the display
// hook) may ask of the main window.
#pragma once

#include "win32_widgets.h"

#include <functional>
#include <string>

namespace gea::win32 {

class Toolbar;

HWND mainWindow();
// Layout px -> device px for the main window's monitor.
double windowScale();
// Resizes the client area to `width` x `height` layout px (orientation hook).
void resizeMainWindowClient(int width, int height);
// Asks for a frame as soon as the message loop is idle.
void requestFrame();
void quitApplication();
void setMainWindowTitle(const std::wstring &title);
// "dark" | "light" | "system"
void setMainWindowAppearance(const std::string &appearance);
void setMainWindowBackground(Color color);
// The client area content may use, below the toolbar strip, in device px.
RECT contentArea();

// Windows-native apps (programs written against @geastack/windows/Controls)
// hand the target a root window and, optionally, a toolbar. The root fills
// the content area; the layout callback runs each frame with that area.
void installNativeRootWindow(HWND root);
void installNativeToolbar(Toolbar *toolbar);
void setNativeLayoutCallback(std::function<void(const RECT &)> callback);

// Runs a shell command and returns its output (win32_device_control.cpp).
std::string runShellCommandBlocking(const std::string &command);

}  // namespace gea::win32
