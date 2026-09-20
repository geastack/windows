// SPDX-License-Identifier: Apache-2.0
// The native widget layer of the Win32 target.
//
// A Widget is a real child HWND. This header declares the window classes the
// target registers and the one data model both consumers drive:
//
//   * the gea-tree renderer (win32_renderer.cpp) creates a window only for
//     what needs one (a painted surface, a scroll container, a native
//     control) and paints everything else into the surface through
//     WidgetEvents::onPaintOverlay with the drawText/drawBox primitives here;
//   * the Controls bridge (native/controls.cpp) builds a window per WinView /
//     WinLabel / ... a Windows-native program creates, filling WidgetStyle
//     from the properties it sets.
//
// Neither knows how the other paints. Containers, labels, canvases and images
// are custom classes painted here (GDI+ for shapes, GDI for text); buttons,
// edits, check boxes, sliders and progress bars are the real common controls,
// wrapped so their notifications arrive through the same WidgetEvents.
//
// All geometry is in device pixels; callers apply the DPI scale.

#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef UNICODE
#define UNICODE
#endif
// GDI+ needs the full Windows headers (IStream, PROPID, byte); the build
// defines WIN32_LEAN_AND_MEAN globally for the framework sources.
#ifdef WIN32_LEAN_AND_MEAN
#undef WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <objidl.h>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace gea::win32 {

struct Color {
	std::uint8_t r = 0;
	std::uint8_t g = 0;
	std::uint8_t b = 0;
	std::uint8_t a = 0;  // 0 = not set / transparent
	bool set() const { return a != 0; }
	COLORREF ref() const { return RGB(r, g, b); }
	static Color rgb(int r, int g, int b, int a = 255)
	{
		return Color{static_cast<std::uint8_t>(r), static_cast<std::uint8_t>(g), static_cast<std::uint8_t>(b), static_cast<std::uint8_t>(a)};
	}
};

// `over` composited onto `under` (both opaque results); used to flatten a
// translucent style colour against the colour it will visually sit on.
Color blend(Color under, Color over);

enum class WidgetKind : int {
	View = 0,      // container; paints background/border/radius
	Label,         // static text, click-transparent
	Button,        // native BUTTON (or a styled View when the app paints it)
	TextField,     // single-line EDIT
	TextArea,      // multi-line EDIT
	CheckBox,      // BUTTON | BS_AUTOCHECKBOX
	Slider,        // msctls_trackbar32
	ProgressBar,   // msctls_progress32
	Image,         // custom-painted bitmap
	Canvas,        // custom-painted pixel surface with pointer events
	Scroll,        // vertical scroller hosting a document View
	Divider,       // draggable split divider
};

enum class TextLineBreak : int { WordWrap = 0, TruncateTail = 1, Clip = 2 };

struct WidgetStyle {
	Color background;           // a == 0 -> show the ancestor's background
	Color border;
	int borderWidth = 0;        // device px
	float radius[4] = {0, 0, 0, 0};  // TL, TR, BR, BL device px
	std::uint8_t opacity = 255;
	bool hidden = false;
	bool clipChildren = true;
	// Text (Label, Button, TextField, TextArea, CheckBox)
	std::wstring text;
	std::wstring placeholder;
	HFONT font = nullptr;       // owned by the font registry, never freed here
	Color textColor;            // a == 0 -> system default for the control
	int textAlign = 0;          // 0 left, 1 center, 2 right
	int textDecoration = 0;     // 0 none, 1 underline, 2 line-through
	TextLineBreak lineBreak = TextLineBreak::WordWrap;
	int maxLines = 0;           // 0 = unlimited
	bool symbol = false;        // one icon glyph, centred on its ink rather than its line box
	bool editable = true;
	bool bordered = false;      // native control frame for edits
	// Slider / progress
	double minimum = 0;
	double maximum = 100;
	double value = 0;
	bool indeterminate = false;
	// CheckBox
	bool checked = false;
	// Image
	HBITMAP bitmap = nullptr;   // 32bpp premultiplied BGRA, owned by the image bridge
	int contentMode = 0;        // 0 fill, 1 contain, 2 cover, 3 none/center, 4 scale-down
	Color tint;                 // symbol / template tint (a == 0: none)
	// Scroll
	bool verticalScroller = true;
	bool horizontalScroller = false;
	int contentWidth = 0;       // document size, device px
	int contentHeight = 0;
	// Press feedback for styled buttons
	bool pressHighlight = false;
};

// One painted box: what a View draws for itself. `pressed` darkens the fill
// the way a styled button shows a press.
struct BoxPaint {
	Color fill;
	Color border;
	int borderWidth = 0;
	float radius[4] = {0, 0, 0, 0};
	std::uint8_t opacity = 255;
	bool pressed = false;
};

struct WidgetEvents {
	// Surface hooks (View): paints content over the widget's own background
	// and under its child windows; receives presses the renderer hit-tests
	// against the painted tree (phase 1 down, 2 move, 3 up, 4 cancelled).
	std::function<void(HDC, const RECT &)> onPaintOverlay;
	std::function<void(int phase, int x, int y)> onSurfacePointer;
	std::function<void()> onPressDown;
	std::function<void()> onPressUp;
	std::function<void()> onClick;
	std::function<void(const std::wstring &)> onTextChanged;
	std::function<void(int keyCode)> onKeyDown;      // browser keyCode values
	std::function<void(double)> onValueChanged;      // slider
	std::function<void(bool)> onCheckedChanged;      // check box
	std::function<void(int phase, int x, int y)> onPointer;  // canvas: 1 down, 2 move, 3 up (client px)
	std::function<void(int scrollTop)> onScroll;
	std::function<void(HDC, const RECT &)> onPaintContent;  // canvas/image content painter
	std::function<void(int width, int height)> onResize;
	std::function<void(int deltaX)> onDividerDrag;   // divider: cumulative drag in device px
	std::function<void()> onFocus;
	std::function<void()> onBlur;
};

struct Widget {
	HWND hwnd = nullptr;
	WidgetKind kind = WidgetKind::View;
	WidgetStyle style;
	WidgetEvents events;
	int nodeId = -1;           // gea node this widget renders, -1 for Controls objects
	void *user = nullptr;      // owner-defined
	HWND document = nullptr;   // Scroll: the child that hosts the content
	int scrollTop = 0;         // Scroll: current offset, device px
	int scrollLeft = 0;
	bool pressed = false;
	bool hover = false;
	bool focusable = false;
	bool clickTransparent = false;  // Label: mouse passes through to the parent
	bool styledButton = false;      // Button painted here rather than by the theme
	std::wstring lastText;          // edits: last text we pushed, to avoid caret resets
};

// Process-wide setup: registers the window classes, starts GDI+ and the
// common controls. Idempotent.
void initializeWidgets(HINSTANCE instance);

// Creates a child window of `kind` under `parent` and returns its Widget.
Widget *createWidget(WidgetKind kind, HWND parent);
// The widget record for one of our windows, or nullptr for a foreign HWND.
Widget *widgetFor(HWND hwnd);
// Ancestor walk: the nearest widget at or above `hwnd`.
Widget *nearestWidget(HWND hwnd);
void destroyWidget(Widget *widget);

// Applies a style; repaints only when something visible changed. Edits push
// text into the control when it differs from what the control shows.
void applyWidgetStyle(Widget *widget, const WidgetStyle &style);
// Moves/sizes the window (device px, relative to its parent's client origin).
void setWidgetFrame(Widget *widget, int x, int y, int width, int height);
void setWidgetParent(Widget *widget, HWND parent);
RECT widgetFrame(const Widget *widget);
// Forces a repaint of the widget and, for containers, its subtree.
void invalidateWidget(Widget *widget, bool children = false);
// Scroll: sets the scroll offset (clamped) and repositions the document.
void setWidgetScrollTop(Widget *widget, int scrollTop);
void setWidgetScrollLeft(Widget *widget, int scrollLeft);
// Edits: current text as UTF-16 with \n line endings.
std::wstring widgetText(const Widget *widget);
void focusWidget(Widget *widget);
// The background this widget visually sits on: its own when opaque, else the
// nearest ancestor's, else the window background.
Color effectiveBackground(HWND hwnd);
// Window background used by effectiveBackground when nothing above paints.
void setWindowBackground(Color color);
Color windowBackground();
// Dark-mode theming for the common controls under `hwnd`.
void applyControlTheme(HWND hwnd, bool dark);
bool darkModeEnabled();
void setDarkModeEnabled(bool dark);
// Routes a wheel message to the scroll container under the cursor. Called by
// the message pump so wheel input scrolls whatever is hovered, not focused.
bool routeMouseWheel(MSG &message);
// Wide/UTF-8 helpers shared with the rest of the target.
std::wstring toWide(const std::string &utf8);
std::string fromWide(const std::wstring &wide);
// GDI+ bitmap helpers for the image/canvas painters.
HBITMAP createBgraBitmap(int width, int height, void **bits);
// Paints a 32bpp premultiplied bitmap into `dest` according to `contentMode`.
void paintBitmap(HDC hdc, HBITMAP bitmap, int bitmapWidth, int bitmapHeight, const RECT &dest, int contentMode);
// Fills `rect` on `hdc` with `color` (alpha ignored).
void fillRect(HDC hdc, const RECT &rect, Color color);
// Painting primitives for a surface's overlay: a box with fill, radii and
// border; text laid out in `rect` per the style (font, colour, alignment,
// wrapping, ellipsis, symbol centring), with `under` the colour it sits on.
void drawBox(HDC hdc, const RECT &rect, const BoxPaint &box);
void drawText(HDC hdc, const RECT &rect, const WidgetStyle &style, Color under);
// Icon glyph for a symbol name (SF Symbol names map to Segoe Fluent Icons).
std::wstring symbolGlyph(const std::string &name);
HFONT symbolFont(int pixelSize);

}  // namespace gea::win32
