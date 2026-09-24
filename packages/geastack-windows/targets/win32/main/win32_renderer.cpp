// SPDX-License-Identifier: Apache-2.0
// See win32_renderer.h. What gets a window and what gets painted:
//
//   painted into the nearest surface (no window of its own):
//     View                    -> box: background, border, radii, opacity
//     Text                    -> GDI text with the node's font/colour/alignment
//     Button with a background-> box painted with its title, darkened on press
//     Image                   -> the ImageStore bitmap
//     Canvas                  -> the element's RGB565 pixels, stretched
//     <symbol data-symbol>    -> a Segoe Fluent Icons glyph centred on its ink
//     <vibrancy>              -> box with a material-tinted background
//
//   a window of its own (a child of the surface it sits in):
//     the root of a sync      -> GeaView surface painting its whole subtree
//     View with overflow:scroll, VirtualList
//                             -> GeaScroll; its document window is a surface
//                                painting the scrolled content
//     Button                  -> native BUTTON
//     <input>                 -> EDIT (type=range -> trackbar, type=checkbox -> check box)
//     <textarea>              -> multi-line EDIT
//     <progress>              -> progress bar
//
// A surface repaints when the paint signature of its subtree (geometry,
// style, text, images) changes, or every frame while it holds a canvas.
// Presses on a surface are hit-tested against the painted tree, and the
// press sequence (touchstart, touchend, click) fires at the deepest painted
// node under the pointer, text and symbols being transparent to it; per-node
// listeners bound on document.body pick that up by event.target.

#include "win32_renderer.h"

#include "canvas.h"
#include "events.h"
#include "host/display_orientation.h"
#include "ui/node_model.h"
#include "ui/tree_events.h"
#include "ui/tree_internal.h"
#include "win32_color.h"
#include "win32_font_registry.h"
#include "win32_image_bridge.h"
#include "win32_press_bridge.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace gea::win32 {

namespace {

using gea::embedded::ui::Node;
using gea::embedded::ui::NodeType;
using gea::embedded::ui::Tree;

double g_scale = 1.0;

// Nodes that own a window (surfaces, scroll containers, native controls).
std::unordered_map<int, Widget *> &nodeWidgets()
{
	static std::unordered_map<int, Widget *> map;
	return map;
}

int toDevice(double layoutPx)
{
	return static_cast<int>(std::lround(layoutPx * g_scale));
}

const char *tagOf(int nodeId)
{
	const char *tag = Tree::instance().tagName(nodeId);
	return tag ? tag : "";
}

const char *attribute(int nodeId, const char *name)
{
	return Tree::instance().getAttribute(nodeId, name);
}

void fireEvent(int nodeId, gea::framework::events::PointerEventType type, int keyCode = 0)
{
	using gea::framework::events::PointerEvent;
	if (nodeId < 0) return;
	auto &tree = Tree::instance();
	if (nodeId >= tree.nodeCount()) return;
	PointerEvent event;
	event.type = type;
	event.targetId = nodeId;
	event.keyCode = keyCode;
	tree.dispatchEvent(event);
}

// --- what a node becomes ---------------------------------------------------------

struct Materialization {
	WidgetKind kind = WidgetKind::View;
	bool styledButton = false;
	bool symbol = false;
	bool vibrancy = false;
};

Materialization materializationFor(int nodeId, const Node &node)
{
	Materialization out;
	const char *tag = tagOf(nodeId);
	if (node.type == NodeType::View && std::strcmp(tag, "input") == 0) {
		const char *type = attribute(nodeId, "type");
		if (type && std::strcmp(type, "range") == 0) out.kind = WidgetKind::Slider;
		else if (type && std::strcmp(type, "checkbox") == 0) out.kind = WidgetKind::CheckBox;
		else out.kind = WidgetKind::TextField;
		return out;
	}
	if (node.type == NodeType::View && std::strcmp(tag, "textarea") == 0) {
		out.kind = WidgetKind::TextArea;
		return out;
	}
	if (node.type == NodeType::View && std::strcmp(tag, "vibrancy") == 0) {
		out.vibrancy = true;
		return out;
	}
	if (node.type == NodeType::View && std::strcmp(tag, "symbol") == 0) {
		out.kind = WidgetKind::Label;
		out.symbol = true;
		return out;
	}
	if (node.type == NodeType::View && std::strcmp(tag, "progress") == 0) {
		out.kind = WidgetKind::ProgressBar;
		return out;
	}
	switch (node.type) {
	case NodeType::Text: out.kind = WidgetKind::Label; break;
	case NodeType::Button:
		if (node.style.has_bg) {
			out.kind = WidgetKind::View;
			out.styledButton = true;
		} else {
			out.kind = WidgetKind::Button;
		}
		break;
	case NodeType::Image: out.kind = WidgetKind::Image; break;
	case NodeType::Canvas: out.kind = WidgetKind::Canvas; break;
	case NodeType::VirtualList: out.kind = WidgetKind::Scroll; break;
	case NodeType::View:
	default:
		out.kind = node.style.overflow == 2 ? WidgetKind::Scroll : WidgetKind::View;
		break;
	}
	return out;
}

// True for the kinds that need a window of their own inside a surface.
bool ownsWindow(const Materialization &materialization)
{
	switch (materialization.kind) {
	case WidgetKind::Button:
	case WidgetKind::CheckBox:
	case WidgetKind::TextField:
	case WidgetKind::TextArea:
	case WidgetKind::Slider:
	case WidgetKind::ProgressBar:
	case WidgetKind::Scroll:
		return true;
	default:
		return false;
	}
}

// Text a button shows: its Text child, or every descendant text run joined.
std::string buttonTitle(const Node &node)
{
	Tree &tree = Tree::instance();
	std::string title;
	std::vector<int> stack;
	for (int child = node.first_child; child >= 0; child = tree.node(child).next_sibling) stack.push_back(child);
	std::reverse(stack.begin(), stack.end());
	while (!stack.empty()) {
		const int id = stack.back();
		stack.pop_back();
		const Node &current = tree.node(id);
		if (current.type == NodeType::Text && !current.text.empty()) {
			if (!title.empty()) title.push_back(' ');
			title += current.text;
		}
		std::vector<int> children;
		for (int child = current.first_child; child >= 0; child = tree.node(child).next_sibling) children.push_back(child);
		for (auto it = children.rbegin(); it != children.rend(); ++it) stack.push_back(*it);
	}
	return title;
}

void resolveRadii(const Node &node, int deviceWidth, int deviceHeight, float out[4])
{
	const double width = std::max(0, deviceWidth);
	const double height = std::max(0, deviceHeight);
	double radii[4];
	for (int i = 0; i < 4; ++i) {
		if (node.style.border_radius_percent[i] != gea::embedded::ui::kUnset) {
			const double percent = node.style.border_radius_percent[i] / 1000.0;
			radii[i] = std::max(0.0, std::min(width, height) * percent);
		} else {
			radii[i] = std::max(0, static_cast<int>(node.style.border_radius[i])) * g_scale;
		}
	}
	double scale = 1.0;
	auto constrain = [&](double limit, double sum) {
		if (limit > 0 && sum > limit) scale = std::min(scale, limit / sum);
	};
	constrain(width, radii[0] + radii[1]);
	constrain(width, radii[3] + radii[2]);
	constrain(height, radii[0] + radii[3]);
	constrain(height, radii[1] + radii[2]);
	for (int i = 0; i < 4; ++i) out[i] = static_cast<float>(radii[i] * scale);
}

Color vibrancyColor(int nodeId)
{
	const char *appearance = attribute(nodeId, "data-appearance");
	const bool dark = appearance ? std::strcmp(appearance, "dark") == 0 : darkModeEnabled();
	const char *material = attribute(nodeId, "data-material");
	// Approximations of the AppKit materials over the window background:
	// the sidebar is slightly lighter than the content in dark mode.
	if (material && std::strcmp(material, "sidebar") == 0) return dark ? Color::rgb(40, 40, 42) : Color::rgb(236, 236, 238);
	if (material && std::strcmp(material, "header") == 0) return dark ? Color::rgb(44, 44, 46) : Color::rgb(242, 242, 244);
	return dark ? Color::rgb(36, 36, 38) : Color::rgb(240, 240, 242);
}

// The node's own box, as painted or as applied to its window's style.
BoxPaint boxFor(const Node &node, int nodeId, const Materialization &materialization, int deviceWidth, int deviceHeight)
{
	BoxPaint box;
	box.opacity = node.style.opacity;
	if (materialization.vibrancy) {
		box.fill = vibrancyColor(nodeId);
	} else if (node.style.has_bg) {
		box.fill = colorFromStyle(node.style.bg_color, node.style.bg_alpha);
	}
	if (node.style.border_width > 0) {
		box.borderWidth = std::max(1, toDevice(node.style.border_width));
		box.border = colorFromStyle(node.style.border_color, node.style.border_alpha);
	}
	resolveRadii(node, deviceWidth, deviceHeight, box.radius);
	return box;
}

void applyCommonStyle(WidgetStyle &style, const Node &node, int deviceWidth, int deviceHeight, const Materialization &materialization, int nodeId)
{
	const BoxPaint box = boxFor(node, nodeId, materialization, deviceWidth, deviceHeight);
	style.hidden = node.style.display == gea::embedded::ui::kDisplayNone;
	style.opacity = box.opacity;
	style.background = box.fill;
	style.border = box.border;
	style.borderWidth = box.borderWidth;
	for (int i = 0; i < 4; ++i) style.radius[i] = box.radius[i];
	style.clipChildren = true;
}

void applyTextStyle(WidgetStyle &style, const Node &node)
{
	style.font = fontForId(node.style.font_id, node.style.font_size > 0 ? node.style.font_size : 13, node.style.font_weight);
	style.textColor = node.style.text_color != 0 || node.style.text_alpha != 0 ? colorFromStyle(node.style.text_color, 255) : Color{};
	// text_color 0 is black in RGB565 as well as "unset"; the style system
	// resolves an unset colour to the default, so treat 0 as black when the
	// node has any explicit colour alpha.
	if (node.style.text_color == 0) style.textColor = Color::rgb(0, 0, 0);
	style.textAlign = node.style.text_align;
	style.textDecoration = node.style.text_decoration;
	style.lineBreak = node.style.text_overflow == 1 ? TextLineBreak::TruncateTail : TextLineBreak::WordWrap;
	style.maxLines = node.style.white_space == 1 ? 1 : 0;
	style.opacity = node.style.opacity;
}

WidgetStyle symbolStyle(const Node &node, int nodeId, int deviceWidth, int deviceHeight)
{
	WidgetStyle style;
	const char *symbol = attribute(nodeId, "data-symbol");
	style.text = symbolGlyph(symbol ? symbol : "");
	const int size = std::max(8, std::min(deviceWidth, deviceHeight) - 2);
	style.font = symbolFont(size);
	style.textColor = node.style.text_color != 0 ? colorFromStyle(node.style.text_color) : (darkModeEnabled() ? Color::rgb(235, 235, 240) : Color::rgb(30, 30, 30));
	style.textAlign = 1;
	style.maxLines = 1;
	style.symbol = true;
	style.opacity = node.style.opacity;
	return style;
}

// --- canvas ---------------------------------------------------------------------------

void paintCanvasNode(HDC hdc, const RECT &rect, int nodeId)
{
	auto &tree = Tree::instance();
	const auto *canvas = tree.canvas(nodeId);
	if (!canvas) return;
	const std::uint16_t *source = canvas->pixels();
	const int width = canvas->width();
	const int height = canvas->height();
	if (!source || width <= 0 || height <= 0) return;
	static std::vector<std::uint32_t> scratch;
	scratch.resize(static_cast<size_t>(width) * static_cast<size_t>(height));
	for (int i = 0; i < width * height; i++) {
		int r = 0, g = 0, b = 0;
		gea::framework::graphics::pixel::unpackRgb565(source[i], &r, &g, &b);
		scratch[static_cast<size_t>(i)] = (static_cast<std::uint32_t>((r * 255 + 15) / 31) << 16) | (static_cast<std::uint32_t>((g * 255 + 31) / 63) << 8) |
		                                  static_cast<std::uint32_t>((b * 255 + 15) / 31);
	}
	BITMAPINFO info{};
	info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	info.bmiHeader.biWidth = width;
	info.bmiHeader.biHeight = -height;
	info.bmiHeader.biPlanes = 1;
	info.bmiHeader.biBitCount = 32;
	info.bmiHeader.biCompression = BI_RGB;
	SetStretchBltMode(hdc, HALFTONE);
	StretchDIBits(hdc, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top, 0, 0, width, height, scratch.data(), &info, DIB_RGB_COLORS,
	              SRCCOPY);
}

// A pointer phase (1 down, 2 move, 3 up) at (x, y) relative to the canvas box.
void canvasPointer(int nodeId, const RECT &rect, int phase, int x, int y)
{
	auto &tree = Tree::instance();
	if (nodeId < 0 || nodeId >= tree.nodeCount()) return;
	const auto *canvas = tree.canvas(nodeId);
	const int width = static_cast<int>(rect.right - rect.left);
	const int height = static_cast<int>(rect.bottom - rect.top);
	if (!canvas || width <= 0 || height <= 0 || canvas->width() <= 0 || canvas->height() <= 0) return;
	// Box point -> canvas pixel grid (the blit stretches the canvas into the
	// box, so input applies the inverse stretch).
	const int lx = std::clamp(static_cast<int>(std::lround(static_cast<double>(x) * canvas->width() / width)), 0, canvas->width() - 1);
	const int ly = std::clamp(static_cast<int>(std::lround(static_cast<double>(y) * canvas->height() / height)), 0, canvas->height() - 1);
	namespace od = gea::framework::display::detail;
	using gea::framework::display::DisplayOrientation;
	int px = lx;
	int py = ly;
	switch (od::DisplayOrientationState::orientation()) {
	case DisplayOrientation::LandscapePrimary:
		px = ly;
		py = od::DisplayOrientationState::nativeHeight() - 1 - lx;
		break;
	case DisplayOrientation::LandscapeSecondary:
		px = od::DisplayOrientationState::nativeWidth() - 1 - ly;
		py = lx;
		break;
	case DisplayOrientation::PortraitSecondary:
		px = od::DisplayOrientationState::nativeWidth() - 1 - lx;
		py = od::DisplayOrientationState::nativeHeight() - 1 - ly;
		break;
	default:
		break;
	}
	const bool touching = phase != 3;
	gea_win32_touch_set_state(touching ? 1 : 0, px, py);
	gea::framework::events::Event event{};
	event.type = gea::framework::events::EventType::Touch;
	event.touchPhase = phase == 1 ? gea::framework::events::TouchPhase::Down
	                 : phase == 2 ? gea::framework::events::TouchPhase::Move
	                              : gea::framework::events::TouchPhase::Up;
	event.touching = touching;
	event.x = lx;
	event.y = ly;
	event.pointerId = 0;
	gea::framework::events::TouchRuntime::dispatchEvent(event);
}

// --- surfaces ------------------------------------------------------------------------
//
// A surface is a GeaView window that paints the subtree of its owner node:
// the root of a sync (the window is the owner's own box; the overlay paints
// the descendants) or a scroll container's document window (the overlay
// paints the scrolled children). Device coordinates inside a surface are
// relative to the owner's layout origin.

struct SurfaceState {
	int owner = -1;
	std::uint64_t paintHash = 0;
	bool hasCanvas = false;
	int pressedNode = -1;
	int focusedNode = -1;
	bool canvasPressed = false;
};

std::unordered_map<Widget *, SurfaceState> &surfaces()
{
	static std::unordered_map<Widget *, SurfaceState> map;
	return map;
}

struct Origin {
	int x = 0;
	int y = 0;
};

Origin originOf(const Node &owner)
{
	return Origin{toDevice(owner.layout.x), toDevice(owner.layout.y)};
}

RECT deviceRect(const Node &node, const Origin &origin)
{
	RECT rect;
	rect.left = toDevice(node.layout.x) - origin.x;
	rect.top = toDevice(node.layout.y) - origin.y;
	rect.right = toDevice(node.layout.x + node.layout.width) - origin.x;
	rect.bottom = toDevice(node.layout.y + node.layout.height) - origin.y;
	return rect;
}

bool clipsChildren(const Node &node)
{
	return node.style.overflow == 1;
}

std::vector<int> childrenByStacking(const Node &node)
{
	Tree &tree = Tree::instance();
	std::vector<std::pair<int, int>> children;  // z-index, id
	for (int child = node.first_child; child >= 0; child = tree.node(child).next_sibling) {
		children.emplace_back(tree.node(child).style.z_index, child);
	}
	std::stable_sort(children.begin(), children.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
	std::vector<int> ordered;
	ordered.reserve(children.size());
	for (const auto &entry : children) ordered.push_back(entry.second);
	return ordered;
}

// --- painting ------------------------------------------------------------------------

struct PaintContext {
	HDC hdc = nullptr;
	Origin origin;
	int pressedNode = -1;
};

void paintChildren(const PaintContext &context, const Node &parent, Color under);

void paintNode(const PaintContext &context, int nodeId, Color under)
{
	Tree &tree = Tree::instance();
	const Node &node = tree.node(nodeId);
	if (node.style.display == gea::embedded::ui::kDisplayNone) return;
	const Materialization materialization = materializationFor(nodeId, node);
	if (ownsWindow(materialization)) return;  // its window paints it
	const RECT rect = deviceRect(node, context.origin);
	const int width = static_cast<int>(rect.right - rect.left);
	const int height = static_cast<int>(rect.bottom - rect.top);

	if (node.type == NodeType::Text) {
		if (node.text.empty()) return;
		WidgetStyle style;
		style.text = toWide(node.text);
		applyTextStyle(style, node);
		drawText(context.hdc, rect, style, under);
		return;
	}

	BoxPaint box = boxFor(node, nodeId, materialization, width, height);
	box.pressed = materialization.styledButton && context.pressedNode == nodeId;
	drawBox(context.hdc, rect, box);
	Color next = under;
	if (box.fill.set()) {
		const int alpha = box.fill.a * box.opacity / 255;
		next = alpha >= 255 ? Color::rgb(box.fill.r, box.fill.g, box.fill.b) : blend(under, Color::rgb(box.fill.r, box.fill.g, box.fill.b, alpha));
	}

	if (materialization.symbol) {
		drawText(context.hdc, rect, symbolStyle(node, nodeId, width, height), next);
		return;
	}
	if (node.type == NodeType::Image) {
		int bitmapWidth = 0;
		int bitmapHeight = 0;
		if (HBITMAP bitmap = bitmapForImageId(node.image_id, &bitmapWidth, &bitmapHeight)) {
			paintBitmap(context.hdc, bitmap, bitmapWidth, bitmapHeight, rect, node.style.image_fit, node.style.opacity);
		}
		return;
	}
	if (node.type == NodeType::Canvas) {
		paintCanvasNode(context.hdc, rect, nodeId);
		return;
	}
	if (materialization.styledButton) {
		// The title sits centred in the box, the way a bezel would show it.
		WidgetStyle style;
		style.text = toWide(buttonTitle(node));
		applyTextStyle(style, node);
		style.textAlign = 1;
		style.maxLines = 1;
		RECT text = rect;
		text.left += 4;
		text.right -= 4;
		if (style.font) {
			HGDIOBJ previous = SelectObject(context.hdc, style.font);
			RECT measure = text;
			DrawTextW(context.hdc, style.text.c_str(), -1, &measure, DT_CALCRECT | DT_NOPREFIX | DT_SINGLELINE);
			SelectObject(context.hdc, previous);
			text.top += std::max(0, (height - static_cast<int>(measure.bottom - measure.top)) / 2);
		}
		drawText(context.hdc, text, style, next);
		return;
	}

	const bool clip = clipsChildren(node);
	int saved = 0;
	if (clip) {
		saved = SaveDC(context.hdc);
		IntersectClipRect(context.hdc, rect.left, rect.top, rect.right, rect.bottom);
	}
	paintChildren(context, node, next);
	if (clip) RestoreDC(context.hdc, saved);
}

void paintChildren(const PaintContext &context, const Node &parent, Color under)
{
	for (int child : childrenByStacking(parent)) paintNode(context, child, under);
}

void paintSurface(Widget *surface, HDC hdc)
{
	auto it = surfaces().find(surface);
	if (it == surfaces().end()) return;
	const SurfaceState &state = it->second;
	Tree &tree = Tree::instance();
	if (state.owner < 0 || state.owner >= tree.nodeCount()) return;
	const Node &owner = tree.node(state.owner);
	PaintContext context;
	context.hdc = hdc;
	context.origin = originOf(owner);
	context.pressedNode = state.pressedNode;
	if (owner.type == NodeType::Canvas) {
		RECT client{};
		GetClientRect(surface->hwnd, &client);
		paintCanvasNode(hdc, client, state.owner);
	}
	paintChildren(context, owner, effectiveBackground(surface->hwnd));
}

// --- hit testing and presses -----------------------------------------------------------

// The deepest painted node under `point`, or -1. Text and symbols are
// transparent to presses (the parent takes them), as is pointer-events: none.
int hitTestNode(int nodeId, const Origin &origin, POINT point)
{
	Tree &tree = Tree::instance();
	const Node &node = tree.node(nodeId);
	if (node.style.display == gea::embedded::ui::kDisplayNone) return -1;
	const Materialization materialization = materializationFor(nodeId, node);
	if (ownsWindow(materialization)) return -1;  // its window took the click
	RECT rect = deviceRect(node, origin);
	const bool inside = PtInRect(&rect, point) != FALSE;
	if (clipsChildren(node) && !inside) return -1;
	if (node.type != NodeType::Text && !materialization.styledButton) {
		const std::vector<int> children = childrenByStacking(node);
		for (auto it = children.rbegin(); it != children.rend(); ++it) {
			const int hit = hitTestNode(*it, origin, point);
			if (hit >= 0) return hit;
		}
	}
	if (!inside) return -1;
	if (node.type == NodeType::Text || materialization.symbol || node.style.pointer_events == 1) return -1;
	return nodeId;
}

int hitTestSurface(const SurfaceState &state, POINT point)
{
	Tree &tree = Tree::instance();
	if (state.owner < 0 || state.owner >= tree.nodeCount()) return -1;
	const Node &owner = tree.node(state.owner);
	const Origin origin = originOf(owner);
	const std::vector<int> children = childrenByStacking(owner);
	for (auto it = children.rbegin(); it != children.rend(); ++it) {
		const int hit = hitTestNode(*it, origin, point);
		if (hit >= 0) return hit;
	}
	return owner.style.pointer_events == 1 ? -1 : state.owner;
}

void surfacePointer(Widget *surface, int phase, int x, int y)
{
	auto it = surfaces().find(surface);
	if (it == surfaces().end()) return;
	SurfaceState &state = it->second;
	Tree &tree = Tree::instance();
	if (state.owner < 0 || state.owner >= tree.nodeCount()) return;
	const Origin origin = originOf(tree.node(state.owner));
	const POINT point{x, y};
	auto rectOf = [&](int nodeId) { return deviceRect(tree.node(nodeId), origin); };
	auto isStyledButton = [&](int nodeId) {
		return nodeId >= 0 && nodeId < tree.nodeCount() && materializationFor(nodeId, tree.node(nodeId)).styledButton;
	};
	switch (phase) {
	case 1: {
		const int target = hitTestSurface(state, point);
		state.pressedNode = target;
		state.focusedNode = target;
		if (target < 0) return;
		if (tree.node(target).type == NodeType::Canvas) {
			state.canvasPressed = true;
			const RECT rect = rectOf(target);
			canvasPointer(target, rect, 1, x - rect.left, y - rect.top);
			return;
		}
		fireEvent(target, gea::framework::events::PointerEventType::TouchStart);
		if (isStyledButton(target)) InvalidateRect(surface->hwnd, nullptr, FALSE);
		return;
	}
	case 2: {
		if (state.canvasPressed && state.pressedNode >= 0 && state.pressedNode < tree.nodeCount()) {
			const RECT rect = rectOf(state.pressedNode);
			canvasPointer(state.pressedNode, rect, 2, x - rect.left, y - rect.top);
		}
		return;
	}
	default: {
		const int pressed = state.pressedNode;
		state.pressedNode = -1;
		if (pressed < 0 || pressed >= tree.nodeCount()) {
			state.canvasPressed = false;
			return;
		}
		if (state.canvasPressed) {
			state.canvasPressed = false;
			const RECT rect = rectOf(pressed);
			canvasPointer(pressed, rect, 3, x - rect.left, y - rect.top);
			return;
		}
		fireEvent(pressed, gea::framework::events::PointerEventType::TouchEnd);
		if (phase == 3) {
			RECT rect = rectOf(pressed);
			if (PtInRect(&rect, point)) fireEvent(pressed, gea::framework::events::PointerEventType::Click);
		}
		if (isStyledButton(pressed)) InvalidateRect(surface->hwnd, nullptr, FALSE);
		return;
	}
	}
}

void surfaceKey(Widget *surface, int keyCode)
{
	auto it = surfaces().find(surface);
	if (it == surfaces().end()) return;
	const SurfaceState &state = it->second;
	fireEvent(state.focusedNode >= 0 ? state.focusedNode : state.owner, gea::framework::events::PointerEventType::KeyDown, keyCode);
}

// --- paint signature -----------------------------------------------------------------

struct Hasher {
	std::uint64_t hash = 1469598103934665603ULL;
	void bytes(const void *data, size_t size)
	{
		const auto *p = static_cast<const unsigned char *>(data);
		for (size_t i = 0; i < size; ++i) {
			hash ^= p[i];
			hash *= 1099511628211ULL;
		}
	}
	template <typename T>
	void value(const T &v)
	{
		bytes(&v, sizeof(v));
	}
	void text(const char *s)
	{
		if (!s) s = "";
		bytes(s, std::strlen(s) + 1);
	}
};

void hashNode(Hasher &hasher, const Node &node, int nodeId, const Materialization &materialization)
{
	hasher.value(nodeId);
	hasher.value(node.type);
	hasher.value(node.layout.x);
	hasher.value(node.layout.y);
	hasher.value(node.layout.width);
	hasher.value(node.layout.height);
	const auto &s = node.style;
	hasher.value(s.display);
	hasher.value(s.opacity);
	hasher.value(s.has_bg);
	hasher.value(s.bg_color);
	hasher.value(s.bg_alpha);
	hasher.value(s.border_width);
	hasher.value(s.border_color);
	hasher.value(s.border_alpha);
	hasher.bytes(s.border_radius, sizeof(s.border_radius));
	hasher.bytes(s.border_radius_percent, sizeof(s.border_radius_percent));
	hasher.value(s.text_color);
	hasher.value(s.text_alpha);
	hasher.value(s.font_id);
	hasher.value(s.font_size);
	hasher.value(s.font_weight);
	hasher.value(s.text_align);
	hasher.value(s.text_decoration);
	hasher.value(s.white_space);
	hasher.value(s.text_overflow);
	hasher.value(s.overflow);
	hasher.value(s.image_fit);
	hasher.value(s.z_index);
	hasher.value(node.image_id);
	hasher.bytes(node.text.data(), node.text.size());
	hasher.value(node.text.size());
	if (materialization.symbol) hasher.text(attribute(nodeId, "data-symbol"));
	if (materialization.vibrancy) {
		hasher.text(attribute(nodeId, "data-material"));
		hasher.text(attribute(nodeId, "data-appearance"));
	}
}

// --- windows ---------------------------------------------------------------------------

void configureEvents(Widget *widget, int nodeId)
{
	widget->nodeId = nodeId;
	switch (widget->kind) {
	case WidgetKind::Button:
		widget->events.onClick = [nodeId] { gea_win32_fire_press_for_node(nodeId); };
		break;
	case WidgetKind::TextField:
	case WidgetKind::TextArea:
		widget->events.onTextChanged = [nodeId](const std::wstring &text) {
			auto &tree = Tree::instance();
			if (nodeId >= tree.nodeCount()) return;
			// Mirror the text into `value` so onInput handlers reading
			// event.currentTarget.value see it, then dispatch `input`.
			tree.setAttribute(nodeId, "value", fromWide(text).c_str());
			fireEvent(nodeId, gea::framework::events::PointerEventType::Input);
		};
		widget->events.onKeyDown = [nodeId](int keyCode) { fireEvent(nodeId, gea::framework::events::PointerEventType::KeyDown, keyCode); };
		break;
	case WidgetKind::Slider:
		widget->events.onValueChanged = [nodeId](double value) {
			auto &tree = Tree::instance();
			if (nodeId >= tree.nodeCount()) return;
			tree.setAttribute(nodeId, "value", std::to_string(static_cast<int>(std::lround(value))).c_str());
			fireEvent(nodeId, gea::framework::events::PointerEventType::Input);
		};
		break;
	case WidgetKind::CheckBox:
		widget->events.onCheckedChanged = [nodeId](bool checked) {
			auto &tree = Tree::instance();
			if (nodeId >= tree.nodeCount()) return;
			tree.setAttribute(nodeId, "checked", checked ? "true" : "false");
			fireEvent(nodeId, gea::framework::events::PointerEventType::Input);
		};
		break;
	default:
		break;
	}
}

Widget *ensureWidget(int nodeId, WidgetKind kind, HWND parent, bool *created)
{
	*created = false;
	auto &map = nodeWidgets();
	auto it = map.find(nodeId);
	Widget *widget = it == map.end() ? nullptr : it->second;
	if (widget && widget->kind != kind) {
		surfaces().erase(widget);
		if (widget->document) surfaces().erase(widgetFor(widget->document));
		destroyWidget(widget);
		map.erase(it);
		widget = nullptr;
	}
	if (widget) {
		if (GetParent(widget->hwnd) != parent) {
			setWidgetParent(widget, parent);
			*created = true;  // z-order must be re-established
		}
		return widget;
	}
	widget = createWidget(kind, parent);
	if (!widget) return nullptr;
	configureEvents(widget, nodeId);
	map.emplace(nodeId, widget);
	*created = true;
	return widget;
}

void applyWindowStyle(Widget *widget, int nodeId, const Node &node, int deviceWidth, int deviceHeight, const Materialization &materialization)
{
	Tree &tree = Tree::instance();
	WidgetStyle style = widget->style;
	applyCommonStyle(style, node, deviceWidth, deviceHeight, materialization, nodeId);
	switch (widget->kind) {
	case WidgetKind::Button:
		style.text = toWide(buttonTitle(node));
		style.font = fontForId(node.style.font_id, node.style.font_size > 0 ? node.style.font_size : 13, node.style.font_weight);
		style.editable = !tree.hasAttribute(nodeId, "disabled");
		break;
	case WidgetKind::TextField:
	case WidgetKind::TextArea: {
		applyTextStyle(style, node);
		const char *value = attribute(nodeId, "value");
		style.text = toWide(value ? value : "");
		// The engine stores attribute values in a fixed buffer
		// (kNodeAttributeValueMax), so a long `value` comes back cut at the
		// cap. Pushing that back into the control would clip what the user is
		// typing, so when the model holds exactly the cap and it is a prefix
		// of the text the control already shows, the control keeps its text
		// (the same as the macOS target sees through NSTextView).
		if (value && std::strlen(value) == static_cast<std::size_t>(gea::embedded::ui::kNodeAttributeValueMax - 1) &&
		    widget->lastText.size() > style.text.size() && widget->lastText.compare(0, style.text.size(), style.text) == 0) {
			style.text = widget->lastText;
		}
		const char *placeholder = attribute(nodeId, "placeholder");
		style.placeholder = toWide(placeholder ? placeholder : "");
		style.editable = !tree.hasAttribute(nodeId, "readonly") && !tree.hasAttribute(nodeId, "disabled");
		style.bordered = false;
		break;
	}
	case WidgetKind::Slider: {
		const char *minimum = attribute(nodeId, "min");
		const char *maximum = attribute(nodeId, "max");
		const char *value = attribute(nodeId, "value");
		style.minimum = minimum && minimum[0] ? std::atof(minimum) : 0;
		style.maximum = maximum && maximum[0] ? std::atof(maximum) : 100;
		if (value && value[0]) style.value = std::atof(value);
		break;
	}
	case WidgetKind::ProgressBar: {
		const char *maximum = attribute(nodeId, "max");
		const char *value = attribute(nodeId, "value");
		style.minimum = 0;
		style.maximum = maximum && maximum[0] ? std::atof(maximum) : 100;
		style.indeterminate = !(value && value[0]);
		if (value && value[0]) style.value = std::atof(value);
		break;
	}
	case WidgetKind::CheckBox: {
		const char *checked = attribute(nodeId, "checked");
		const bool has = tree.hasAttribute(nodeId, "checked");
		const bool explicitlyOff = checked && (std::strcmp(checked, "false") == 0 || std::strcmp(checked, "0") == 0);
		style.checked = has && !explicitlyOff;
		style.text = toWide(buttonTitle(node));
		break;
	}
	case WidgetKind::Scroll:
		style.contentWidth = toDevice(std::max<int>(node.layout.scroll_content_width, node.layout.width));
		style.contentHeight = toDevice(node.layout.scroll_content_height > 0 ? node.layout.scroll_content_height : node.layout.height);
		style.verticalScroller = node.style.overflow_y != 1;
		style.horizontalScroller = node.style.overflow_x == 2 && node.layout.scroll_content_width > node.layout.width;
		break;
	default:
		break;
	}
	applyWidgetStyle(widget, style);
}

void enforceChildOrder(HWND parent, const std::vector<HWND> &orderedChildren)
{
	// Later siblings paint over earlier ones: walk from the last child down,
	// placing each at the top only when it is not already directly beneath
	// the previously placed one.
	HWND previous = nullptr;
	for (auto it = orderedChildren.rbegin(); it != orderedChildren.rend(); ++it) {
		HWND child = *it;
		if (previous == nullptr) {
			if (GetWindow(parent, GW_CHILD) != child) SetWindowPos(child, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
		} else if (GetWindow(previous, GW_HWNDNEXT) != child) {
			SetWindowPos(child, previous, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
		}
		previous = child;
	}
}

// --- sync ------------------------------------------------------------------------------

struct SyncScope {
	HWND surfaceHwnd = nullptr;
	int originLayoutX = 0;
	int originLayoutY = 0;
	std::vector<std::pair<int, HWND>> hosted;  // z-index, window: the surface's child windows in document order
	bool orderDirty = false;
	bool layered = false;
	bool hasCanvas = false;
	Hasher hasher;
};

bool syncWindowNode(int nodeId, const Node &node, const Materialization &materialization, HWND parent, int originLayoutX, int originLayoutY,
                    std::unordered_set<int> &unseen);

void syncPaintedNode(SyncScope &scope, int nodeId, std::unordered_set<int> &unseen)
{
	Tree &tree = Tree::instance();
	const Node &node = tree.node(nodeId);
	const Materialization materialization = materializationFor(nodeId, node);
	hashNode(scope.hasher, node, nodeId, materialization);
	if (ownsWindow(materialization)) {
		if (syncWindowNode(nodeId, node, materialization, scope.surfaceHwnd, scope.originLayoutX, scope.originLayoutY, unseen)) scope.orderDirty = true;
		auto it = nodeWidgets().find(nodeId);
		if (it != nodeWidgets().end()) {
			scope.hosted.emplace_back(node.style.z_index, it->second->hwnd);
			if (node.style.z_index != 0) scope.layered = true;
		}
		return;
	}
	if (node.style.display == gea::embedded::ui::kDisplayNone) return;
	if (node.type == NodeType::Canvas) scope.hasCanvas = true;
	if (node.type == NodeType::Button) {
		// A styled button paints its title from its descendants; they are
		// not laid out as separate boxes.
		const std::string title = buttonTitle(node);
		scope.hasher.bytes(title.data(), title.size());
		return;
	}
	for (int child = node.first_child; child >= 0; child = tree.node(child).next_sibling) syncPaintedNode(scope, child, unseen);
}

// Paints `owner`'s children into `surface` (whose window is `hwnd`),
// creating windows for the descendants that need them.
void syncSurface(Widget *surface, HWND hwnd, int ownerId, std::unordered_set<int> &unseen)
{
	if (!surface || !hwnd) return;
	Tree &tree = Tree::instance();
	const Node &owner = tree.node(ownerId);
	SurfaceState &state = surfaces()[surface];
	if (state.owner != ownerId) {
		state = SurfaceState{};
		state.owner = ownerId;
		surface->events.onPaintOverlay = [surface](HDC hdc, const RECT &) { paintSurface(surface, hdc); };
		surface->events.onSurfacePointer = [surface](int phase, int x, int y) { surfacePointer(surface, phase, x, y); };
		surface->events.onKeyDown = [surface](int keyCode) { surfaceKey(surface, keyCode); };
		surface->focusable = true;
		InvalidateRect(hwnd, nullptr, FALSE);
	}
	SyncScope scope;
	scope.surfaceHwnd = hwnd;
	scope.originLayoutX = owner.layout.x;
	scope.originLayoutY = owner.layout.y;
	for (int child = owner.first_child; child >= 0; child = tree.node(child).next_sibling) syncPaintedNode(scope, child, unseen);
	if (scope.orderDirty || scope.layered) {
		std::stable_sort(scope.hosted.begin(), scope.hosted.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
		std::vector<HWND> windows;
		windows.reserve(scope.hosted.size());
		for (const auto &entry : scope.hosted) windows.push_back(entry.second);
		enforceChildOrder(hwnd, windows);
	}
	state.hasCanvas = scope.hasCanvas || owner.type == NodeType::Canvas;
	if (scope.hasher.hash != state.paintHash || state.hasCanvas) {
		state.paintHash = scope.hasher.hash;
		InvalidateRect(hwnd, nullptr, FALSE);
	}
}

// A node that owns a window: positions and styles it under `parent` (device
// coordinates relative to the owner origin) and syncs what it contains.
// Returns true when the window was created or reparented.
bool syncWindowNode(int nodeId, const Node &node, const Materialization &materialization, HWND parent, int originLayoutX, int originLayoutY,
                    std::unordered_set<int> &unseen)
{
	unseen.erase(nodeId);
	const WidgetKind kind = ownsWindow(materialization) ? materialization.kind : WidgetKind::View;
	bool created = false;
	Widget *widget = ensureWidget(nodeId, kind, parent, &created);
	if (!widget) return false;
	const int deviceX = toDevice(node.layout.x) - toDevice(originLayoutX);
	const int deviceY = toDevice(node.layout.y) - toDevice(originLayoutY);
	const int deviceWidth = toDevice(node.layout.x + node.layout.width) - toDevice(node.layout.x);
	const int deviceHeight = toDevice(node.layout.y + node.layout.height) - toDevice(node.layout.y);
	setWidgetFrame(widget, deviceX, deviceY, deviceWidth, deviceHeight);
	applyWindowStyle(widget, nodeId, node, deviceWidth, deviceHeight, materialization);
	if (widget->kind == WidgetKind::Scroll) {
		if (Widget *document = widgetFor(widget->document)) syncSurface(document, widget->document, nodeId, unseen);
	} else if (widget->kind == WidgetKind::View) {
		syncSurface(widget, widget->hwnd, nodeId, unseen);
	}
	return created;
}

void sweep(std::unordered_set<int> &unseen)
{
	auto &map = nodeWidgets();
	for (int gone : unseen) {
		auto it = map.find(gone);
		if (it == map.end()) continue;
		Widget *widget = it->second;
		surfaces().erase(widget);
		if (widget->document) surfaces().erase(widgetFor(widget->document));
		destroyWidget(widget);
		map.erase(it);
	}
}

}  // namespace

Renderer &Renderer::instance()
{
	static Renderer renderer;
	return renderer;
}

void Renderer::setScale(double scale)
{
	if (scale <= 0) scale = 1.0;
	g_scale = scale;
	setFontScale(scale);
}

double Renderer::scale() const
{
	return g_scale;
}

Widget *Renderer::widgetForNode(int nodeId) const
{
	auto &map = nodeWidgets();
	auto it = map.find(nodeId);
	return it == map.end() ? nullptr : it->second;
}

void Renderer::noteFrame() {}

void orientationLogicalSize(int *width, int *height)
{
	namespace od = gea::framework::display::detail;
	if (width) *width = od::DisplayOrientationState::width();
	if (height) *height = od::DisplayOrientationState::height();
}

void Renderer::sync(HWND parent, int rootNodeId)
{
	Tree &tree = Tree::instance();
	if (rootNodeId < 0 || rootNodeId >= tree.nodeCount()) return;
	std::unordered_set<int> unseen;
	for (const auto &entry : nodeWidgets()) unseen.insert(entry.first);
	const Node &root = tree.node(rootNodeId);
	syncWindowNode(rootNodeId, root, materializationFor(rootNodeId, root), parent, root.layout.x, root.layout.y, unseen);
	sweep(unseen);
}

void Renderer::syncPanes(const std::vector<HWND> &panes, const std::vector<int> &rootNodeIds)
{
	Tree &tree = Tree::instance();
	std::unordered_set<int> unseen;
	for (const auto &entry : nodeWidgets()) unseen.insert(entry.first);
	for (size_t i = 0; i < panes.size() && i < rootNodeIds.size(); i++) {
		const int rootNodeId = rootNodeIds[i];
		if (rootNodeId < 0 || rootNodeId >= tree.nodeCount()) continue;
		const Node &root = tree.node(rootNodeId);
		syncWindowNode(rootNodeId, root, materializationFor(rootNodeId, root), panes[i], root.layout.x, root.layout.y, unseen);
	}
	sweep(unseen);
}

void Renderer::teardown()
{
	for (auto &entry : nodeWidgets()) destroyWidget(entry.second);
	nodeWidgets().clear();
	surfaces().clear();
	dropImageBitmaps();
}

}  // namespace gea::win32
