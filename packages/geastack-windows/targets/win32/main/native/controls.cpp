// SPDX-License-Identifier: Apache-2.0
// The @geastack/windows/Controls object model over the widget layer.
//
// A Windows-native program builds WinView / WinStackView / WinLabel / ...
// objects; the compiler lowers every property and method to one of the
// thunks declared by the generated gea/windows/native_bridge.h, and this file
// defines them. Objects live in the bridge's handle table; each holds a
// widget (one HWND) plus the layout state the AppKit-shaped API implies:
// stack views arrange their children, anchors pin a child to its parent's
// edges, scroll views size their document to its content.
//
// Layout runs once per frame from the target's frame loop (the callback
// installed through setNativeLayoutCallback), starting at the root view that
// installRootView handed over, in layout px scaled to the window's DPI.

#include "gea/windows/native_bridge.h"

#include "../win32_font_registry.h"
#include "../win32_main.h"
#include "../win32_native_shell.h"
#include "../win32_widgets.h"

#include <gdiplus.h>
#include <shellapi.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace gea::windows::Controls {

namespace {

using gea::win32::Color;
using gea::win32::Widget;
using gea::win32::WidgetKind;
using gea::win32::WidgetStyle;

// --- objects -----------------------------------------------------------------------

enum class Kind { Callback, Color, Font, Image, View, Toolbar };

struct Object {
	Kind kind;
	explicit Object(Kind objectKind) : kind(objectKind) {}
	virtual ~Object() = default;
};

struct CallbackObject : Object {
	std::function<void()> handler;
	CallbackObject() : Object(Kind::Callback) {}
};

struct ColorObject : Object {
	Color color;
	ColorObject() : Object(Kind::Color) {}
};

struct FontObject : Object {
	std::wstring family = L"Segoe UI";
	int size = 13;
	int weight = 400;
	FontObject() : Object(Kind::Font) {}
	HFONT font() const { return gea::win32::fontForFamily(family, size, weight); }
};

struct ImageObject : Object {
	std::string symbol;           // symbol name, or
	std::unique_ptr<Gdiplus::Bitmap> bitmap;  // a decoded file
	HBITMAP premultiplied = nullptr;
	int width = 0;
	int height = 0;
	ImageObject() : Object(Kind::Image) {}
	~ImageObject() override
	{
		if (premultiplied) DeleteObject(premultiplied);
	}
};

// Layout intent stored per view; resolved against the parent's size each pass.
struct Anchors {
	bool fill = false;
	int inset = 0;
	int leading = -1;
	int top = -1;
	int trailing = -1;
	int bottom = -1;
	bool any() const { return fill || leading >= 0 || top >= 0 || trailing >= 0 || bottom >= 0; }
};

struct ViewObject;
using ViewPtr = ViewObject *;

enum class ViewKind { View, Stack, Label, TextField, TextView, Button, CheckBox, Slider, ProgressBar, Image, Scroll, Box, Split };

struct ViewObject : Object {
	ViewKind viewKind = ViewKind::View;
	Widget *widget = nullptr;
	ViewPtr parent = nullptr;
	std::vector<ViewPtr> children;   // addSubview order
	std::vector<ViewPtr> arranged;   // stack views
	// Explicit geometry, layout px, relative to the parent.
	int x = 0;
	int y = 0;
	int width = 0;
	int height = 0;
	bool explicitFrame = false;
	Anchors anchors;
	int minWidth = 0;
	int minHeight = 0;
	int intrinsicWidth = -1;   // -1: derive from content
	int intrinsicHeight = -1;
	// Stack layout
	int orientation = 1;       // 0 horizontal, 1 vertical
	int spacing = 0;
	int alignment = 0;         // 0 leading, 1 center, 2 trailing, 3 fill
	int distribution = 0;      // 0 fill, 1 fillEqually, 2 equalSpacing, 3 gravity
	int padding[4] = {0, 0, 0, 0};  // top, leading, bottom, trailing
	bool detachesHiddenViews = true;
	// Box
	int contentInsets[4] = {0, 0, 0, 0};
	ViewPtr contentView = nullptr;
	// Text
	std::wstring text;
	std::wstring placeholder;
	FontObject *font = nullptr;
	ColorObject *textColor = nullptr;
	ColorObject *background = nullptr;
	ColorObject *border = nullptr;
	ColorObject *tint = nullptr;
	int textAlign = 0;
	int maxLines = 0;
	int lineBreak = 0;
	bool editable = true;
	bool bordered = false;
	bool hidden = false;
	double cornerRadius = 0;
	double borderWidth = 0;
	std::string tag;
	// Controls
	bool checked = false;
	double minimum = 0;
	double maximum = 100;
	double value = 0;
	bool indeterminate = false;
	ImageObject *image = nullptr;
	int contentMode = 1;
	bool drawsBackground = true;
	bool verticalScroller = true;
	bool horizontalScroller = false;
	// Callbacks
	CallbackObject *onClick = nullptr;
	CallbackObject *onChange = nullptr;
	CallbackObject *onSubmit = nullptr;
	// Split
	std::unique_ptr<gea::win32::SplitView> split;
	std::vector<ViewPtr> panes;
	ColorObject *dividerColor = nullptr;
	int dividerWidth = 1;
	// Text metrics cache
	bool dirty = true;

	ViewObject() : Object(Kind::View) {}
	~ViewObject() override
	{
		if (widget) gea::win32::destroyWidget(widget);
	}
};

struct ToolbarObject : Object {
	std::unique_ptr<gea::win32::Toolbar> toolbar;
	int height = 52;
	ToolbarObject() : Object(Kind::Toolbar) {}
};

// --- handle table glue -----------------------------------------------------------------

template <typename T>
void destroyObject(void *object)
{
	delete static_cast<T *>(object);
}

template <typename T>
double retain(T *object)
{
	return gea::windows::handles::retainRaw(object, &destroyObject<T>);
}

template <typename T>
T *get(double handle, Kind kind)
{
	auto *object = static_cast<Object *>(gea::windows::handles::object(handle));
	if (!object || object->kind != kind) return nullptr;
	return static_cast<T *>(object);
}

ViewPtr view(const WinView &wrapper) { return get<ViewObject>(wrapper.handle, Kind::View); }
ViewPtr view(double handle) { return get<ViewObject>(handle, Kind::View); }
ColorObject *color(const WinColor &wrapper) { return get<ColorObject>(wrapper.handle, Kind::Color); }
FontObject *font(const WinFont &wrapper) { return get<FontObject>(wrapper.handle, Kind::Font); }
ImageObject *image(const WinImage &wrapper) { return get<ImageObject>(wrapper.handle, Kind::Image); }
CallbackObject *callback(const WinCallback &wrapper) { return get<CallbackObject>(wrapper.handle, Kind::Callback); }
ToolbarObject *toolbar(const WinToolbar &wrapper) { return get<ToolbarObject>(wrapper.handle, Kind::Toolbar); }

WinColor makeColor(Color value)
{
	auto *object = new ColorObject();
	object->color = value;
	return WinColor(retain(object));
}

Color colorOf(ColorObject *object, Color fallback = Color{})
{
	return object ? object->color : fallback;
}

// --- the tree ---------------------------------------------------------------------------

ViewPtr g_root = nullptr;
ToolbarObject *g_toolbar = nullptr;
std::vector<ViewPtr> g_allViews;
bool g_layoutInstalled = false;
bool g_needsLayout = true;

double scale()
{
	return gea::win32::windowScale();
}

int toDevice(double layoutPx)
{
	return static_cast<int>(std::lround(layoutPx * scale()));
}

HWND hostWindow()
{
	return gea::win32::mainWindow();
}

Color labelColor()
{
	return gea::win32::darkModeEnabled() ? Color::rgb(245, 245, 247) : Color::rgb(30, 30, 30);
}

WidgetKind widgetKindFor(ViewKind kind)
{
	switch (kind) {
	case ViewKind::Label: return WidgetKind::Label;
	case ViewKind::TextField: return WidgetKind::TextField;
	case ViewKind::TextView: return WidgetKind::TextArea;
	case ViewKind::Button: return WidgetKind::Button;
	case ViewKind::CheckBox: return WidgetKind::CheckBox;
	case ViewKind::Slider: return WidgetKind::Slider;
	case ViewKind::ProgressBar: return WidgetKind::ProgressBar;
	case ViewKind::Image: return WidgetKind::Image;
	case ViewKind::Scroll: return WidgetKind::Scroll;
	default: return WidgetKind::View;
	}
}

HFONT fontFor(ViewPtr node, int fallbackSize = 13)
{
	if (node->font) return node->font->font();
	return gea::win32::fontForFamily(L"Segoe UI", fallbackSize);
}

void ensureWidget(ViewPtr node, HWND parent)
{
	if (!node->widget) {
		node->widget = gea::win32::createWidget(widgetKindFor(node->viewKind), parent);
		if (!node->widget) return;
		if (node->viewKind == ViewKind::Label) node->widget->clickTransparent = node->onClick == nullptr;
		ViewObject *self = node;
		node->widget->events.onClick = [self] {
			if (self->onClick && self->onClick->handler) self->onClick->handler();
		};
		node->widget->events.onTextChanged = [self](const std::wstring &text) {
			self->text = text;
			if (self->onChange && self->onChange->handler) self->onChange->handler();
		};
		node->widget->events.onKeyDown = [self](int keyCode) {
			if (keyCode == 13 && self->viewKind == ViewKind::TextField && self->onSubmit && self->onSubmit->handler) self->onSubmit->handler();
		};
		node->widget->events.onCheckedChanged = [self](bool checked) {
			self->checked = checked;
			if (self->onChange && self->onChange->handler) self->onChange->handler();
		};
		node->widget->events.onValueChanged = [self](double value) {
			self->value = value;
			if (self->onChange && self->onChange->handler) self->onChange->handler();
		};
		if (node->viewKind == ViewKind::Image) {
			node->widget->events.onPaintContent = [self](HDC hdc, const RECT &rect) {
				ImageObject *img = self->image;
				if (!img) return;
				if (img->premultiplied) {
					gea::win32::paintBitmap(hdc, img->premultiplied, img->width, img->height, rect, self->contentMode);
					return;
				}
				if (!img->symbol.empty()) {
					const int size = std::max(8, static_cast<int>(std::min(rect.right - rect.left, rect.bottom - rect.top)) - 2);
					HFONT glyphFont = gea::win32::symbolFont(size);
					HGDIOBJ previous = SelectObject(hdc, glyphFont);
					SetBkMode(hdc, TRANSPARENT);
					SetTextColor(hdc, colorOf(self->tint, labelColor()).ref());
					RECT text = rect;
					const std::wstring glyph = gea::win32::symbolGlyph(img->symbol);
					DrawTextW(hdc, glyph.c_str(), static_cast<int>(glyph.size()), &text, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
					SelectObject(hdc, previous);
				}
			};
		}
	} else if (GetParent(node->widget->hwnd) != parent) {
		gea::win32::setWidgetParent(node->widget, parent);
	}
}

// Content size of a leaf in layout px, the "intrinsic size" a stack view
// packs children by. Containers report the size of their arranged children.
struct Size {
	int width = 0;
	int height = 0;
};

Size measure(ViewPtr node, int availableWidth);

Size measureStack(ViewPtr node, int availableWidth)
{
	Size total;
	const bool vertical = node->orientation == 1;
	const int innerWidth = std::max(0, availableWidth - node->padding[1] - node->padding[3]);
	int count = 0;
	for (ViewPtr child : node->arranged) {
		if (child->hidden && node->detachesHiddenViews) continue;
		const Size size = measure(child, vertical ? innerWidth : innerWidth);
		if (vertical) {
			total.width = std::max(total.width, size.width);
			total.height += size.height;
		} else {
			total.height = std::max(total.height, size.height);
			total.width += size.width;
		}
		count++;
	}
	if (count > 1) {
		if (vertical) total.height += node->spacing * (count - 1);
		else total.width += node->spacing * (count - 1);
	}
	total.width += node->padding[1] + node->padding[3];
	total.height += node->padding[0] + node->padding[2];
	return total;
}

Size measure(ViewPtr node, int availableWidth)
{
	Size size;
	if (node->intrinsicWidth >= 0) size.width = node->intrinsicWidth;
	if (node->intrinsicHeight >= 0) size.height = node->intrinsicHeight;
	const bool needWidth = node->intrinsicWidth < 0;
	const bool needHeight = node->intrinsicHeight < 0;
	if (node->explicitFrame) {
		if (needWidth) size.width = node->width;
		if (needHeight) size.height = node->height;
		return size;
	}
	switch (node->viewKind) {
	case ViewKind::Label: {
		int w = 0, h = 0;
		HFONT f = fontFor(node);
		const int bound = node->maxLines == 1 ? 0 : std::max(0, availableWidth);
		gea::win32::measureText(node->text.empty() ? L" " : node->text, f, bound, node->maxLines == 1, &w, &h);
		if (node->text.empty()) w = 0;
		if (node->maxLines == 1 && availableWidth > 0) w = std::min(w, availableWidth);
		if (needWidth) size.width = w;
		if (needHeight) size.height = h;
		break;
	}
	case ViewKind::TextField: {
		if (needHeight) size.height = gea::win32::fontLineHeight(fontFor(node)) + 8;
		if (needWidth) size.width = 0;  // flexible
		break;
	}
	case ViewKind::Button:
	case ViewKind::CheckBox: {
		int w = 0, h = 0;
		gea::win32::measureText(node->text.empty() ? L" " : node->text, fontFor(node), 0, true, &w, &h);
		if (needWidth) size.width = w + (node->viewKind == ViewKind::CheckBox ? 28 : 28);
		if (needHeight) size.height = std::max(h + 10, 24);
		break;
	}
	case ViewKind::Slider:
		if (needHeight) size.height = 24;
		break;
	case ViewKind::ProgressBar:
		if (needHeight) size.height = 8;
		break;
	case ViewKind::Image:
		if (node->image) {
			if (needWidth) size.width = node->image->width > 0 ? node->image->width : 16;
			if (needHeight) size.height = node->image->height > 0 ? node->image->height : 16;
		}
		break;
	case ViewKind::Stack: {
		const Size stack = measureStack(node, availableWidth);
		if (needWidth) size.width = stack.width;
		if (needHeight) size.height = stack.height;
		break;
	}
	case ViewKind::Box: {
		if (node->contentView) {
			const Size inner = measure(node->contentView, std::max(0, availableWidth - node->contentInsets[1] - node->contentInsets[3]));
			if (needWidth) size.width = inner.width + node->contentInsets[1] + node->contentInsets[3];
			if (needHeight) size.height = inner.height + node->contentInsets[0] + node->contentInsets[2];
		}
		break;
	}
	default:
		// Plain views, scroll views, text views and split views are flexible:
		// they take what the parent gives them.
		break;
	}
	size.width = std::max(size.width, node->minWidth);
	size.height = std::max(size.height, node->minHeight);
	return size;
}

void applyStyle(ViewPtr node)
{
	Widget *widget = node->widget;
	if (!widget) return;
	WidgetStyle style = widget->style;
	style.hidden = node->hidden;
	style.background = colorOf(node->background);
	if (node->viewKind == ViewKind::Box) style.background = colorOf(node->background);
	style.borderWidth = node->borderWidth > 0 ? std::max(1, toDevice(node->borderWidth)) : 0;
	style.border = colorOf(node->border);
	const float radius = static_cast<float>(toDevice(node->cornerRadius));
	style.radius[0] = style.radius[1] = style.radius[2] = style.radius[3] = radius;
	style.text = node->text;
	style.placeholder = node->placeholder;
	style.font = fontFor(node, node->viewKind == ViewKind::TextView ? 14 : 13);
	style.textColor = colorOf(node->textColor, node->textColor ? Color{} : labelColor());
	style.textAlign = node->textAlign;
	style.maxLines = node->maxLines;
	style.lineBreak = node->lineBreak == 1 ? gea::win32::TextLineBreak::TruncateTail : node->lineBreak == 2 ? gea::win32::TextLineBreak::Clip : gea::win32::TextLineBreak::WordWrap;
	style.editable = node->editable;
	style.bordered = node->bordered;
	style.checked = node->checked;
	style.minimum = node->minimum;
	style.maximum = node->maximum;
	style.value = node->value;
	style.indeterminate = node->indeterminate;
	style.tint = colorOf(node->tint);
	style.contentMode = node->contentMode;
	style.verticalScroller = node->verticalScroller;
	style.horizontalScroller = node->horizontalScroller;
	if (node->viewKind == ViewKind::TextView && !node->drawsBackground) style.background = Color{};
	if (node->viewKind == ViewKind::Scroll && !node->drawsBackground) style.background = Color{};
	if (node->viewKind == ViewKind::Label) widget->clickTransparent = node->onClick == nullptr;
	gea::win32::applyWidgetStyle(widget, style);
	if (node->viewKind == ViewKind::Image) gea::win32::invalidateWidget(widget);
}

void layoutChildren(ViewPtr node, int width, int height);

void place(ViewPtr child, int x, int y, int width, int height)
{
	child->x = x;
	child->y = y;
	child->width = width;
	child->height = height;
	if (child->widget) gea::win32::setWidgetFrame(child->widget, toDevice(x), toDevice(y), toDevice(x + width) - toDevice(x), toDevice(y + height) - toDevice(y));
	layoutChildren(child, width, height);
}

void layoutStack(ViewPtr node, int width, int height)
{
	const bool vertical = node->orientation == 1;
	const int innerX = node->padding[1];
	const int innerY = node->padding[0];
	const int innerWidth = std::max(0, width - node->padding[1] - node->padding[3]);
	const int innerHeight = std::max(0, height - node->padding[0] - node->padding[2]);
	std::vector<ViewPtr> visible;
	for (ViewPtr child : node->arranged) {
		if (child->hidden && node->detachesHiddenViews) {
			if (child->widget) {
				WidgetStyle style = child->widget->style;
				style.hidden = true;
				gea::win32::applyWidgetStyle(child->widget, style);
			}
			continue;
		}
		visible.push_back(child);
	}
	if (visible.empty()) return;
	const int axisLength = vertical ? innerHeight : innerWidth;
	const int crossLength = vertical ? innerWidth : innerHeight;
	std::vector<Size> sizes;
	int used = 0;
	int flexible = 0;
	for (ViewPtr child : visible) {
		Size size = measure(child, vertical ? innerWidth : innerWidth);
		sizes.push_back(size);
		const int along = vertical ? size.height : size.width;
		used += along;
		if (along == 0) flexible++;
	}
	const int gaps = static_cast<int>(visible.size()) - 1;
	int spacing = node->spacing;
	int leftover = axisLength - used - spacing * gaps;
	int extraPerFlexible = 0;
	int equalLength = 0;
	switch (node->distribution) {
	case 1:  // fill equally
		equalLength = gaps >= 0 ? std::max(0, (axisLength - spacing * gaps) / static_cast<int>(visible.size())) : 0;
		break;
	case 2:  // equal spacing
		if (gaps > 0 && leftover > 0) spacing = spacing + leftover / gaps;
		break;
	case 3:  // gravity areas: pack from the leading edge
		break;
	default:  // fill: flexible children (no intrinsic size) absorb the leftover
		if (flexible > 0 && leftover > 0) extraPerFlexible = leftover / flexible;
		else if (flexible == 0 && leftover > 0 && !visible.empty()) {
			// Nothing flexible: the last child stretches, as AppKit does when
			// every arranged view has a hugging priority.
			sizes.back().height += vertical ? leftover : 0;
			sizes.back().width += vertical ? 0 : leftover;
		}
		break;
	}
	int cursor = vertical ? innerY : innerX;
	for (size_t i = 0; i < visible.size(); i++) {
		ViewPtr child = visible[i];
		Size size = sizes[i];
		int along = vertical ? size.height : size.width;
		if (node->distribution == 1) along = equalLength;
		else if (node->distribution == 0 && along == 0) along = extraPerFlexible;
		int cross = vertical ? size.width : size.height;
		if (node->alignment == 3 || cross == 0 || child->viewKind == ViewKind::TextField || child->viewKind == ViewKind::TextView ||
		    child->viewKind == ViewKind::Scroll || child->viewKind == ViewKind::Box || child->viewKind == ViewKind::Stack) {
			cross = crossLength;
		}
		cross = std::min(cross, crossLength);
		int crossOffset = vertical ? innerX : innerY;
		if (node->alignment == 1) crossOffset += (crossLength - cross) / 2;
		else if (node->alignment == 2) crossOffset += crossLength - cross;
		if (vertical) place(child, crossOffset, cursor, cross, along);
		else place(child, cursor, crossOffset, along, cross);
		cursor += along + spacing;
	}
}

void layoutChildren(ViewPtr node, int width, int height)
{
	if (node->widget) applyStyle(node);
	switch (node->viewKind) {
	case ViewKind::Stack:
		layoutStack(node, width, height);
		break;
	case ViewKind::Box:
		if (node->contentView) {
			place(node->contentView, node->contentInsets[1], node->contentInsets[0], std::max(0, width - node->contentInsets[1] - node->contentInsets[3]),
			      std::max(0, height - node->contentInsets[0] - node->contentInsets[2]));
		}
		break;
	case ViewKind::Scroll:
		if (node->contentView && node->widget) {
			// The document takes the viewport width and its content height.
			RECT client{};
			GetClientRect(node->widget->hwnd, &client);
			const int viewportWidth = static_cast<int>(std::lround((client.right - client.left) / scale()));
			const Size content = measure(node->contentView, viewportWidth);
			const int documentHeight = std::max(content.height, height);
			WidgetStyle style = node->widget->style;
			style.contentWidth = toDevice(viewportWidth);
			style.contentHeight = toDevice(documentHeight);
			gea::win32::applyWidgetStyle(node->widget, style);
			ensureWidget(node->contentView, node->widget->document);
			place(node->contentView, 0, 0, viewportWidth, documentHeight);
		}
		break;
	case ViewKind::Split:
		if (node->split && node->widget) {
			RECT bounds{0, 0, toDevice(width), toDevice(height)};
			node->split->layout(bounds, scale());
			for (size_t i = 0; i < node->panes.size() && i < node->split->paneCount(); i++) {
				ViewPtr pane = node->panes[i];
				if (!pane->widget) continue;
				RECT frame{};
				GetClientRect(pane->widget->hwnd, &frame);
				const int paneWidth = static_cast<int>(std::lround((frame.right - frame.left) / scale()));
				const int paneHeight = static_cast<int>(std::lround((frame.bottom - frame.top) / scale()));
				pane->x = 0;
				pane->y = 0;
				pane->width = paneWidth;
				pane->height = paneHeight;
				layoutChildren(pane, paneWidth, paneHeight);
			}
		}
		break;
	default:
		break;
	}
	// Free-form children: explicit frames or anchors against this view.
	for (ViewPtr child : node->children) {
		if (std::find(node->arranged.begin(), node->arranged.end(), child) != node->arranged.end()) continue;
		if (node->viewKind == ViewKind::Box && child == node->contentView) continue;
		if (node->viewKind == ViewKind::Scroll && child == node->contentView) continue;
		if (node->viewKind == ViewKind::Split) continue;
		int x = child->x;
		int y = child->y;
		int w = child->width;
		int h = child->height;
		if (child->anchors.fill) {
			x = child->anchors.inset;
			y = child->anchors.inset;
			w = std::max(0, width - 2 * child->anchors.inset);
			h = std::max(0, height - 2 * child->anchors.inset);
		} else if (child->anchors.any()) {
			const Size size = measure(child, width);
			const Anchors &a = child->anchors;
			if (a.leading >= 0 && a.trailing >= 0) {
				x = a.leading;
				w = std::max(0, width - a.leading - a.trailing);
			} else if (a.leading >= 0) {
				x = a.leading;
				w = size.width > 0 ? size.width : std::max(0, width - a.leading);
			} else if (a.trailing >= 0) {
				w = size.width > 0 ? size.width : width;
				x = std::max(0, width - a.trailing - w);
			}
			if (a.top >= 0 && a.bottom >= 0) {
				y = a.top;
				h = std::max(0, height - a.top - a.bottom);
			} else if (a.top >= 0) {
				y = a.top;
				h = size.height > 0 ? size.height : std::max(0, height - a.top);
			} else if (a.bottom >= 0) {
				h = size.height > 0 ? size.height : height;
				y = std::max(0, height - a.bottom - h);
			}
		} else if (!child->explicitFrame) {
			// No geometry stated: a bare subview fills its parent, which is
			// what a container view added with addSubview and no frame means.
			x = 0;
			y = 0;
			w = width;
			h = height;
		}
		place(child, x, y, w, h);
	}
}

void layoutRoot(const RECT &area)
{
	if (!g_root || !g_root->widget) return;
	const int width = static_cast<int>(std::lround((area.right - area.left) / scale()));
	const int height = static_cast<int>(std::lround((area.bottom - area.top) / scale()));
	g_root->x = 0;
	g_root->y = 0;
	g_root->width = width;
	g_root->height = height;
	layoutChildren(g_root, width, height);
}

void installLayout()
{
	if (g_layoutInstalled) return;
	g_layoutInstalled = true;
	gea::win32::setNativeLayoutCallback([](const RECT &area) { layoutRoot(area); });
}

void attach(ViewPtr parent, ViewPtr child)
{
	if (!parent || !child || child == parent) return;
	if (child->parent && child->parent != parent) {
		auto &siblings = child->parent->children;
		siblings.erase(std::remove(siblings.begin(), siblings.end(), child), siblings.end());
		auto &arranged = child->parent->arranged;
		arranged.erase(std::remove(arranged.begin(), arranged.end(), child), arranged.end());
	}
	child->parent = parent;
	if (std::find(parent->children.begin(), parent->children.end(), child) == parent->children.end()) parent->children.push_back(child);
	HWND host = parent->widget ? (parent->viewKind == ViewKind::Scroll && parent->widget->document ? parent->widget->document : parent->widget->hwnd) : hostWindow();
	if (parent->widget) ensureWidget(child, host);
	g_needsLayout = true;
	gea::win32::requestFrame();
}

ViewPtr newView(ViewKind kind)
{
	auto *object = new ViewObject();
	object->viewKind = kind;
	g_allViews.push_back(object);
	// Widgets are created lazily under a real parent; a detached view still
	// needs one for property writes that touch the control, so host it on the
	// main window until it is attached.
	ensureWidget(object, hostWindow());
	if (object->widget) ShowWindow(object->widget->hwnd, SW_HIDE);
	object->hidden = false;
	return object;
}

double retainView(ViewPtr object)
{
	return retain(object);
}

void markDirty()
{
	g_needsLayout = true;
	gea::win32::requestFrame();
}

}  // namespace

// --- free functions -------------------------------------------------------------------

void installRootView(WinView wrapper)
{
	ViewPtr root = view(wrapper);
	if (!root) return;
	g_root = root;
	installLayout();
	ensureWidget(root, hostWindow());
	if (root->widget) {
		WidgetStyle style = root->widget->style;
		style.hidden = false;
		gea::win32::applyWidgetStyle(root->widget, style);
		ShowWindow(root->widget->hwnd, SW_SHOWNA);
		gea::win32::installNativeRootWindow(root->widget->hwnd);
	}
	// Every attached descendant becomes visible with its parent.
	for (ViewPtr node : g_allViews) {
		if (node->widget && node->parent) ShowWindow(node->widget->hwnd, node->hidden ? SW_HIDE : SW_SHOWNA);
	}
	markDirty();
}

void installToolbar(WinToolbar wrapper)
{
	ToolbarObject *object = toolbar(wrapper);
	if (!object || !object->toolbar) return;
	g_toolbar = object;
	object->toolbar->setLayoutHeight(object->height);
	installLayout();
	gea::win32::installNativeToolbar(object->toolbar.get());
	markDirty();
}

void setWindowTitle(std::string title) { gea::win32::setMainWindowTitle(gea::win32::toWide(title)); }
void setWindowAppearance(std::string appearance) { gea::win32::setMainWindowAppearance(appearance); }
void setWindowBackgroundColor(WinColor wrapper)
{
	if (ColorObject *object = color(wrapper)) gea::win32::setMainWindowBackground(object->color);
}
void setWindowSize(double width, double height) { gea::win32::resizeMainWindowClient(static_cast<int>(width), static_cast<int>(height)); }
double mainWindowHandle() { return static_cast<double>(reinterpret_cast<std::uintptr_t>(gea::win32::mainWindow())); }
std::string runDeviceCommand(std::string command) { return gea::win32::runShellCommandBlocking(command); }
void requestFrame() { markDirty(); }
void quit() { gea::win32::quitApplication(); }

// --- WinCallback ----------------------------------------------------------------------

WinCallback WinCallback_create(std::function<void()> handler)
{
	auto *object = new CallbackObject();
	object->handler = std::move(handler);
	return WinCallback(retain(object));
}

void WinCallback_invoke(WinCallback self)
{
	if (CallbackObject *object = callback(self); object && object->handler) object->handler();
}

// --- WinColor -------------------------------------------------------------------------

WinColor WinColor_rgb(double red, double green, double blue)
{
	auto channel = [](double value) { return static_cast<int>(std::lround(std::clamp(value <= 1.0 && value >= 0.0 && value != 1.0 ? value * 255.0 : value, 0.0, 255.0))); };
	return makeColor(Color::rgb(channel(red), channel(green), channel(blue)));
}

WinColor WinColor_rgba(double red, double green, double blue, double alpha)
{
	auto channel = [](double value) { return static_cast<int>(std::lround(std::clamp(value <= 1.0 && value >= 0.0 && value != 1.0 ? value * 255.0 : value, 0.0, 255.0))); };
	const int a = static_cast<int>(std::lround(std::clamp(alpha <= 1.0 ? alpha * 255.0 : alpha, 0.0, 255.0)));
	return makeColor(Color::rgb(channel(red), channel(green), channel(blue), std::max(1, a)));
}

WinColor WinColor_fromHex(std::string hex)
{
	std::string digits = hex;
	if (!digits.empty() && digits[0] == '#') digits.erase(0, 1);
	if (digits.size() == 3) digits = {digits[0], digits[0], digits[1], digits[1], digits[2], digits[2]};
	if (digits.size() != 6 && digits.size() != 8) return makeColor(Color::rgb(0, 0, 0));
	const unsigned long value = std::strtoul(digits.c_str(), nullptr, 16);
	if (digits.size() == 8) return makeColor(Color::rgb((value >> 24) & 0xFF, (value >> 16) & 0xFF, (value >> 8) & 0xFF, std::max(1ul, value & 0xFF)));
	return makeColor(Color::rgb((value >> 16) & 0xFF, (value >> 8) & 0xFF, value & 0xFF));
}

WinColor WinColor_clear() { return makeColor(Color{}); }
WinColor WinColor_windowBackground() { return makeColor(gea::win32::windowBackground()); }
WinColor WinColor_controlBackground() { return makeColor(gea::win32::darkModeEnabled() ? Color::rgb(45, 45, 48) : Color::rgb(255, 255, 255)); }
WinColor WinColor_label() { return makeColor(labelColor()); }
WinColor WinColor_secondaryLabel() { return makeColor(gea::win32::darkModeEnabled() ? Color::rgb(160, 160, 165) : Color::rgb(96, 96, 100)); }
WinColor WinColor_tertiaryLabel() { return makeColor(gea::win32::darkModeEnabled() ? Color::rgb(120, 120, 125) : Color::rgb(140, 140, 145)); }
WinColor WinColor_separator() { return makeColor(gea::win32::darkModeEnabled() ? Color::rgb(58, 58, 62) : Color::rgb(214, 214, 218)); }
WinColor WinColor_accent()
{
	DWORD argb = 0;
	BOOL opaque = FALSE;
	typedef HRESULT(WINAPI * GetColorFn)(DWORD *, BOOL *);
	static GetColorFn getColor = reinterpret_cast<GetColorFn>(GetProcAddress(LoadLibraryW(L"dwmapi.dll"), "DwmGetColorizationColor"));
	if (getColor && SUCCEEDED(getColor(&argb, &opaque))) return makeColor(Color::rgb((argb >> 16) & 0xFF, (argb >> 8) & 0xFF, argb & 0xFF));
	return makeColor(Color::rgb(0, 120, 212));
}
WinColor WinColor_systemBlue() { return makeColor(Color::rgb(0, 120, 212)); }
WinColor WinColor_systemYellow() { return makeColor(Color::rgb(255, 185, 0)); }
WinColor WinColor_systemRed() { return makeColor(Color::rgb(232, 17, 35)); }
WinColor WinColor_systemGreen() { return makeColor(Color::rgb(16, 137, 62)); }
double WinColor_get_red(WinColor self) { return color(self) ? color(self)->color.r : 0; }
double WinColor_get_green(WinColor self) { return color(self) ? color(self)->color.g : 0; }
double WinColor_get_blue(WinColor self) { return color(self) ? color(self)->color.b : 0; }
double WinColor_get_alpha(WinColor self) { return color(self) ? color(self)->color.a / 255.0 : 0; }

// --- WinFont --------------------------------------------------------------------------

namespace {
WinFont makeFont(const std::wstring &family, double size, double weight)
{
	auto *object = new FontObject();
	object->family = family;
	object->size = std::max(1, static_cast<int>(std::lround(size)));
	// AppKit weights (-1..1) and CSS weights (100..900) both arrive here.
	int css = static_cast<int>(std::lround(weight));
	if (weight > -1.0 && weight < 1.0 && weight != 0.0) css = static_cast<int>(std::lround(400 + weight * 400));
	if (weight == 0.0) css = 400;
	object->weight = std::clamp(css, 100, 900);
	return WinFont(retain(object));
}
}  // namespace

WinFont WinFont_system(double size) { return makeFont(L"Segoe UI", size, 400); }
WinFont WinFont_systemWeight(double size, double weight) { return makeFont(L"Segoe UI", size, weight); }
WinFont WinFont_named(std::string family, double size, double weight) { return makeFont(gea::win32::faceForFamily(family), size, weight); }
WinFont WinFont_monospace(double size) { return makeFont(L"Cascadia Mono", size, 400); }
double WinFont_get_size(WinFont self) { return font(self) ? font(self)->size : 0; }
std::string WinFont_get_family(WinFont self) { return font(self) ? gea::win32::fromWide(font(self)->family) : std::string(); }
double WinFont_get_weight(WinFont self) { return font(self) ? font(self)->weight : 0; }

// --- WinImage -------------------------------------------------------------------------

WinImage WinImage_symbol(std::string name)
{
	auto *object = new ImageObject();
	object->symbol = name;
	object->width = 16;
	object->height = 16;
	return WinImage(retain(object));
}

WinImage WinImage_fromFile(std::string path)
{
	auto *object = new ImageObject();
	object->bitmap = std::make_unique<Gdiplus::Bitmap>(gea::win32::toWide(path).c_str());
	if (object->bitmap->GetLastStatus() == Gdiplus::Ok) {
		object->width = static_cast<int>(object->bitmap->GetWidth());
		object->height = static_cast<int>(object->bitmap->GetHeight());
		void *bits = nullptr;
		object->premultiplied = gea::win32::createBgraBitmap(object->width, object->height, &bits);
		Gdiplus::Rect rect(0, 0, object->width, object->height);
		Gdiplus::BitmapData data{};
		if (bits && object->bitmap->LockBits(&rect, Gdiplus::ImageLockModeRead, PixelFormat32bppPARGB, &data) == Gdiplus::Ok) {
			for (int y = 0; y < object->height; y++) {
				std::memcpy(static_cast<unsigned char *>(bits) + static_cast<size_t>(y) * object->width * 4,
				            static_cast<unsigned char *>(data.Scan0) + static_cast<size_t>(y) * data.Stride, static_cast<size_t>(object->width) * 4);
			}
			object->bitmap->UnlockBits(&data);
		}
	} else {
		object->bitmap.reset();
	}
	return WinImage(retain(object));
}
double WinImage_get_width(WinImage self) { return image(self) ? image(self)->width : 0; }
double WinImage_get_height(WinImage self) { return image(self) ? image(self)->height : 0; }

// --- WinView --------------------------------------------------------------------------

#define GEA_VIEW_PROPERTY(Class, Type, Name, Field)                                         \
	Type Class##_get_##Name(Class self) { ViewPtr node = view(self.handle); return node ? node->Field : Type{}; } \
	void Class##_set_##Name(Class self, Type value) { if (ViewPtr node = view(self.handle)) { node->Field = value; markDirty(); } }

#define GEA_VIEW_OBJECT_PROPERTY(Class, Wrapper, Name, Field, Getter, Kind)                             \
	Wrapper Class##_get_##Name(Class self)                                                                \
	{                                                                                                     \
		ViewPtr node = view(self.handle);                                                                 \
		return Wrapper(node && node->Field ? retainExisting(node->Field) : 0.0);                          \
	}                                                                                                     \
	void Class##_set_##Name(Class self, Wrapper value) { if (ViewPtr node = view(self.handle)) { node->Field = Getter(value); markDirty(); } }

namespace {
// Objects are retained once at creation; a getter hands back a fresh handle to
// the same object so the table entry stays valid for the program's lifetime.
template <typename T>
double retainExisting(T *object)
{
	return gea::windows::handles::retainRaw(object, [](void *) {});
}
std::string stringOf(const std::wstring &value) { return gea::win32::fromWide(value); }
}  // namespace

double WinView_create() { return retainView(newView(ViewKind::View)); }
bool WinView_get_hidden(WinView self) { ViewPtr node = view(self); return node ? node->hidden : false; }
void WinView_set_hidden(WinView self, bool value)
{
	if (ViewPtr node = view(self)) {
		node->hidden = value;
		if (node->widget) ShowWindow(node->widget->hwnd, value ? SW_HIDE : SW_SHOWNA);
		markDirty();
	}
}
GEA_VIEW_OBJECT_PROPERTY(WinView, WinColor, backgroundColor, background, color, Color)
GEA_VIEW_PROPERTY(WinView, double, cornerRadius, cornerRadius)
GEA_VIEW_PROPERTY(WinView, double, borderWidth, borderWidth)
GEA_VIEW_OBJECT_PROPERTY(WinView, WinColor, borderColor, border, color, Color)
GEA_VIEW_PROPERTY(WinView, std::string, tag, tag)
std::string WinView_get_tooltip(WinView) { return std::string(); }
void WinView_set_tooltip(WinView, std::string) {}
double WinView_get_x(WinView self) { ViewPtr node = view(self); return node ? node->x : 0; }
double WinView_get_y(WinView self) { ViewPtr node = view(self); return node ? node->y : 0; }
double WinView_get_width(WinView self) { ViewPtr node = view(self); return node ? node->width : 0; }
double WinView_get_height(WinView self) { ViewPtr node = view(self); return node ? node->height : 0; }
double WinView_get_handle(WinView self)
{
	ViewPtr node = view(self);
	return node && node->widget ? static_cast<double>(reinterpret_cast<std::uintptr_t>(node->widget->hwnd)) : 0;
}
WinView WinView_get_superview(WinView self)
{
	ViewPtr node = view(self);
	return WinView(node && node->parent ? retainExisting(node->parent) : 0.0);
}
void WinView_addSubview(WinView self, WinView child) { attach(view(self), view(child)); }
void WinView_removeFromSuperview(WinView self)
{
	ViewPtr node = view(self);
	if (!node || !node->parent) return;
	auto &siblings = node->parent->children;
	siblings.erase(std::remove(siblings.begin(), siblings.end(), node), siblings.end());
	auto &arranged = node->parent->arranged;
	arranged.erase(std::remove(arranged.begin(), arranged.end(), node), arranged.end());
	node->parent = nullptr;
	if (node->widget) {
		gea::win32::setWidgetParent(node->widget, hostWindow());
		ShowWindow(node->widget->hwnd, SW_HIDE);
	}
	markDirty();
}
void WinView_setFrame(WinView self, double x, double y, double width, double height)
{
	if (ViewPtr node = view(self)) {
		node->x = static_cast<int>(x);
		node->y = static_cast<int>(y);
		node->width = static_cast<int>(width);
		node->height = static_cast<int>(height);
		node->explicitFrame = true;
		markDirty();
	}
}
void WinView_setSize(WinView self, double width, double height)
{
	if (ViewPtr node = view(self)) {
		node->intrinsicWidth = static_cast<int>(width);
		node->intrinsicHeight = static_cast<int>(height);
		markDirty();
	}
}
void WinView_setMinimumSize(WinView self, double width, double height)
{
	if (ViewPtr node = view(self)) {
		node->minWidth = static_cast<int>(width);
		node->minHeight = static_cast<int>(height);
		markDirty();
	}
}
void WinView_setIntrinsicSize(WinView self, double width, double height) { WinView_setSize(self, width, height); }
void WinView_anchorFill(WinView self, double inset)
{
	if (ViewPtr node = view(self)) {
		node->anchors = Anchors{};
		node->anchors.fill = true;
		node->anchors.inset = static_cast<int>(inset);
		markDirty();
	}
}
void WinView_anchorEdges(WinView self, double leading, double top, double trailing, double bottom)
{
	if (ViewPtr node = view(self)) {
		node->anchors = Anchors{};
		node->anchors.leading = static_cast<int>(leading);
		node->anchors.top = static_cast<int>(top);
		node->anchors.trailing = static_cast<int>(trailing);
		node->anchors.bottom = static_cast<int>(bottom);
		markDirty();
	}
}
void WinView_onClick(WinView self, WinCallback handler)
{
	if (ViewPtr node = view(self)) {
		node->onClick = callback(handler);
		if (node->widget) node->widget->clickTransparent = false;
		markDirty();
	}
}
void WinView_setNeedsLayout(WinView) { markDirty(); }
void WinView_setNeedsDisplay(WinView self)
{
	if (ViewPtr node = view(self); node && node->widget) gea::win32::invalidateWidget(node->widget, true);
}

// --- WinStackView ---------------------------------------------------------------------

double WinStackView_create() { return retainView(newView(ViewKind::Stack)); }
GEA_VIEW_PROPERTY(WinStackView, double, orientation, orientation)
GEA_VIEW_PROPERTY(WinStackView, double, spacing, spacing)
GEA_VIEW_PROPERTY(WinStackView, double, alignment, alignment)
GEA_VIEW_PROPERTY(WinStackView, double, distribution, distribution)
GEA_VIEW_PROPERTY(WinStackView, bool, detachesHiddenViews, detachesHiddenViews)
double WinStackView_get_arrangedSubviewCount(WinStackView self) { ViewPtr node = view(self.handle); return node ? static_cast<double>(node->arranged.size()) : 0; }
void WinStackView_addArrangedSubview(WinStackView self, WinView child)
{
	ViewPtr node = view(self.handle);
	ViewPtr subview = view(child);
	if (!node || !subview) return;
	attach(node, subview);
	if (std::find(node->arranged.begin(), node->arranged.end(), subview) == node->arranged.end()) node->arranged.push_back(subview);
	markDirty();
}
void WinStackView_insertArrangedSubviewAtIndex(WinStackView self, WinView child, double index)
{
	ViewPtr node = view(self.handle);
	ViewPtr subview = view(child);
	if (!node || !subview) return;
	attach(node, subview);
	node->arranged.erase(std::remove(node->arranged.begin(), node->arranged.end(), subview), node->arranged.end());
	const size_t at = std::min(node->arranged.size(), static_cast<size_t>(std::max(0.0, index)));
	node->arranged.insert(node->arranged.begin() + static_cast<std::ptrdiff_t>(at), subview);
	markDirty();
}
void WinStackView_removeArrangedSubview(WinStackView self, WinView child)
{
	ViewPtr node = view(self.handle);
	ViewPtr subview = view(child);
	if (!node || !subview) return;
	node->arranged.erase(std::remove(node->arranged.begin(), node->arranged.end(), subview), node->arranged.end());
	WinView_removeFromSuperview(child);
}
void WinStackView_setPadding(WinStackView self, double top, double leading, double bottom, double trailing)
{
	if (ViewPtr node = view(self.handle)) {
		node->padding[0] = static_cast<int>(top);
		node->padding[1] = static_cast<int>(leading);
		node->padding[2] = static_cast<int>(bottom);
		node->padding[3] = static_cast<int>(trailing);
		markDirty();
	}
}

// --- WinLabel ----------------------------------------------------------------------------

double WinLabel_create() { return retainView(newView(ViewKind::Label)); }
std::string WinLabel_get_text(WinLabel self) { ViewPtr node = view(self.handle); return node ? stringOf(node->text) : std::string(); }
void WinLabel_set_text(WinLabel self, std::string value)
{
	if (ViewPtr node = view(self.handle)) {
		node->text = gea::win32::toWide(value);
		markDirty();
	}
}
GEA_VIEW_OBJECT_PROPERTY(WinLabel, WinFont, font, font, font, Font)
GEA_VIEW_OBJECT_PROPERTY(WinLabel, WinColor, textColor, textColor, color, Color)
GEA_VIEW_PROPERTY(WinLabel, double, alignment, textAlign)
GEA_VIEW_PROPERTY(WinLabel, double, maximumNumberOfLines, maxLines)
GEA_VIEW_PROPERTY(WinLabel, double, lineBreakMode, lineBreak)
bool WinLabel_get_selectable(WinLabel) { return false; }
void WinLabel_set_selectable(WinLabel, bool) {}

// --- WinTextField ------------------------------------------------------------------------

double WinTextField_create() { return retainView(newView(ViewKind::TextField)); }
std::string WinTextField_get_text(WinTextField self)
{
	ViewPtr node = view(self.handle);
	if (!node) return std::string();
	if (node->widget) return stringOf(gea::win32::widgetText(node->widget));
	return stringOf(node->text);
}
void WinTextField_set_text(WinTextField self, std::string value)
{
	if (ViewPtr node = view(self.handle)) {
		node->text = gea::win32::toWide(value);
		markDirty();
	}
}
std::string WinTextField_get_placeholder(WinTextField self) { ViewPtr node = view(self.handle); return node ? stringOf(node->placeholder) : std::string(); }
void WinTextField_set_placeholder(WinTextField self, std::string value)
{
	if (ViewPtr node = view(self.handle)) {
		node->placeholder = gea::win32::toWide(value);
		markDirty();
	}
}
GEA_VIEW_OBJECT_PROPERTY(WinTextField, WinFont, font, font, font, Font)
GEA_VIEW_OBJECT_PROPERTY(WinTextField, WinColor, textColor, textColor, color, Color)
GEA_VIEW_PROPERTY(WinTextField, double, alignment, textAlign)
GEA_VIEW_PROPERTY(WinTextField, bool, editable, editable)
GEA_VIEW_PROPERTY(WinTextField, bool, bordered, bordered)
void WinTextField_setOnChange(WinTextField self, WinCallback handler) { if (ViewPtr node = view(self.handle)) node->onChange = callback(handler); }
void WinTextField_setOnSubmit(WinTextField self, WinCallback handler) { if (ViewPtr node = view(self.handle)) node->onSubmit = callback(handler); }
void WinTextField_focus(WinTextField self) { if (ViewPtr node = view(self.handle); node && node->widget) gea::win32::focusWidget(node->widget); }
void WinTextField_selectAll(WinTextField self)
{
	if (ViewPtr node = view(self.handle); node && node->widget) SendMessageW(node->widget->hwnd, EM_SETSEL, 0, -1);
}

// --- WinTextView --------------------------------------------------------------------------

double WinTextView_create()
{
	ViewPtr node = newView(ViewKind::TextView);
	node->drawsBackground = false;
	return retainView(node);
}
std::string WinTextView_get_text(WinTextView self)
{
	ViewPtr node = view(self.handle);
	if (!node) return std::string();
	if (node->widget) return stringOf(gea::win32::widgetText(node->widget));
	return stringOf(node->text);
}
void WinTextView_set_text(WinTextView self, std::string value)
{
	if (ViewPtr node = view(self.handle)) {
		node->text = gea::win32::toWide(value);
		markDirty();
	}
}
GEA_VIEW_OBJECT_PROPERTY(WinTextView, WinFont, font, font, font, Font)
GEA_VIEW_OBJECT_PROPERTY(WinTextView, WinColor, textColor, textColor, color, Color)
GEA_VIEW_PROPERTY(WinTextView, bool, editable, editable)
GEA_VIEW_PROPERTY(WinTextView, bool, drawsBackground, drawsBackground)
void WinTextView_setOnChange(WinTextView self, WinCallback handler) { if (ViewPtr node = view(self.handle)) node->onChange = callback(handler); }
void WinTextView_focus(WinTextView self) { if (ViewPtr node = view(self.handle); node && node->widget) gea::win32::focusWidget(node->widget); }

// --- WinButton / WinCheckBox / WinSlider / WinProgressBar ---------------------------------------

double WinButton_create() { return retainView(newView(ViewKind::Button)); }
std::string WinButton_get_title(WinButton self) { ViewPtr node = view(self.handle); return node ? stringOf(node->text) : std::string(); }
void WinButton_set_title(WinButton self, std::string value) { if (ViewPtr node = view(self.handle)) { node->text = gea::win32::toWide(value); markDirty(); } }
GEA_VIEW_OBJECT_PROPERTY(WinButton, WinFont, font, font, font, Font)
GEA_VIEW_PROPERTY(WinButton, bool, enabled, editable)
GEA_VIEW_OBJECT_PROPERTY(WinButton, WinImage, image, image, image, Image)

double WinCheckBox_create() { return retainView(newView(ViewKind::CheckBox)); }
std::string WinCheckBox_get_title(WinCheckBox self) { ViewPtr node = view(self.handle); return node ? stringOf(node->text) : std::string(); }
void WinCheckBox_set_title(WinCheckBox self, std::string value) { if (ViewPtr node = view(self.handle)) { node->text = gea::win32::toWide(value); markDirty(); } }
GEA_VIEW_PROPERTY(WinCheckBox, bool, checked, checked)
void WinCheckBox_setOnChange(WinCheckBox self, WinCallback handler) { if (ViewPtr node = view(self.handle)) node->onChange = callback(handler); }

double WinSlider_create() { return retainView(newView(ViewKind::Slider)); }
GEA_VIEW_PROPERTY(WinSlider, double, minimum, minimum)
GEA_VIEW_PROPERTY(WinSlider, double, maximum, maximum)
GEA_VIEW_PROPERTY(WinSlider, double, value, value)
void WinSlider_setOnChange(WinSlider self, WinCallback handler) { if (ViewPtr node = view(self.handle)) node->onChange = callback(handler); }

double WinProgressBar_create() { return retainView(newView(ViewKind::ProgressBar)); }
GEA_VIEW_PROPERTY(WinProgressBar, double, minimum, minimum)
GEA_VIEW_PROPERTY(WinProgressBar, double, maximum, maximum)
GEA_VIEW_PROPERTY(WinProgressBar, double, value, value)
GEA_VIEW_PROPERTY(WinProgressBar, bool, indeterminate, indeterminate)

// --- WinImageView ---------------------------------------------------------------------------

double WinImageView_create() { return retainView(newView(ViewKind::Image)); }
GEA_VIEW_OBJECT_PROPERTY(WinImageView, WinImage, image, image, image, Image)
GEA_VIEW_OBJECT_PROPERTY(WinImageView, WinColor, tintColor, tint, color, Color)
GEA_VIEW_PROPERTY(WinImageView, double, contentMode, contentMode)

// --- WinScrollView --------------------------------------------------------------------------

double WinScrollView_create() { return retainView(newView(ViewKind::Scroll)); }
WinView WinScrollView_get_documentView(WinScrollView self)
{
	ViewPtr node = view(self.handle);
	return WinView(node && node->contentView ? retainExisting(node->contentView) : 0.0);
}
void WinScrollView_set_documentView(WinScrollView self, WinView document)
{
	ViewPtr node = view(self.handle);
	ViewPtr content = view(document);
	if (!node || !content) return;
	node->contentView = content;
	attach(node, content);
	markDirty();
}
GEA_VIEW_PROPERTY(WinScrollView, bool, drawsBackground, drawsBackground)
GEA_VIEW_PROPERTY(WinScrollView, bool, hasVerticalScroller, verticalScroller)
GEA_VIEW_PROPERTY(WinScrollView, bool, hasHorizontalScroller, horizontalScroller)
double WinScrollView_get_scrollTop(WinScrollView self)
{
	ViewPtr node = view(self.handle);
	return node && node->widget ? node->widget->scrollTop / scale() : 0;
}
void WinScrollView_scrollToTop(WinScrollView self) { if (ViewPtr node = view(self.handle); node && node->widget) gea::win32::setWidgetScrollTop(node->widget, 0); }
void WinScrollView_scrollTo(WinScrollView self, double y)
{
	if (ViewPtr node = view(self.handle); node && node->widget) gea::win32::setWidgetScrollTop(node->widget, toDevice(y));
}

// --- WinBox ----------------------------------------------------------------------------------

double WinBox_create() { return retainView(newView(ViewKind::Box)); }
GEA_VIEW_OBJECT_PROPERTY(WinBox, WinColor, fillColor, background, color, Color)
WinView WinBox_get_contentView(WinBox self)
{
	ViewPtr node = view(self.handle);
	return WinView(node && node->contentView ? retainExisting(node->contentView) : 0.0);
}
void WinBox_set_contentView(WinBox self, WinView content)
{
	ViewPtr node = view(self.handle);
	ViewPtr inner = view(content);
	if (!node || !inner) return;
	node->contentView = inner;
	attach(node, inner);
	markDirty();
}
void WinBox_setContentInsets(WinBox self, double top, double leading, double bottom, double trailing)
{
	if (ViewPtr node = view(self.handle)) {
		node->contentInsets[0] = static_cast<int>(top);
		node->contentInsets[1] = static_cast<int>(leading);
		node->contentInsets[2] = static_cast<int>(bottom);
		node->contentInsets[3] = static_cast<int>(trailing);
		markDirty();
	}
}

// --- WinSplitView ------------------------------------------------------------------------------

double WinSplitView_create()
{
	ViewPtr node = newView(ViewKind::Split);
	if (node->widget) node->split = std::make_unique<gea::win32::SplitView>(node->widget->hwnd);
	if (node->split) node->split->onLayoutChanged = [] { markDirty(); };
	return retainView(node);
}
GEA_VIEW_OBJECT_PROPERTY(WinSplitView, WinColor, dividerColor, dividerColor, color, Color)
GEA_VIEW_PROPERTY(WinSplitView, double, dividerWidth, dividerWidth)
void WinSplitView_addPane(WinSplitView self, WinView paneWrapper, double role, double minimumThickness, double maximumThickness, bool canCollapse)
{
	ViewPtr node = view(self.handle);
	ViewPtr pane = view(paneWrapper);
	if (!node || !pane || !node->split) return;
	pane->parent = node;
	node->children.push_back(pane);
	node->panes.push_back(pane);
	ensureWidget(pane, node->split->hwnd());
	if (pane->widget) ShowWindow(pane->widget->hwnd, SW_SHOWNA);
	node->split->addPane(pane->widget ? pane->widget->hwnd : nullptr, static_cast<int>(role), static_cast<int>(minimumThickness),
	                     static_cast<int>(maximumThickness), canCollapse);
	if (node->dividerColor) node->split->setDividerColor(node->dividerColor->color);
	markDirty();
}
void WinSplitView_toggleSidebar(WinSplitView self)
{
	if (ViewPtr node = view(self.handle); node && node->split) node->split->toggleSidebar();
}
void WinSplitView_setPaneThickness(WinSplitView self, double index, double thickness)
{
	if (ViewPtr node = view(self.handle); node && node->split) node->split->setPaneThickness(static_cast<size_t>(std::max(0.0, index)), static_cast<int>(thickness));
}

// --- WinToolbar ---------------------------------------------------------------------------------

double WinToolbar_create()
{
	auto *object = new ToolbarObject();
	object->toolbar = std::make_unique<gea::win32::Toolbar>(hostWindow());
	return retain(object);
}
double WinToolbar_get_height(WinToolbar self) { ToolbarObject *object = toolbar(self); return object ? object->height : 0; }
void WinToolbar_set_height(WinToolbar self, double value)
{
	if (ToolbarObject *object = toolbar(self)) {
		object->height = static_cast<int>(value);
		if (object->toolbar) object->toolbar->setLayoutHeight(object->height);
		markDirty();
	}
}
WinColor WinToolbar_get_backgroundColor(WinToolbar) { return makeColor(gea::win32::windowBackground()); }
void WinToolbar_set_backgroundColor(WinToolbar self, WinColor value)
{
	if (ToolbarObject *object = toolbar(self); object && object->toolbar && color(value)) object->toolbar->setBackground(color(value)->color);
}
void WinToolbar_addItem(WinToolbar self, std::string symbol, std::string label, WinCallback action)
{
	ToolbarObject *object = toolbar(self);
	if (!object || !object->toolbar) return;
	CallbackObject *handler = callback(action);
	object->toolbar->addItem(symbol, gea::win32::toWide(label), [handler] {
		if (handler && handler->handler) handler->handler();
	});
	markDirty();
}
void WinToolbar_addSidebarToggle(WinToolbar self, WinSplitView splitWrapper)
{
	ToolbarObject *object = toolbar(self);
	ViewPtr split = view(splitWrapper.handle);
	if (!object || !object->toolbar) return;
	object->toolbar->addSidebarToggle([split] {
		if (split && split->split) split->split->toggleSidebar();
	});
	markDirty();
}
void WinToolbar_addSpace(WinToolbar self)
{
	if (ToolbarObject *object = toolbar(self); object && object->toolbar) object->toolbar->addSpace();
}
void WinToolbar_addSearchField(WinToolbar self, std::string placeholder, WinCallback onChange)
{
	ToolbarObject *object = toolbar(self);
	if (!object || !object->toolbar) return;
	CallbackObject *handler = callback(onChange);
	object->toolbar->addSearchField(gea::win32::toWide(placeholder), [handler](const std::wstring &) {
		if (handler && handler->handler) handler->handler();
	});
	markDirty();
}
std::string WinToolbar_searchText(WinToolbar self)
{
	ToolbarObject *object = toolbar(self);
	return object && object->toolbar ? gea::win32::fromWide(object->toolbar->searchText()) : std::string();
}

}  // namespace gea::windows::Controls
