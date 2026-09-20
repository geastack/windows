// SPDX-License-Identifier: Apache-2.0
// See win32_widgets.h for the model. This file owns the window classes, the
// painting, the common-control wrappers and the input plumbing.

#include "win32_widgets.h"

#include <commctrl.h>
#include <gdiplus.h>
#include <uxtheme.h>
#include <windowsx.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <map>
#include <memory>
#include <unordered_map>
#include <vector>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "msimg32.lib")

namespace gea::win32 {

namespace {

const wchar_t *const kWidgetProp = L"gea.widget";
const wchar_t *const kViewClass = L"GeaView";
const wchar_t *const kLabelClass = L"GeaLabel";
const wchar_t *const kCanvasClass = L"GeaCanvas";
const wchar_t *const kImageClass = L"GeaImage";
const wchar_t *const kScrollClass = L"GeaScroll";
const wchar_t *const kDividerClass = L"GeaDivider";
const UINT_PTR kSubclassId = 0x6765;  // 'ge'

HINSTANCE g_instance = nullptr;
bool g_initialized = false;
bool g_dark = false;
Color g_windowBackground = Color::rgb(255, 255, 255);
ULONG_PTR g_gdiplusToken = 0;

// Brushes handed back from WM_CTLCOLOR* must outlive the message; cache them
// per colour for the process lifetime (a handful of distinct colours per app).
HBRUSH brushFor(Color color)
{
	static std::map<COLORREF, HBRUSH> cache;
	const COLORREF key = color.ref();
	auto it = cache.find(key);
	if (it != cache.end()) return it->second;
	HBRUSH brush = CreateSolidBrush(key);
	cache.emplace(key, brush);
	return brush;
}

// Text decorations are font attributes in GDI; derive an underlined or
// struck variant of a registry font once and keep it.
HFONT decoratedFont(HFONT base, int decoration)
{
	if (!base || decoration == 0) return base;
	static std::map<std::pair<HFONT, int>, HFONT> cache;
	const auto key = std::make_pair(base, decoration);
	auto it = cache.find(key);
	if (it != cache.end()) return it->second;
	LOGFONTW logical{};
	if (GetObjectW(base, sizeof(logical), &logical) == 0) return base;
	if (decoration == 1) logical.lfUnderline = TRUE;
	if (decoration == 2) logical.lfStrikeOut = TRUE;
	HFONT font = CreateFontIndirectW(&logical);
	cache.emplace(key, font ? font : base);
	return font ? font : base;
}

Gdiplus::Color toGdiplus(Color color, int alphaOverride = -1)
{
	const int alpha = alphaOverride >= 0 ? alphaOverride : color.a;
	return Gdiplus::Color(static_cast<BYTE>(alpha), color.r, color.g, color.b);
}

bool anyRadius(const WidgetStyle &style)
{
	for (float radius : style.radius) {
		if (radius > 0.5f) return true;
	}
	return false;
}

// Rounded rectangle with per-corner radii as a GDI+ path (CSS corner order).
void addRoundedRect(Gdiplus::GraphicsPath &path, const Gdiplus::RectF &rect, const float radii[4])
{
	const float w = rect.Width;
	const float h = rect.Height;
	float r[4];
	for (int i = 0; i < 4; ++i) r[i] = std::max(0.0f, std::min(radii[i], std::min(w, h) / 2.0f));
	const float x = rect.X;
	const float y = rect.Y;
	if (r[0] > 0) path.AddArc(x, y, r[0] * 2, r[0] * 2, 180, 90);
	else path.AddLine(x, y, x, y);
	if (r[1] > 0) path.AddArc(x + w - r[1] * 2, y, r[1] * 2, r[1] * 2, 270, 90);
	else path.AddLine(x + w, y, x + w, y);
	if (r[2] > 0) path.AddArc(x + w - r[2] * 2, y + h - r[2] * 2, r[2] * 2, r[2] * 2, 0, 90);
	else path.AddLine(x + w, y + h, x + w, y + h);
	if (r[3] > 0) path.AddArc(x, y + h - r[3] * 2, r[3] * 2, r[3] * 2, 90, 90);
	else path.AddLine(x, y + h, x, y + h);
	path.CloseFigure();
}

Widget *widgetFromWindow(HWND hwnd)
{
	if (!hwnd) return nullptr;
	return static_cast<Widget *>(GetPropW(hwnd, kWidgetProp));
}

// The scroll container that should receive wheel input for a window.
Widget *scrollAncestor(HWND hwnd)
{
	for (HWND cursor = hwnd; cursor; cursor = GetParent(cursor)) {
		Widget *widget = widgetFromWindow(cursor);
		if (!widget) continue;
		if (widget->kind == WidgetKind::Scroll) return widget;
		// A multi-line edit scrolls itself; leave its wheel alone.
		if (widget->kind == WidgetKind::TextArea) return nullptr;
	}
	return nullptr;
}

int browserKeyCode(WPARAM virtualKey)
{
	switch (virtualKey) {
	case VK_RETURN: return 13;
	case VK_TAB: return 9;
	case VK_ESCAPE: return 27;
	case VK_BACK: return 8;
	case VK_DELETE: return 46;
	case VK_LEFT: return 37;
	case VK_UP: return 38;
	case VK_RIGHT: return 39;
	case VK_DOWN: return 40;
	case VK_HOME: return 36;
	case VK_END: return 35;
	case VK_SPACE: return 32;
	default: return 0;
	}
}

std::wstring toCrLf(const std::wstring &text)
{
	std::wstring out;
	out.reserve(text.size() + 16);
	for (size_t i = 0; i < text.size(); ++i) {
		if (text[i] == L'\n' && (i == 0 || text[i - 1] != L'\r')) out += L"\r\n";
		else out.push_back(text[i]);
	}
	return out;
}

std::wstring fromCrLf(const std::wstring &text)
{
	std::wstring out;
	out.reserve(text.size());
	for (wchar_t ch : text) {
		if (ch != L'\r') out.push_back(ch);
	}
	return out;
}

void paintText(HDC hdc, const RECT &rect, const WidgetStyle &style, Color background)
{
	if (style.text.empty()) return;
	HFONT font = decoratedFont(style.font, style.textDecoration);
	HGDIOBJ previous = font ? SelectObject(hdc, font) : nullptr;
	SetBkMode(hdc, TRANSPARENT);
	Color color = style.textColor.set() ? style.textColor : (g_dark ? Color::rgb(240, 240, 240) : Color::rgb(0, 0, 0));
	if (style.opacity < 255) color = blend(background, Color::rgb(color.r, color.g, color.b, style.opacity));
	SetTextColor(hdc, color.ref());
	UINT format = DT_NOPREFIX | DT_EXTERNALLEADING;
	switch (style.textAlign) {
	case 1: format |= DT_CENTER; break;
	case 2: format |= DT_RIGHT; break;
	default: format |= DT_LEFT;
	}
	if (style.maxLines == 1 || style.lineBreak != TextLineBreak::WordWrap) {
		format |= DT_SINGLELINE;
		if (style.lineBreak == TextLineBreak::TruncateTail) format |= DT_END_ELLIPSIS;
	} else {
		format |= DT_WORDBREAK | DT_EDITCONTROL;
	}
	RECT text = rect;
	if (style.symbol && style.text.size() == 1) {
		// An icon glyph is centred on its ink. DrawText would centre the
		// advance width and leave the glyph at the top of the line box, and
		// Segoe Fluent Icons glyphs sit high in theirs, so a symbol drawn that
		// way lands up and to the right of the box it should fill.
		GLYPHMETRICS metrics{};
		const MAT2 identity{{0, 1}, {0, 0}, {0, 0}, {0, 1}};
		if (GetGlyphOutlineW(hdc, style.text[0], GGO_METRICS, &metrics, 0, nullptr, &identity) != GDI_ERROR && metrics.gmBlackBoxX > 0 &&
		    metrics.gmBlackBoxY > 0) {
			const int inkLeft = (rect.left + rect.right - static_cast<int>(metrics.gmBlackBoxX)) / 2;
			const int inkTop = (rect.top + rect.bottom - static_cast<int>(metrics.gmBlackBoxY)) / 2;
			// gmptGlyphOrigin is the ink's top-left relative to the pen position
			// on the baseline (x to the right, y upwards).
			const UINT previousAlign = SetTextAlign(hdc, TA_LEFT | TA_BASELINE | TA_NOUPDATECP);
			ExtTextOutW(hdc, inkLeft - metrics.gmptGlyphOrigin.x, inkTop + metrics.gmptGlyphOrigin.y, 0, nullptr, style.text.c_str(), 1, nullptr);
			SetTextAlign(hdc, previousAlign);
			if (previous) SelectObject(hdc, previous);
			return;
		}
	}
	DrawTextW(hdc, style.text.c_str(), static_cast<int>(style.text.size()), &text, format);
	if (previous) SelectObject(hdc, previous);
}

void paintBackgroundAndBorder(HDC hdc, const RECT &client, Widget *widget, Color inherited)
{
	const WidgetStyle &style = widget->style;
	const int width = static_cast<int>(client.right - client.left);
	const int height = static_cast<int>(client.bottom - client.top);
	// Inherited fill first: anything not covered by an opaque rounded shape
	// shows what sits behind this window.
	fillRect(hdc, client, inherited);
	Color fill = style.background;
	if (fill.set()) {
		int alpha = fill.a;
		if (style.opacity < 255) alpha = alpha * style.opacity / 255;
		if (widget->pressed && style.pressHighlight) {
			fill = Color::rgb(fill.r * 4 / 5, fill.g * 4 / 5, fill.b * 4 / 5, fill.a);
		}
		const bool rounded = anyRadius(style);
		if (!rounded && alpha >= 255 && style.borderWidth <= 0) {
			fillRect(hdc, client, fill);
		} else {
			Gdiplus::Graphics graphics(hdc);
			graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
			graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
			Gdiplus::GraphicsPath path;
			addRoundedRect(path, Gdiplus::RectF(0, 0, static_cast<float>(width), static_cast<float>(height)), style.radius);
			Gdiplus::SolidBrush brush(toGdiplus(fill, alpha));
			graphics.FillPath(&brush, &path);
		}
	}
	if (style.borderWidth > 0 && style.border.set()) {
		Gdiplus::Graphics graphics(hdc);
		graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
		const float inset = style.borderWidth / 2.0f;
		Gdiplus::GraphicsPath path;
		float radii[4];
		for (int i = 0; i < 4; ++i) radii[i] = std::max(0.0f, style.radius[i] - inset);
		addRoundedRect(path, Gdiplus::RectF(inset, inset, width - style.borderWidth, height - style.borderWidth), radii);
		int alpha = style.border.a;
		if (style.opacity < 255) alpha = alpha * style.opacity / 255;
		Gdiplus::Pen pen(toGdiplus(style.border, alpha), static_cast<float>(style.borderWidth));
		graphics.DrawPath(&pen, &path);
	}
}

void paintWidget(Widget *widget, HDC target, const RECT &client)
{
	const int width = static_cast<int>(client.right - client.left);
	const int height = static_cast<int>(client.bottom - client.top);
	if (width <= 0 || height <= 0) return;
	HDC memory = CreateCompatibleDC(target);
	HBITMAP surface = CreateCompatibleBitmap(target, width, height);
	HGDIOBJ previous = SelectObject(memory, surface);
	Color inherited = effectiveBackground(GetParent(widget->hwnd));
	paintBackgroundAndBorder(memory, client, widget, inherited);
	if (widget->events.onPaintOverlay) widget->events.onPaintOverlay(memory, client);
	Color under = widget->style.background.set() ? blend(inherited, widget->style.background) : inherited;
	switch (widget->kind) {
	case WidgetKind::Label:
	case WidgetKind::View: {
		RECT text = client;
		if (widget->styledButton) {
			// Centre the title in the box the way a bezel would.
			text.left += 4;
			text.right -= 4;
			HFONT font = widget->style.font;
			HGDIOBJ prev = font ? SelectObject(memory, font) : nullptr;
			RECT measure = text;
			DrawTextW(memory, widget->style.text.c_str(), -1, &measure, DT_CALCRECT | DT_NOPREFIX | DT_SINGLELINE);
			if (prev) SelectObject(memory, prev);
			const int textHeight = measure.bottom - measure.top;
			text.top += std::max(0, (height - textHeight) / 2);
			WidgetStyle centred = widget->style;
			centred.textAlign = 1;
			centred.maxLines = 1;
			paintText(memory, text, centred, under);
		} else {
			paintText(memory, text, widget->style, under);
		}
		break;
	}
	case WidgetKind::Image:
	case WidgetKind::Canvas:
		if (widget->events.onPaintContent) widget->events.onPaintContent(memory, client);
		break;
	case WidgetKind::Divider: {
		// A thin line in the middle of the divider's hit area.
		RECT line = client;
		line.left = (client.left + client.right) / 2;
		line.right = line.left + 1;
		fillRect(memory, line, widget->style.border.set() ? widget->style.border : (g_dark ? Color::rgb(60, 60, 64) : Color::rgb(210, 210, 214)));
		break;
	}
	default:
		break;
	}
	BitBlt(target, client.left, client.top, width, height, memory, 0, 0, SRCCOPY);
	SelectObject(memory, previous);
	DeleteObject(surface);
	DeleteDC(memory);
}

void updateScrollBars(Widget *widget)
{
	RECT client{};
	GetClientRect(widget->hwnd, &client);
	const int viewportHeight = static_cast<int>(client.bottom - client.top);
	const int viewportWidth = static_cast<int>(client.right - client.left);
	const int contentHeight = std::max(widget->style.contentHeight, viewportHeight);
	const int contentWidth = std::max(widget->style.contentWidth, viewportWidth);
	if (widget->style.verticalScroller) {
		SCROLLINFO info{};
		info.cbSize = sizeof(info);
		info.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
		info.nMin = 0;
		info.nMax = contentHeight - 1;
		info.nPage = static_cast<UINT>(viewportHeight);
		info.nPos = widget->scrollTop;
		SetScrollInfo(widget->hwnd, SB_VERT, &info, TRUE);
		ShowScrollBar(widget->hwnd, SB_VERT, widget->style.contentHeight > viewportHeight);
	}
	if (widget->style.horizontalScroller) {
		SCROLLINFO info{};
		info.cbSize = sizeof(info);
		info.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
		info.nMin = 0;
		info.nMax = contentWidth - 1;
		info.nPage = static_cast<UINT>(viewportWidth);
		info.nPos = widget->scrollLeft;
		SetScrollInfo(widget->hwnd, SB_HORZ, &info, TRUE);
		ShowScrollBar(widget->hwnd, SB_HORZ, widget->style.contentWidth > viewportWidth);
	}
	if (widget->document) {
		GetClientRect(widget->hwnd, &client);
		const int clientHeight = static_cast<int>(client.bottom - client.top);
		const int clientWidth = static_cast<int>(client.right - client.left);
		const int documentHeight = std::max(widget->style.contentHeight, clientHeight);
		const int documentWidth = std::max(widget->style.contentWidth, clientWidth);
		const int maxTop = std::max(0, documentHeight - clientHeight);
		if (widget->scrollTop > maxTop) widget->scrollTop = maxTop;
		SetWindowPos(widget->document, nullptr, -widget->scrollLeft, -widget->scrollTop, documentWidth, documentHeight,
		             SWP_NOZORDER | SWP_NOACTIVATE);
	}
}

void handleVerticalScroll(Widget *widget, WORD request, int position)
{
	RECT client{};
	GetClientRect(widget->hwnd, &client);
	const int page = static_cast<int>(client.bottom - client.top);
	int top = widget->scrollTop;
	switch (request) {
	case SB_LINEUP: top -= 40; break;
	case SB_LINEDOWN: top += 40; break;
	case SB_PAGEUP: top -= page; break;
	case SB_PAGEDOWN: top += page; break;
	case SB_TOP: top = 0; break;
	case SB_BOTTOM: top = widget->style.contentHeight; break;
	case SB_THUMBTRACK:
	case SB_THUMBPOSITION: top = position; break;
	default: return;
	}
	setWidgetScrollTop(widget, top);
}

void notifyCommand(Widget *parent, HWND control, WORD code)
{
	(void)parent;
	Widget *widget = widgetFromWindow(control);
	if (!widget) return;
	switch (widget->kind) {
	case WidgetKind::Button:
		if (code == BN_CLICKED && widget->events.onClick) widget->events.onClick();
		break;
	case WidgetKind::CheckBox:
		if (code == BN_CLICKED) {
			widget->style.checked = SendMessageW(control, BM_GETCHECK, 0, 0) == BST_CHECKED;
			if (widget->events.onCheckedChanged) widget->events.onCheckedChanged(widget->style.checked);
		}
		break;
	case WidgetKind::TextField:
	case WidgetKind::TextArea:
		if (code == EN_CHANGE) {
			widget->lastText = widgetText(widget);
			widget->style.text = widget->lastText;
			if (widget->events.onTextChanged) widget->events.onTextChanged(widget->lastText);
		} else if (code == EN_SETFOCUS) {
			if (widget->events.onFocus) widget->events.onFocus();
		} else if (code == EN_KILLFOCUS) {
			if (widget->events.onBlur) widget->events.onBlur();
		}
		break;
	default:
		break;
	}
}

LRESULT CALLBACK widgetProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	Widget *widget = widgetFromWindow(hwnd);
	if (!widget) return DefWindowProcW(hwnd, message, wParam, lParam);
	switch (message) {
	case WM_ERASEBKGND:
		return 1;
	case WM_PAINT: {
		PAINTSTRUCT paint{};
		HDC hdc = BeginPaint(hwnd, &paint);
		RECT client{};
		GetClientRect(hwnd, &client);
		paintWidget(widget, hdc, client);
		EndPaint(hwnd, &paint);
		return 0;
	}
	case WM_NCHITTEST:
		if (widget->clickTransparent) return HTTRANSPARENT;
		break;
	case WM_SETCURSOR:
		if (widget->kind == WidgetKind::Divider) {
			SetCursor(LoadCursorW(nullptr, IDC_SIZEWE));
			return TRUE;
		}
		break;
	case WM_SIZE:
		if (widget->kind == WidgetKind::Scroll) updateScrollBars(widget);
		if (widget->events.onResize) widget->events.onResize(LOWORD(lParam), HIWORD(lParam));
		if (widget->style.background.set() && anyRadius(widget->style)) InvalidateRect(hwnd, nullptr, FALSE);
		break;
	case WM_VSCROLL:
		if (widget->kind == WidgetKind::Scroll) {
			SCROLLINFO info{};
			info.cbSize = sizeof(info);
			info.fMask = SIF_TRACKPOS;
			GetScrollInfo(hwnd, SB_VERT, &info);
			handleVerticalScroll(widget, LOWORD(wParam), info.nTrackPos);
			return 0;
		}
		// A trackbar child reports through its parent.
		if (Widget *child = widgetFromWindow(reinterpret_cast<HWND>(lParam)); child && child->kind == WidgetKind::Slider) {
			child->style.value = static_cast<double>(SendMessageW(child->hwnd, TBM_GETPOS, 0, 0));
			if (child->events.onValueChanged) child->events.onValueChanged(child->style.value);
			return 0;
		}
		break;
	case WM_HSCROLL:
		if (Widget *child = widgetFromWindow(reinterpret_cast<HWND>(lParam)); child && child->kind == WidgetKind::Slider) {
			child->style.value = static_cast<double>(SendMessageW(child->hwnd, TBM_GETPOS, 0, 0));
			if (child->events.onValueChanged) child->events.onValueChanged(child->style.value);
			return 0;
		}
		if (widget->kind == WidgetKind::Scroll) {
			SCROLLINFO info{};
			info.cbSize = sizeof(info);
			info.fMask = SIF_TRACKPOS;
			GetScrollInfo(hwnd, SB_HORZ, &info);
			int left = widget->scrollLeft;
			switch (LOWORD(wParam)) {
			case SB_LINELEFT: left -= 40; break;
			case SB_LINERIGHT: left += 40; break;
			case SB_THUMBTRACK:
			case SB_THUMBPOSITION: left = info.nTrackPos; break;
			default: break;
			}
			setWidgetScrollLeft(widget, left);
			return 0;
		}
		break;
	case WM_MOUSEWHEEL: {
		Widget *scroll = widget->kind == WidgetKind::Scroll ? widget : scrollAncestor(GetParent(hwnd));
		if (scroll) {
			UINT lines = 3;
			SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, 0, &lines, 0);
			const int delta = GET_WHEEL_DELTA_WPARAM(wParam);
			const int step = static_cast<int>(lines) * 40;
			setWidgetScrollTop(scroll, scroll->scrollTop - delta * step / WHEEL_DELTA);
			return 0;
		}
		break;
	}
	case WM_MOUSEHWHEEL: {
		Widget *scroll = widget->kind == WidgetKind::Scroll ? widget : scrollAncestor(GetParent(hwnd));
		if (scroll && scroll->style.horizontalScroller) {
			const int delta = GET_WHEEL_DELTA_WPARAM(wParam);
			setWidgetScrollLeft(scroll, scroll->scrollLeft + delta * 120 / WHEEL_DELTA);
			return 0;
		}
		break;
	}
	case WM_COMMAND:
		notifyCommand(widget, reinterpret_cast<HWND>(lParam), HIWORD(wParam));
		return 0;
	case WM_CTLCOLOREDIT:
	case WM_CTLCOLORSTATIC:
	case WM_CTLCOLORBTN: {
		HDC hdc = reinterpret_cast<HDC>(wParam);
		HWND control = reinterpret_cast<HWND>(lParam);
		Widget *child = widgetFromWindow(control);
		Color background = effectiveBackground(control);
		if (child && child->style.background.set()) background = blend(background, child->style.background);
		Color text = child && child->style.textColor.set() ? child->style.textColor : (g_dark ? Color::rgb(240, 240, 240) : Color::rgb(0, 0, 0));
		SetTextColor(hdc, text.ref());
		SetBkColor(hdc, background.ref());
		SetBkMode(hdc, OPAQUE);
		return reinterpret_cast<LRESULT>(brushFor(background));
	}
	case WM_LBUTTONDOWN: {
		if (widget->kind == WidgetKind::Canvas) {
			SetCapture(hwnd);
			widget->pressed = true;
			if (widget->events.onPointer) widget->events.onPointer(1, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
			return 0;
		}
		if (widget->kind == WidgetKind::Divider) {
			SetCapture(hwnd);
			widget->pressed = true;
			widget->user = reinterpret_cast<void *>(static_cast<intptr_t>(GET_X_LPARAM(lParam)));
			return 0;
		}
		// Clicking a plain view takes focus away from any edit, which commits
		// its text and lets a bound value refresh; then the press sequence.
		SetFocus(hwnd);
		SetCapture(hwnd);
		widget->pressed = true;
		if (widget->events.onSurfacePointer) {
			widget->events.onSurfacePointer(1, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
			return 0;
		}
		if (widget->style.pressHighlight) InvalidateRect(hwnd, nullptr, FALSE);
		if (widget->events.onPressDown) widget->events.onPressDown();
		return 0;
	}
	case WM_MOUSEMOVE:
		if (widget->pressed && widget->events.onSurfacePointer) {
			widget->events.onSurfacePointer(2, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
			return 0;
		}
		if (widget->kind == WidgetKind::Canvas && widget->pressed) {
			if (widget->events.onPointer) widget->events.onPointer(2, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
			return 0;
		}
		if (widget->kind == WidgetKind::Divider && widget->pressed) {
			const int origin = static_cast<int>(reinterpret_cast<intptr_t>(widget->user));
			if (widget->events.onDividerDrag) widget->events.onDividerDrag(GET_X_LPARAM(lParam) - origin);
			return 0;
		}
		break;
	case WM_LBUTTONUP: {
		if (!widget->pressed) break;
		widget->pressed = false;
		ReleaseCapture();
		if (widget->events.onSurfacePointer) {
			widget->events.onSurfacePointer(3, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
			return 0;
		}
		if (widget->kind == WidgetKind::Canvas) {
			if (widget->events.onPointer) widget->events.onPointer(3, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
			return 0;
		}
		if (widget->kind == WidgetKind::Divider) return 0;
		if (widget->style.pressHighlight) InvalidateRect(hwnd, nullptr, FALSE);
		RECT client{};
		GetClientRect(hwnd, &client);
		POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
		const bool inside = PtInRect(&client, point) != FALSE;
		if (widget->events.onPressUp) widget->events.onPressUp();
		if (inside && widget->events.onClick) widget->events.onClick();
		return 0;
	}
	case WM_CAPTURECHANGED:
		if (widget->pressed && reinterpret_cast<HWND>(lParam) != hwnd) {
			widget->pressed = false;
			if (widget->events.onSurfacePointer) {
				widget->events.onSurfacePointer(4, 0, 0);
			} else if (widget->kind == WidgetKind::Canvas) {
				if (widget->events.onPointer) widget->events.onPointer(3, 0, 0);
			} else if (widget->kind != WidgetKind::Divider) {
				if (widget->events.onPressUp) widget->events.onPressUp();
			}
			InvalidateRect(hwnd, nullptr, FALSE);
		}
		break;
	case WM_SETFOCUS:
		if (widget->events.onFocus) widget->events.onFocus();
		break;
	case WM_KILLFOCUS:
		if (widget->events.onBlur) widget->events.onBlur();
		break;
	case WM_KEYDOWN:
		if (widget->events.onKeyDown) {
			const int code = browserKeyCode(wParam);
			if (code) widget->events.onKeyDown(code);
		}
		break;
	case WM_DESTROY:
		RemovePropW(hwnd, kWidgetProp);
		break;
	default:
		break;
	}
	return DefWindowProcW(hwnd, message, wParam, lParam);
}

// Common controls keep their own window procedure; this subclass adds the
// key, focus and wheel plumbing the model expects from every widget.
LRESULT CALLBACK controlSubclass(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR)
{
	Widget *widget = widgetFromWindow(hwnd);
	if (widget) {
		switch (message) {
		case WM_KEYDOWN:
			if (widget->events.onKeyDown) {
				const int code = browserKeyCode(wParam);
				if (code) widget->events.onKeyDown(code);
			}
			break;
		case WM_MOUSEWHEEL:
			if (widget->kind != WidgetKind::TextArea) {
				if (Widget *scroll = scrollAncestor(GetParent(hwnd))) return SendMessageW(scroll->hwnd, message, wParam, lParam);
			}
			break;
		case WM_SETFOCUS:
			if (widget->events.onFocus) widget->events.onFocus();
			break;
		case WM_KILLFOCUS:
			if (widget->events.onBlur) widget->events.onBlur();
			break;
		case WM_NCDESTROY:
			RemoveWindowSubclass(hwnd, controlSubclass, kSubclassId);
			break;
		default:
			break;
		}
	}
	return DefSubclassProc(hwnd, message, wParam, lParam);
}

void registerClass(const wchar_t *name, UINT extraStyle = 0)
{
	WNDCLASSEXW cls{};
	cls.cbSize = sizeof(cls);
	cls.style = CS_DBLCLKS | extraStyle;
	cls.lpfnWndProc = widgetProc;
	cls.hInstance = g_instance;
	cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
	cls.lpszClassName = name;
	RegisterClassExW(&cls);
}

const wchar_t *classNameFor(WidgetKind kind, DWORD &style, DWORD &exStyle)
{
	style = WS_CHILD | WS_VISIBLE;
	exStyle = 0;
	switch (kind) {
	case WidgetKind::View: style |= WS_CLIPCHILDREN | WS_CLIPSIBLINGS; return kViewClass;
	case WidgetKind::Label: style |= WS_CLIPSIBLINGS; return kLabelClass;
	case WidgetKind::Canvas: style |= WS_CLIPSIBLINGS; return kCanvasClass;
	case WidgetKind::Image: style |= WS_CLIPSIBLINGS; return kImageClass;
	case WidgetKind::Scroll: style |= WS_CLIPCHILDREN | WS_CLIPSIBLINGS | WS_VSCROLL; return kScrollClass;
	case WidgetKind::Divider: style |= WS_CLIPSIBLINGS; return kDividerClass;
	case WidgetKind::Button: style |= WS_TABSTOP | BS_PUSHBUTTON | BS_CENTER | BS_VCENTER; return WC_BUTTONW;
	case WidgetKind::CheckBox: style |= WS_TABSTOP | BS_AUTOCHECKBOX; return WC_BUTTONW;
	case WidgetKind::TextField: style |= WS_TABSTOP | ES_AUTOHSCROLL | ES_LEFT; return WC_EDITW;
	case WidgetKind::TextArea: style |= WS_TABSTOP | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN | WS_VSCROLL | ES_LEFT; return WC_EDITW;
	case WidgetKind::Slider: style |= WS_TABSTOP | TBS_HORZ | TBS_NOTICKS | TBS_BOTH; return TRACKBAR_CLASSW;
	case WidgetKind::ProgressBar: style |= PBS_SMOOTH; return PROGRESS_CLASSW;
	}
	return kViewClass;
}

bool isCustomClass(WidgetKind kind)
{
	switch (kind) {
	case WidgetKind::View:
	case WidgetKind::Label:
	case WidgetKind::Canvas:
	case WidgetKind::Image:
	case WidgetKind::Scroll:
	case WidgetKind::Divider:
		return true;
	default:
		return false;
	}
}

}  // namespace

Color blend(Color under, Color over)
{
	if (!over.set()) return under;
	if (over.a >= 255) return Color::rgb(over.r, over.g, over.b);
	const int a = over.a;
	const int inv = 255 - a;
	return Color::rgb((over.r * a + under.r * inv) / 255, (over.g * a + under.g * inv) / 255, (over.b * a + under.b * inv) / 255);
}

void initializeWidgets(HINSTANCE instance)
{
	if (g_initialized) return;
	g_initialized = true;
	g_instance = instance;
	INITCOMMONCONTROLSEX controls{};
	controls.dwSize = sizeof(controls);
	controls.dwICC = ICC_STANDARD_CLASSES | ICC_BAR_CLASSES | ICC_PROGRESS_CLASS | ICC_WIN95_CLASSES;
	InitCommonControlsEx(&controls);
	Gdiplus::GdiplusStartupInput input;
	Gdiplus::GdiplusStartup(&g_gdiplusToken, &input, nullptr);
	registerClass(kViewClass);
	registerClass(kLabelClass);
	registerClass(kCanvasClass);
	registerClass(kImageClass);
	registerClass(kScrollClass);
	registerClass(kDividerClass);
}

Widget *createWidget(WidgetKind kind, HWND parent)
{
	DWORD style = 0;
	DWORD exStyle = 0;
	const wchar_t *className = classNameFor(kind, style, exStyle);
	auto *widget = new Widget();
	widget->kind = kind;
	widget->clickTransparent = kind == WidgetKind::Label;
	HWND hwnd = CreateWindowExW(exStyle, className, L"", style, 0, 0, 10, 10, parent, nullptr, g_instance, nullptr);
	if (!hwnd) {
		delete widget;
		return nullptr;
	}
	widget->hwnd = hwnd;
	SetPropW(hwnd, kWidgetProp, widget);
	if (!isCustomClass(kind)) {
		SetWindowSubclass(hwnd, controlSubclass, kSubclassId, 0);
		if (g_dark) applyControlTheme(hwnd, true);
		if (kind == WidgetKind::Slider) {
			SendMessageW(hwnd, TBM_SETRANGEMIN, FALSE, 0);
			SendMessageW(hwnd, TBM_SETRANGEMAX, TRUE, 100);
		}
	}
	if (kind == WidgetKind::Scroll) {
		Widget *document = createWidget(WidgetKind::View, hwnd);
		widget->document = document ? document->hwnd : nullptr;
		ShowScrollBar(hwnd, SB_VERT, FALSE);
	}
	return widget;
}

Widget *widgetFor(HWND hwnd)
{
	return widgetFromWindow(hwnd);
}

Widget *nearestWidget(HWND hwnd)
{
	for (HWND cursor = hwnd; cursor; cursor = GetParent(cursor)) {
		if (Widget *widget = widgetFromWindow(cursor)) return widget;
	}
	return nullptr;
}

void destroyWidget(Widget *widget)
{
	if (!widget) return;
	if (widget->hwnd) {
		if (widget->document) {
			if (Widget *document = widgetFromWindow(widget->document)) destroyWidget(document);
			widget->document = nullptr;
		}
		RemovePropW(widget->hwnd, kWidgetProp);
		DestroyWindow(widget->hwnd);
		widget->hwnd = nullptr;
	}
	delete widget;
}

void applyWidgetStyle(Widget *widget, const WidgetStyle &style)
{
	if (!widget || !widget->hwnd) return;
	WidgetStyle previous = widget->style;
	widget->style = style;
	HWND hwnd = widget->hwnd;
	const bool visibleChanged = previous.hidden != style.hidden;
	if (visibleChanged) ShowWindow(hwnd, style.hidden ? SW_HIDE : SW_SHOWNA);
	switch (widget->kind) {
	case WidgetKind::TextField:
	case WidgetKind::TextArea: {
		if (previous.font != style.font && style.font) SendMessageW(hwnd, WM_SETFONT, reinterpret_cast<WPARAM>(style.font), TRUE);
		if (previous.editable != style.editable) SendMessageW(hwnd, EM_SETREADONLY, style.editable ? FALSE : TRUE, 0);
		if (previous.bordered != style.bordered) {
			LONG_PTR windowStyle = GetWindowLongPtrW(hwnd, GWL_STYLE);
			windowStyle = style.bordered ? (windowStyle | WS_BORDER) : (windowStyle & ~WS_BORDER);
			SetWindowLongPtrW(hwnd, GWL_STYLE, windowStyle);
			SetWindowPos(hwnd, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
		}
		if (widget->kind == WidgetKind::TextField && previous.placeholder != style.placeholder) {
			SendMessageW(hwnd, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(style.placeholder.c_str()));
		}
		if (previous.textAlign != style.textAlign) {
			LONG_PTR windowStyle = GetWindowLongPtrW(hwnd, GWL_STYLE) & ~(ES_LEFT | ES_CENTER | ES_RIGHT);
			windowStyle |= style.textAlign == 1 ? ES_CENTER : style.textAlign == 2 ? ES_RIGHT : ES_LEFT;
			SetWindowLongPtrW(hwnd, GWL_STYLE, windowStyle);
		}
		// Push the model's text only when the control shows something else:
		// while the user types, EN_CHANGE keeps `lastText` equal to the model
		// and the caret is left alone. A programmatic change (another note
		// selected) differs and always wins, focused or not.
		const std::wstring current = widgetText(widget);
		if (current != style.text) {
			widget->lastText = style.text;
			SetWindowTextW(hwnd, toCrLf(style.text).c_str());
			if (widget->kind == WidgetKind::TextArea) SendMessageW(hwnd, EM_SETSEL, 0, 0);
		}
		if (previous.textColor.r != style.textColor.r || previous.textColor.g != style.textColor.g || previous.textColor.b != style.textColor.b ||
		    previous.textColor.a != style.textColor.a || previous.background.a != style.background.a) {
			InvalidateRect(hwnd, nullptr, TRUE);
		}
		break;
	}
	case WidgetKind::Button:
	case WidgetKind::CheckBox: {
		if (previous.font != style.font && style.font) SendMessageW(hwnd, WM_SETFONT, reinterpret_cast<WPARAM>(style.font), TRUE);
		if (previous.text != style.text) SetWindowTextW(hwnd, style.text.c_str());
		if (previous.editable != style.editable) EnableWindow(hwnd, style.editable);
		if (widget->kind == WidgetKind::CheckBox && previous.checked != style.checked) {
			SendMessageW(hwnd, BM_SETCHECK, style.checked ? BST_CHECKED : BST_UNCHECKED, 0);
		}
		break;
	}
	case WidgetKind::Slider: {
		if (previous.minimum != style.minimum) SendMessageW(hwnd, TBM_SETRANGEMIN, TRUE, static_cast<LPARAM>(std::lround(style.minimum)));
		if (previous.maximum != style.maximum) SendMessageW(hwnd, TBM_SETRANGEMAX, TRUE, static_cast<LPARAM>(std::lround(style.maximum)));
		const LONG current = static_cast<LONG>(SendMessageW(hwnd, TBM_GETPOS, 0, 0));
		if (!widget->pressed && current != std::lround(style.value)) SendMessageW(hwnd, TBM_SETPOS, TRUE, static_cast<LPARAM>(std::lround(style.value)));
		break;
	}
	case WidgetKind::ProgressBar: {
		if (previous.minimum != style.minimum || previous.maximum != style.maximum) {
			SendMessageW(hwnd, PBM_SETRANGE32, static_cast<WPARAM>(std::lround(style.minimum)), static_cast<LPARAM>(std::lround(style.maximum)));
		}
		if (previous.indeterminate != style.indeterminate) {
			LONG_PTR windowStyle = GetWindowLongPtrW(hwnd, GWL_STYLE);
			windowStyle = style.indeterminate ? (windowStyle | PBS_MARQUEE) : (windowStyle & ~PBS_MARQUEE);
			SetWindowLongPtrW(hwnd, GWL_STYLE, windowStyle);
			SendMessageW(hwnd, PBM_SETMARQUEE, style.indeterminate ? TRUE : FALSE, 30);
		}
		if (!style.indeterminate) SendMessageW(hwnd, PBM_SETPOS, static_cast<WPARAM>(std::lround(style.value)), 0);
		break;
	}
	case WidgetKind::Scroll: {
		if (previous.contentHeight != style.contentHeight || previous.contentWidth != style.contentWidth ||
		    previous.verticalScroller != style.verticalScroller || previous.horizontalScroller != style.horizontalScroller) {
			LONG_PTR windowStyle = GetWindowLongPtrW(hwnd, GWL_STYLE);
			windowStyle = style.horizontalScroller ? (windowStyle | WS_HSCROLL) : (windowStyle & ~WS_HSCROLL);
			windowStyle = style.verticalScroller ? (windowStyle | WS_VSCROLL) : (windowStyle & ~WS_VSCROLL);
			SetWindowLongPtrW(hwnd, GWL_STYLE, windowStyle);
			updateScrollBars(widget);
		}
		if (Widget *document = widgetFromWindow(widget->document)) {
			// The document inherits the scroll's fill so children without a
			// background of their own sit on the right colour.
			WidgetStyle documentStyle = document->style;
			documentStyle.background = style.background;
			documentStyle.opacity = style.opacity;
			if (documentStyle.background.a != document->style.background.a || documentStyle.background.r != document->style.background.r ||
			    documentStyle.background.g != document->style.background.g || documentStyle.background.b != document->style.background.b) {
				applyWidgetStyle(document, documentStyle);
			}
		}
		InvalidateRect(hwnd, nullptr, FALSE);
		break;
	}
	default: {
		// Custom-painted kinds: repaint when anything that paints changed.
		const bool changed = previous.background.a != style.background.a || previous.background.r != style.background.r ||
		                     previous.background.g != style.background.g || previous.background.b != style.background.b ||
		                     previous.border.a != style.border.a || previous.border.r != style.border.r || previous.border.g != style.border.g ||
		                     previous.border.b != style.border.b || previous.borderWidth != style.borderWidth || previous.opacity != style.opacity ||
		                     previous.text != style.text || previous.font != style.font || previous.textColor.a != style.textColor.a ||
		                     previous.textColor.r != style.textColor.r || previous.textColor.g != style.textColor.g ||
		                     previous.textColor.b != style.textColor.b || previous.textAlign != style.textAlign ||
		                     previous.textDecoration != style.textDecoration || previous.lineBreak != style.lineBreak ||
		                     previous.maxLines != style.maxLines || previous.bitmap != style.bitmap || previous.contentMode != style.contentMode ||
		                     previous.tint.a != style.tint.a || previous.tint.r != style.tint.r || previous.tint.g != style.tint.g ||
		                     previous.tint.b != style.tint.b || std::memcmp(previous.radius, style.radius, sizeof(style.radius)) != 0 ||
		                     widget->kind == WidgetKind::Canvas;
		if (changed) {
			InvalidateRect(hwnd, nullptr, FALSE);
			// Children without their own background paint the parent's colour
			// themselves, so a container colour change repaints the subtree.
			if (previous.background.a != style.background.a || previous.background.r != style.background.r ||
			    previous.background.g != style.background.g || previous.background.b != style.background.b) {
				RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_NOERASE);
			}
		}
		break;
	}
	}
}

void setWidgetFrame(Widget *widget, int x, int y, int width, int height)
{
	if (!widget || !widget->hwnd) return;
	RECT current{};
	GetWindowRect(widget->hwnd, &current);
	HWND parent = GetParent(widget->hwnd);
	POINT origin{current.left, current.top};
	if (parent) ScreenToClient(parent, &origin);
	const int currentWidth = current.right - current.left;
	const int currentHeight = current.bottom - current.top;
	width = std::max(0, width);
	height = std::max(0, height);
	if (origin.x == x && origin.y == y && currentWidth == width && currentHeight == height) return;
	SetWindowPos(widget->hwnd, nullptr, x, y, width, height, SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOCOPYBITS);
	if (widget->kind == WidgetKind::Scroll) updateScrollBars(widget);
}

void setWidgetParent(Widget *widget, HWND parent)
{
	if (!widget || !widget->hwnd) return;
	if (GetParent(widget->hwnd) == parent) return;
	SetParent(widget->hwnd, parent);
}

RECT widgetFrame(const Widget *widget)
{
	RECT rect{};
	if (!widget || !widget->hwnd) return rect;
	GetWindowRect(widget->hwnd, &rect);
	HWND parent = GetParent(widget->hwnd);
	if (parent) MapWindowPoints(nullptr, parent, reinterpret_cast<POINT *>(&rect), 2);
	return rect;
}

void invalidateWidget(Widget *widget, bool children)
{
	if (!widget || !widget->hwnd) return;
	if (children) RedrawWindow(widget->hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_NOERASE);
	else InvalidateRect(widget->hwnd, nullptr, FALSE);
}

void setWidgetScrollTop(Widget *widget, int scrollTop)
{
	if (!widget || widget->kind != WidgetKind::Scroll) return;
	RECT client{};
	GetClientRect(widget->hwnd, &client);
	const int maxTop = std::max(0, widget->style.contentHeight - static_cast<int>(client.bottom - client.top));
	scrollTop = std::clamp(scrollTop, 0, maxTop);
	if (scrollTop == widget->scrollTop) return;
	widget->scrollTop = scrollTop;
	SetScrollPos(widget->hwnd, SB_VERT, scrollTop, TRUE);
	if (widget->document) {
		SetWindowPos(widget->document, nullptr, -widget->scrollLeft, -scrollTop, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
	}
	if (widget->events.onScroll) widget->events.onScroll(scrollTop);
}

void setWidgetScrollLeft(Widget *widget, int scrollLeft)
{
	if (!widget || widget->kind != WidgetKind::Scroll) return;
	RECT client{};
	GetClientRect(widget->hwnd, &client);
	const int maxLeft = std::max(0, widget->style.contentWidth - static_cast<int>(client.right - client.left));
	scrollLeft = std::clamp(scrollLeft, 0, maxLeft);
	if (scrollLeft == widget->scrollLeft) return;
	widget->scrollLeft = scrollLeft;
	SetScrollPos(widget->hwnd, SB_HORZ, scrollLeft, TRUE);
	if (widget->document) {
		SetWindowPos(widget->document, nullptr, -scrollLeft, -widget->scrollTop, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
	}
}

std::wstring widgetText(const Widget *widget)
{
	if (!widget || !widget->hwnd) return std::wstring();
	const int length = GetWindowTextLengthW(widget->hwnd);
	if (length <= 0) return std::wstring();
	std::wstring text(static_cast<size_t>(length) + 1, L'\0');
	GetWindowTextW(widget->hwnd, text.data(), length + 1);
	text.resize(static_cast<size_t>(length));
	return fromCrLf(text);
}

void focusWidget(Widget *widget)
{
	if (widget && widget->hwnd) SetFocus(widget->hwnd);
}

Color effectiveBackground(HWND hwnd)
{
	std::vector<Color> layers;
	for (HWND cursor = hwnd; cursor; cursor = GetParent(cursor)) {
		Widget *widget = widgetFromWindow(cursor);
		if (!widget) break;
		const Color background = widget->style.background;
		if (!background.set()) continue;
		if (background.a >= 255) {
			Color result = background;
			for (auto it = layers.rbegin(); it != layers.rend(); ++it) result = blend(result, *it);
			return result;
		}
		layers.push_back(background);
	}
	Color result = g_windowBackground;
	for (auto it = layers.rbegin(); it != layers.rend(); ++it) result = blend(result, *it);
	return result;
}

void setWindowBackground(Color color)
{
	g_windowBackground = color.set() ? color : g_windowBackground;
}

Color windowBackground()
{
	return g_windowBackground;
}

void applyControlTheme(HWND hwnd, bool dark)
{
	SetWindowTheme(hwnd, dark ? L"DarkMode_Explorer" : L"Explorer", nullptr);
}

bool darkModeEnabled()
{
	return g_dark;
}

void setDarkModeEnabled(bool dark)
{
	g_dark = dark;
}

bool routeMouseWheel(MSG &message)
{
	if (message.message != WM_MOUSEWHEEL && message.message != WM_MOUSEHWHEEL) return false;
	POINT point{GET_X_LPARAM(message.lParam), GET_Y_LPARAM(message.lParam)};
	HWND under = WindowFromPoint(point);
	if (!under) return false;
	// Only redirect within our own process, and only when something under the
	// cursor scrolls; a foreign window keeps the default focused-window rule.
	DWORD process = 0;
	GetWindowThreadProcessId(under, &process);
	if (process != GetCurrentProcessId()) return false;
	Widget *scroll = scrollAncestor(under);
	if (Widget *direct = widgetFromWindow(under); direct && direct->kind == WidgetKind::TextArea) scroll = nullptr;
	if (scroll) message.hwnd = scroll->hwnd;
	else if (widgetFromWindow(under) || GetParent(under)) message.hwnd = under;
	return false;
}

std::wstring toWide(const std::string &utf8)
{
	if (utf8.empty()) return std::wstring();
	const int needed = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
	if (needed <= 0) return std::wstring();
	std::wstring wide(static_cast<size_t>(needed), L'\0');
	MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), wide.data(), needed);
	return wide;
}

std::string fromWide(const std::wstring &wide)
{
	if (wide.empty()) return std::string();
	const int needed = WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
	if (needed <= 0) return std::string();
	std::string utf8(static_cast<size_t>(needed), '\0');
	WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), utf8.data(), needed, nullptr, nullptr);
	return utf8;
}

HBITMAP createBgraBitmap(int width, int height, void **bits)
{
	BITMAPINFO info{};
	info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	info.bmiHeader.biWidth = width;
	info.bmiHeader.biHeight = -height;  // top-down
	info.bmiHeader.biPlanes = 1;
	info.bmiHeader.biBitCount = 32;
	info.bmiHeader.biCompression = BI_RGB;
	void *pixels = nullptr;
	HBITMAP bitmap = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
	if (bits) *bits = pixels;
	return bitmap;
}

void paintBitmap(HDC hdc, HBITMAP bitmap, int bitmapWidth, int bitmapHeight, const RECT &dest, int contentMode)
{
	if (!bitmap || bitmapWidth <= 0 || bitmapHeight <= 0) return;
	const int destWidth = static_cast<int>(dest.right - dest.left);
	const int destHeight = static_cast<int>(dest.bottom - dest.top);
	if (destWidth <= 0 || destHeight <= 0) return;
	int drawWidth = destWidth;
	int drawHeight = destHeight;
	switch (contentMode) {
	case 1:  // contain
	case 4: {  // scale-down
		const double scale = std::min(static_cast<double>(destWidth) / bitmapWidth, static_cast<double>(destHeight) / bitmapHeight);
		const double clamped = contentMode == 4 ? std::min(scale, 1.0) : scale;
		drawWidth = std::max(1, static_cast<int>(std::lround(bitmapWidth * clamped)));
		drawHeight = std::max(1, static_cast<int>(std::lround(bitmapHeight * clamped)));
		break;
	}
	case 2: {  // cover
		const double scale = std::max(static_cast<double>(destWidth) / bitmapWidth, static_cast<double>(destHeight) / bitmapHeight);
		drawWidth = std::max(1, static_cast<int>(std::lround(bitmapWidth * scale)));
		drawHeight = std::max(1, static_cast<int>(std::lround(bitmapHeight * scale)));
		break;
	}
	case 3:  // none
		drawWidth = bitmapWidth;
		drawHeight = bitmapHeight;
		break;
	default:
		break;
	}
	const int x = dest.left + (destWidth - drawWidth) / 2;
	const int y = dest.top + (destHeight - drawHeight) / 2;
	HDC source = CreateCompatibleDC(hdc);
	HGDIOBJ previous = SelectObject(source, bitmap);
	BLENDFUNCTION blendFunction{};
	blendFunction.BlendOp = AC_SRC_OVER;
	blendFunction.SourceConstantAlpha = 255;
	blendFunction.AlphaFormat = AC_SRC_ALPHA;
	if (contentMode == 2) {
		// Cover: clip to the destination so the overflow is cut off.
		SaveDC(hdc);
		IntersectClipRect(hdc, dest.left, dest.top, dest.right, dest.bottom);
		SetStretchBltMode(hdc, HALFTONE);
		AlphaBlend(hdc, x, y, drawWidth, drawHeight, source, 0, 0, bitmapWidth, bitmapHeight, blendFunction);
		RestoreDC(hdc, -1);
	} else {
		SetStretchBltMode(hdc, HALFTONE);
		AlphaBlend(hdc, x, y, drawWidth, drawHeight, source, 0, 0, bitmapWidth, bitmapHeight, blendFunction);
	}
	SelectObject(source, previous);
	DeleteDC(source);
}

void drawBox(HDC hdc, const RECT &rect, const BoxPaint &box)
{
	const int width = static_cast<int>(rect.right - rect.left);
	const int height = static_cast<int>(rect.bottom - rect.top);
	if (width <= 0 || height <= 0) return;
	bool rounded = false;
	for (float radius : box.radius) rounded = rounded || radius > 0.5f;
	Color fill = box.fill;
	if (fill.set()) {
		const int alpha = fill.a * box.opacity / 255;
		if (box.pressed) fill = Color::rgb(fill.r * 4 / 5, fill.g * 4 / 5, fill.b * 4 / 5, fill.a);
		if (!rounded && alpha >= 255 && box.borderWidth <= 0) {
			fillRect(hdc, rect, fill);
		} else if (alpha > 0) {
			Gdiplus::Graphics graphics(hdc);
			graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
			graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
			Gdiplus::GraphicsPath path;
			addRoundedRect(path, Gdiplus::RectF(static_cast<float>(rect.left), static_cast<float>(rect.top), static_cast<float>(width), static_cast<float>(height)), box.radius);
			Gdiplus::SolidBrush brush(toGdiplus(fill, alpha));
			graphics.FillPath(&brush, &path);
		}
	}
	if (box.borderWidth > 0 && box.border.set()) {
		Gdiplus::Graphics graphics(hdc);
		graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
		const float inset = box.borderWidth / 2.0f;
		Gdiplus::GraphicsPath path;
		float radii[4];
		for (int i = 0; i < 4; ++i) radii[i] = std::max(0.0f, box.radius[i] - inset);
		addRoundedRect(path, Gdiplus::RectF(rect.left + inset, rect.top + inset, width - box.borderWidth, height - box.borderWidth), radii);
		Gdiplus::Pen pen(toGdiplus(box.border, box.border.a * box.opacity / 255), static_cast<float>(box.borderWidth));
		graphics.DrawPath(&pen, &path);
	}
}

void drawText(HDC hdc, const RECT &rect, const WidgetStyle &style, Color under)
{
	paintText(hdc, rect, style, under);
}

void fillRect(HDC hdc, const RECT &rect, Color color)
{
	SetBkColor(hdc, color.ref());
	ExtTextOutW(hdc, 0, 0, ETO_OPAQUE, &rect, L"", 0, nullptr);
}

// SF Symbol names (what the shared examples use) to Segoe Fluent Icons /
// Segoe MDL2 Assets code points. Unknown names that look like a hex code point
// are used directly; anything else falls back to a generic glyph.
std::wstring symbolGlyph(const std::string &name)
{
	static const std::unordered_map<std::string, wchar_t> table = {
		{"sidebar.left", 0xE8A0},         {"sidebar.leading", 0xE8A0},   {"square.and.pencil", 0xE70F}, {"pencil", 0xE70F},
		{"textformat", 0xE8D2},           {"checklist", 0xE73A},         {"tablecells", 0xE80A},        {"square.and.arrow.up", 0xE72D},
		{"tray.full", 0xE7B8},            {"tray", 0xE7B8},              {"folder", 0xE8B7},            {"folder.fill", 0xE8D5},
		{"lightbulb", 0xEA80},            {"airplane", 0xE709},          {"archivebox", 0xE7B8},        {"magnifyingglass", 0xE721},
		{"plus", 0xE710},                 {"minus", 0xE738},             {"xmark", 0xE711},             {"trash", 0xE74D},
		{"gear", 0xE713},                 {"gearshape", 0xE713},         {"star", 0xE734},              {"star.fill", 0xE735},
		{"heart", 0xEB51},                {"heart.fill", 0xEB52},        {"house", 0xE80F},             {"person", 0xE77B},
		{"person.crop.circle", 0xE77B},   {"bell", 0xEA8F},              {"calendar", 0xE787},          {"clock", 0xE823},
		{"camera", 0xE722},               {"photo", 0xEB9F},             {"mic", 0xE720},               {"speaker.wave.2", 0xE767},
		{"play.fill", 0xE768},            {"pause.fill", 0xE769},        {"stop.fill", 0xE71A},         {"chevron.left", 0xE76B},
		{"chevron.right", 0xE76C},        {"chevron.up", 0xE70E},        {"chevron.down", 0xE70D},      {"arrow.left", 0xE72B},
		{"arrow.right", 0xE72A},          {"arrow.clockwise", 0xE72C},   {"doc", 0xE8A5},               {"doc.text", 0xE8A5},
		{"link", 0xE71B},                 {"paperplane", 0xE724},        {"envelope", 0xE715},          {"map", 0xE707},
		{"location", 0xE81D},             {"wifi", 0xE701},              {"battery.100", 0xE83F},       {"bolt", 0xE945},
		{"info.circle", 0xE946},          {"questionmark.circle", 0xE897}, {"exclamationmark.triangle", 0xE7BA}, {"checkmark", 0xE73E},
		{"checkmark.circle", 0xE73E},     {"ellipsis", 0xE712},          {"line.3.horizontal", 0xE700}, {"list.bullet", 0xE8FD},
		{"square.grid.2x2", 0xF0E2},      {"moon", 0xE708},              {"sun.max", 0xE706},           {"printer", 0xE749},
		{"lock", 0xE72E},                 {"lock.open", 0xE785},         {"key", 0xE8D7},               {"globe", 0xE774},
	};
	auto it = table.find(name);
	if (it != table.end()) return std::wstring(1, it->second);
	if (name.size() >= 4 && name.size() <= 5) {
		bool hex = true;
		for (char ch : name) hex = hex && std::isxdigit(static_cast<unsigned char>(ch));
		if (hex) return std::wstring(1, static_cast<wchar_t>(std::stoul(name, nullptr, 16)));
	}
	return std::wstring(1, static_cast<wchar_t>(0xE783));
}

HFONT symbolFont(int pixelSize)
{
	static std::map<int, HFONT> cache;
	auto it = cache.find(pixelSize);
	if (it != cache.end()) return it->second;
	static const wchar_t *family = [] {
		// Segoe Fluent Icons ships with Windows 11; MDL2 Assets with Windows 10.
		HDC screen = GetDC(nullptr);
		LOGFONTW probe{};
		probe.lfCharSet = DEFAULT_CHARSET;
		wcscpy_s(probe.lfFaceName, L"Segoe Fluent Icons");
		bool found = false;
		EnumFontFamiliesExW(
		    screen, &probe,
		    [](const LOGFONTW *, const TEXTMETRICW *, DWORD, LPARAM lParam) -> int {
			    *reinterpret_cast<bool *>(lParam) = true;
			    return 0;
		    },
		    reinterpret_cast<LPARAM>(&found), 0);
		ReleaseDC(nullptr, screen);
		return found ? L"Segoe Fluent Icons" : L"Segoe MDL2 Assets";
	}();
	LOGFONTW logical{};
	logical.lfHeight = -std::max(1, pixelSize);
	logical.lfWeight = FW_NORMAL;
	logical.lfCharSet = DEFAULT_CHARSET;
	logical.lfQuality = CLEARTYPE_QUALITY;
	wcscpy_s(logical.lfFaceName, family);
	HFONT font = CreateFontIndirectW(&logical);
	cache.emplace(pixelSize, font);
	return font;
}

}  // namespace gea::win32
