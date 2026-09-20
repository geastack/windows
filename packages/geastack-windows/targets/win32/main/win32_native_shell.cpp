// SPDX-License-Identifier: Apache-2.0
#include "win32_native_shell.h"

#include "events.h"
#include "ui/node.h"
#include "ui/node_model.h"
#include "ui/tree_internal.h"
#include "win32_font_registry.h"
#include "win32_renderer.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace gea::win32 {

namespace {

using gea::embedded::ui::Tree;

int toDevice(int layoutPx, double scale)
{
	return static_cast<int>(std::lround(layoutPx * scale));
}

const char *nodeTag(int nodeId)
{
	Tree &tree = Tree::instance();
	if (nodeId < 0 || nodeId >= tree.nodeCount()) return "";
	const char *tag = tree.tagName(nodeId);
	return tag ? tag : "";
}

bool tagIs(int nodeId, const char *tag)
{
	return std::strcmp(nodeTag(nodeId), tag) == 0;
}

int firstChild(int nodeId)
{
	Tree &tree = Tree::instance();
	if (nodeId < 0 || nodeId >= tree.nodeCount()) return -1;
	return tree.node(nodeId).first_child;
}

int nextSibling(int nodeId)
{
	Tree &tree = Tree::instance();
	if (nodeId < 0 || nodeId >= tree.nodeCount()) return -1;
	return tree.node(nodeId).next_sibling;
}

std::string attr(int nodeId, const char *name)
{
	Tree &tree = Tree::instance();
	if (nodeId < 0 || nodeId >= tree.nodeCount()) return "";
	const char *value = tree.getAttribute(nodeId, name);
	return value ? std::string(value) : std::string();
}

// Fires a click at the node whose `id` attribute is `elementId`: toolbar
// items drive JSX onClick handlers of a hidden trigger element.
void firePressForElementId(const std::string &elementId)
{
	if (elementId.empty()) return;
	Tree &tree = Tree::instance();
	for (int i = 0; i < tree.nodeCount(); i++) {
		if (attr(i, "id") != elementId) continue;
		gea::framework::events::PointerEvent event;
		event.type = gea::framework::events::PointerEventType::Click;
		event.targetId = i;
		tree.dispatchEvent(event);
		return;
	}
}

int findGlassSplit(int nodeId, int depth = 0)
{
	if (nodeId < 0 || depth > 4) return -1;
	if (tagIs(nodeId, "glass-split")) return nodeId;
	for (int child = firstChild(nodeId); child >= 0; child = nextSibling(child)) {
		const int found = findGlassSplit(child, depth + 1);
		if (found >= 0) return found;
	}
	return -1;
}

Color paneBackground(int role)
{
	const bool dark = darkModeEnabled();
	switch (role) {
	case 0: return dark ? Color::rgb(40, 40, 42) : Color::rgb(236, 236, 238);   // sidebar material
	case 1: return dark ? Color::rgb(33, 33, 35) : Color::rgb(246, 246, 247);   // content list
	default: return dark ? Color::rgb(29, 29, 31) : Color::rgb(255, 255, 255);  // detail
	}
}

}  // namespace

// --- Toolbar -------------------------------------------------------------------

Toolbar::Toolbar(HWND parent)
{
	strip_ = createWidget(WidgetKind::View, parent);
	background_ = windowBackground();
	WidgetStyle style = strip_->style;
	style.background = background_;
	applyWidgetStyle(strip_, style);
}

Toolbar::~Toolbar()
{
	for (Item &item : items_) {
		if (item.widget) destroyWidget(item.widget);
	}
	if (strip_) destroyWidget(strip_);
}

void Toolbar::setBackground(Color color)
{
	background_ = color;
	if (!strip_) return;
	WidgetStyle style = strip_->style;
	style.background = color;
	applyWidgetStyle(strip_, style);
}

void Toolbar::addItem(const std::string &symbol, const std::wstring &label, std::function<void()> action)
{
	Item item;
	item.label = label;
	item.action = std::move(action);
	item.widget = createWidget(WidgetKind::Label, strip_->hwnd);
	item.widget->clickTransparent = false;
	WidgetStyle style = item.widget->style;
	style.text = symbolGlyph(symbol);
	style.textAlign = 1;
	style.maxLines = 1;
	style.symbol = true;
	style.pressHighlight = true;
	style.textColor = darkModeEnabled() ? Color::rgb(235, 235, 240) : Color::rgb(40, 40, 44);
	applyWidgetStyle(item.widget, style);
	Widget *widget = item.widget;
	item.widget->events.onClick = [this, index = items_.size(), widget] {
		// Blur any edit first so a bound value can refresh (New Note while
		// the body still has focus).
		SetFocus(widget->hwnd);
		if (index < items_.size() && items_[index].action) items_[index].action();
	};
	items_.push_back(std::move(item));
}

void Toolbar::addSidebarToggle(std::function<void()> action)
{
	addItem("sidebar.left", L"Toggle Sidebar", std::move(action));
}

void Toolbar::addSpace()
{
	Item item;
	item.space = true;
	items_.push_back(std::move(item));
}

void Toolbar::addSearchField(const std::wstring &placeholder, std::function<void(const std::wstring &)> onChange)
{
	Item item;
	item.search = true;
	item.onChange = std::move(onChange);
	item.widget = createWidget(WidgetKind::TextField, strip_->hwnd);
	WidgetStyle style = item.widget->style;
	style.placeholder = placeholder.empty() ? L"Search" : placeholder;
	style.bordered = true;
	applyWidgetStyle(item.widget, style);
	item.widget->events.onTextChanged = [this, index = items_.size()](const std::wstring &text) {
		if (index < items_.size() && items_[index].onChange) items_[index].onChange(text);
	};
	items_.push_back(std::move(item));
}

std::wstring Toolbar::searchText() const
{
	for (const Item &item : items_) {
		if (item.search && item.widget) return widgetText(item.widget);
	}
	return std::wstring();
}

void Toolbar::layout(const RECT &bounds, double scale)
{
	if (!strip_) return;
	setWidgetFrame(strip_, bounds.left, bounds.top, bounds.right - bounds.left, bounds.bottom - bounds.top);
	const int height = static_cast<int>(bounds.bottom - bounds.top);
	const int itemWidth = toDevice(36, scale);
	const int itemHeight = toDevice(28, scale);
	const int searchWidth = toDevice(200, scale);
	const int searchHeight = toDevice(26, scale);
	const int gap = toDevice(6, scale);
	const int margin = toDevice(12, scale);
	int fixed = margin * 2;
	int spaces = 0;
	for (const Item &item : items_) {
		if (item.space) spaces++;
		else if (item.search) fixed += searchWidth + gap;
		else fixed += itemWidth + gap;
	}
	const int spaceWidth = spaces > 0 ? std::max(0, static_cast<int>(bounds.right - bounds.left) - fixed) / spaces : 0;
	int x = margin;
	for (Item &item : items_) {
		if (item.space) {
			x += spaceWidth;
			continue;
		}
		if (item.search) {
			setWidgetFrame(item.widget, x, (height - searchHeight) / 2, searchWidth, searchHeight);
			WidgetStyle style = item.widget->style;
			style.font = fontForFamily(L"Segoe UI", 13);
			style.textColor = darkModeEnabled() ? Color::rgb(235, 235, 240) : Color::rgb(30, 30, 30);
			style.background = darkModeEnabled() ? Color::rgb(50, 50, 54) : Color::rgb(255, 255, 255);
			applyWidgetStyle(item.widget, style);
			x += searchWidth + gap;
			continue;
		}
		setWidgetFrame(item.widget, x, (height - itemHeight) / 2, itemWidth, itemHeight);
		WidgetStyle style = item.widget->style;
		style.font = symbolFont(toDevice(16, scale));
		style.radius[0] = style.radius[1] = style.radius[2] = style.radius[3] = static_cast<float>(toDevice(5, scale));
		style.background = darkModeEnabled() ? Color::rgb(255, 255, 255, 18) : Color::rgb(0, 0, 0, 12);
		applyWidgetStyle(item.widget, style);
		x += itemWidth + gap;
	}
}

// --- SplitView -------------------------------------------------------------------

SplitView::SplitView(HWND parent)
{
	container_ = createWidget(WidgetKind::View, parent);
	dividerColor_ = darkModeEnabled() ? Color::rgb(58, 58, 62) : Color::rgb(214, 214, 218);
}

SplitView::~SplitView()
{
	for (SplitPane &pane : panes_) {
		if (pane.divider) destroyWidget(pane.divider);
	}
	if (container_) destroyWidget(container_);
}

void SplitView::addPane(HWND view, int role, int minThickness, int maxThickness, bool canCollapse)
{
	SplitPane pane;
	pane.view = view;
	pane.role = role;
	pane.minThickness = std::max(0, minThickness);
	pane.maxThickness = maxThickness > 0 ? maxThickness : 100000;
	pane.canCollapse = canCollapse;
	// Initial widths follow the AppKit defaults the macOS shell inherits:
	// the sidebar opens near its minimum, the list a little wider.
	pane.thickness = role == 0 ? std::max(pane.minThickness, 220) : role == 1 ? std::max(pane.minThickness, 300) : pane.minThickness;
	pane.thickness = std::min(pane.thickness, pane.maxThickness);
	if (!panes_.empty()) {
		SplitPane &previous = panes_.back();
		previous.divider = createWidget(WidgetKind::Divider, container_->hwnd);
		WidgetStyle style = previous.divider->style;
		style.border = dividerColor_;
		applyWidgetStyle(previous.divider, style);
		const size_t index = panes_.size() - 1;
		previous.divider->events.onPressDown = nullptr;
		previous.divider->events.onDividerDrag = [this, index](int deltaDevice) {
			SplitPane &dragged = panes_[index];
			if (dragged.dragStart == 0) dragged.dragStart = dragged.thickness;
			const int delta = static_cast<int>(std::lround(deltaDevice / lastScale_));
			dragged.thickness = std::clamp(dragged.dragStart + delta, dragged.minThickness, dragged.maxThickness);
			layout(lastBounds_, lastScale_);
			if (onLayoutChanged) onLayoutChanged();
		};
	}
	if (view) SetParent(view, container_->hwnd);
	panes_.push_back(pane);
}

void SplitView::toggleSidebar()
{
	for (SplitPane &pane : panes_) {
		if (pane.role != 0 && !pane.canCollapse) continue;
		pane.collapsed = !pane.collapsed;
		layout(lastBounds_, lastScale_);
		if (onLayoutChanged) onLayoutChanged();
		return;
	}
}

void SplitView::setPaneThickness(size_t index, int layoutPx)
{
	if (index >= panes_.size()) return;
	panes_[index].thickness = std::clamp(layoutPx, panes_[index].minThickness, panes_[index].maxThickness);
	layout(lastBounds_, lastScale_);
}

void SplitView::setDividerColor(Color color)
{
	dividerColor_ = color;
	for (SplitPane &pane : panes_) {
		if (!pane.divider) continue;
		WidgetStyle style = pane.divider->style;
		style.border = color;
		applyWidgetStyle(pane.divider, style);
	}
}

void SplitView::layout(const RECT &bounds, double scale)
{
	if (!container_) return;
	lastBounds_ = bounds;
	lastScale_ = scale;
	const int width = static_cast<int>(bounds.right - bounds.left);
	const int height = static_cast<int>(bounds.bottom - bounds.top);
	setWidgetFrame(container_, bounds.left, bounds.top, width, height);
	if (panes_.empty()) return;
	const int dividerHit = toDevice(std::max(dividerWidth_, 7), scale);
	// Every pane but the last has a fixed thickness; the last takes the rest.
	int x = 0;
	for (size_t i = 0; i < panes_.size(); i++) {
		SplitPane &pane = panes_[i];
		const bool last = i + 1 == panes_.size();
		int paneWidth = 0;
		if (pane.collapsed) {
			paneWidth = 0;
		} else if (last) {
			paneWidth = std::max(0, width - x);
		} else {
			paneWidth = toDevice(pane.thickness, scale);
			// Keep the remaining panes at least their minimum.
			int remainingMin = 0;
			for (size_t j = i + 1; j < panes_.size(); j++) {
				if (!panes_[j].collapsed) remainingMin += toDevice(panes_[j].minThickness, scale) + dividerHit;
			}
			paneWidth = std::max(0, std::min(paneWidth, width - x - remainingMin));
		}
		if (pane.view) {
			ShowWindow(pane.view, pane.collapsed ? SW_HIDE : SW_SHOWNA);
			SetWindowPos(pane.view, nullptr, x, 0, paneWidth, height, SWP_NOZORDER | SWP_NOACTIVATE);
		}
		x += paneWidth;
		if (pane.divider) {
			const bool showDivider = !pane.collapsed && !last;
			ShowWindow(pane.divider->hwnd, showDivider ? SW_SHOWNA : SW_HIDE);
			if (showDivider) {
				setWidgetFrame(pane.divider, x - dividerHit / 2, 0, dividerHit, height);
				SetWindowPos(pane.divider->hwnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
			}
		}
		pane.dragStart = 0;
	}
}

// --- GlassSplitShell -------------------------------------------------------------

std::unique_ptr<GlassSplitShell> GlassSplitShell::create(int mountedRoot, HWND window)
{
	const int splitNode = findGlassSplit(mountedRoot);
	if (splitNode < 0) return nullptr;
	std::unique_ptr<GlassSplitShell> shell(new GlassSplitShell());
	shell->split_ = std::make_unique<SplitView>(window);
	struct ToolbarItemInfo {
		std::string symbol;
		std::string label;
		std::string actionId;
		bool search = false;
		bool space = false;
		bool sidebarToggle = false;
	};
	std::vector<ToolbarItemInfo> toolbarItems;
	for (int child = firstChild(splitNode); child >= 0; child = nextSibling(child)) {
		if (tagIs(child, "toolbar")) {
			for (int item = firstChild(child); item >= 0; item = nextSibling(item)) {
				ToolbarItemInfo info;
				if (tagIs(item, "toolbar-search")) info.search = true;
				else if (tagIs(item, "toolbar-space")) info.space = true;
				else if (tagIs(item, "toolbar-item")) {
					info.symbol = attr(item, "data-symbol");
					info.label = attr(item, "data-label");
					info.actionId = attr(item, "data-action");
					info.sidebarToggle = attr(item, "data-role") == "sidebar-toggle";
				} else {
					continue;
				}
				toolbarItems.push_back(info);
			}
			continue;
		}
		if (!tagIs(child, "glass-pane")) continue;
		const std::string role = attr(child, "data-pane");
		const int contentNode = firstChild(child);
		if (contentNode < 0) continue;
		const int roleIndex = role == "sidebar" ? 0 : role == "list" ? 1 : 2;
		Widget *container = createWidget(WidgetKind::View, shell->split_->hwnd());
		WidgetStyle style = container->style;
		style.background = paneBackground(roleIndex);
		applyWidgetStyle(container, style);
		if (roleIndex == 0) shell->split_->addPane(container->hwnd, 0, 180, 320, true);
		else if (roleIndex == 1) shell->split_->addPane(container->hwnd, 1, 240, 460, false);
		else shell->split_->addPane(container->hwnd, 2, 320, 100000, false);
		shell->panes_.push_back(PaneInfo{contentNode, container, RECT{}});
	}
	if (shell->panes_.empty()) return nullptr;
	GlassSplitShell *raw = shell.get();
	shell->split_->onLayoutChanged = [raw] { raw->dirty_ = true; };
	if (!toolbarItems.empty()) {
		shell->toolbar_ = std::make_unique<Toolbar>(window);
		for (const ToolbarItemInfo &info : toolbarItems) {
			if (info.space) shell->toolbar_->addSpace();
			else if (info.search) shell->toolbar_->addSearchField(L"Search", nullptr);
			else if (info.sidebarToggle) shell->toolbar_->addSidebarToggle([raw] { raw->split_->toggleSidebar(); });
			else {
				const std::string action = info.actionId;
				shell->toolbar_->addItem(info.symbol, toWide(info.label), [action] { firePressForElementId(action); });
			}
		}
	}
	return shell;
}

GlassSplitShell::~GlassSplitShell()
{
	for (PaneInfo &pane : panes_) {
		if (pane.container) destroyWidget(pane.container);
	}
}

void GlassSplitShell::layoutAndSync(const RECT &clientArea, double scale)
{
	if (!split_) return;
	Tree &tree = Tree::instance();
	RECT area = clientArea;
	if (toolbar_) {
		RECT strip = area;
		strip.bottom = strip.top + toDevice(toolbar_->layoutHeight(), scale);
		toolbar_->layout(strip, scale);
		area.top = strip.bottom;
	}
	split_->layout(area, scale);

	std::vector<RECT> frames(panes_.size());
	bool anyFrameChanged = false;
	for (size_t i = 0; i < panes_.size(); i++) {
		RECT frame{};
		GetClientRect(panes_[i].container->hwnd, &frame);
		frames[i] = frame;
		if (!EqualRect(&frame, &panes_[i].lastFrame)) anyFrameChanged = true;
	}
	// The expensive layout + full diff runs only when the reactive tree
	// changed or a pane resized; an idle window costs a few rect compares.
	const std::uint64_t serial = tree.refreshSerial();
	if (serial == lastSerial_ && !anyFrameChanged && !dirty_) return;
	dirty_ = false;
	std::vector<HWND> paneViews;
	std::vector<int> rootIds;
	for (size_t i = 0; i < panes_.size(); i++) {
		PaneInfo &pane = panes_[i];
		pane.lastFrame = frames[i];
		const int w = static_cast<int>(std::lround((frames[i].right - frames[i].left) / scale));
		const int h = static_cast<int>(std::lround((frames[i].bottom - frames[i].top) / scale));
		if (w <= 0 || h <= 0) continue;
		if (pane.contentNodeId < 0 || pane.contentNodeId >= tree.nodeCount()) continue;
		gea::embedded::ui::NodeHandle(pane.contentNodeId).style().width(w);
		gea::embedded::ui::NodeHandle(pane.contentNodeId).style().height(h);
		tree.computeLayout(pane.contentNodeId, w, h);
		paneViews.push_back(pane.container->hwnd);
		rootIds.push_back(pane.contentNodeId);
	}
	lastSerial_ = serial;
	if (paneViews.empty()) return;
	Renderer::instance().syncPanes(paneViews, rootIds);
}

}  // namespace gea::win32
