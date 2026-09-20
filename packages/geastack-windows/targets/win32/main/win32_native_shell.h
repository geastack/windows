// SPDX-License-Identifier: Apache-2.0
// Native shell pieces shared by the `<glass-split>` gea shell and the Controls
// bridge: a resizable split view with draggable dividers and a title-bar
// toolbar strip with glyph items and a search field.
#pragma once

#include "win32_widgets.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace gea::win32 {

class Toolbar {
public:
	explicit Toolbar(HWND parent);
	~Toolbar();

	void addItem(const std::string &symbol, const std::wstring &label, std::function<void()> action);
	void addSidebarToggle(std::function<void()> action);
	void addSpace();
	void addSearchField(const std::wstring &placeholder, std::function<void(const std::wstring &)> onChange);
	std::wstring searchText() const;

	// Height in layout px (default 52, the unified title-bar toolbar height).
	int layoutHeight() const { return layoutHeight_; }
	void setLayoutHeight(int layoutPx) { layoutHeight_ = layoutPx; }
	void setBackground(Color color);
	// Positions the strip and its items; `bounds` is device px in the parent.
	void layout(const RECT &bounds, double scale);
	HWND hwnd() const { return strip_ ? strip_->hwnd : nullptr; }

private:
	struct Item {
		bool space = false;
		bool search = false;
		Widget *widget = nullptr;
		std::function<void()> action;
		std::function<void(const std::wstring &)> onChange;
		std::wstring label;
	};
	Widget *strip_ = nullptr;
	std::vector<Item> items_;
	int layoutHeight_ = 52;
	Color background_;
};

struct SplitPane {
	HWND view = nullptr;
	int role = 2;              // 0 sidebar, 1 list, 2 detail
	int minThickness = 100;    // layout px
	int maxThickness = 100000;
	bool canCollapse = false;
	int thickness = 240;       // layout px
	bool collapsed = false;
	Widget *divider = nullptr; // divider after this pane (none after the last)
	int dragStart = 0;
};

class SplitView {
public:
	explicit SplitView(HWND parent);
	~SplitView();

	void addPane(HWND view, int role, int minThickness, int maxThickness, bool canCollapse);
	void toggleSidebar();
	void setPaneThickness(size_t index, int layoutPx);
	void setDividerColor(Color color);
	void setDividerWidth(int layoutPx) { dividerWidth_ = layoutPx; }
	size_t paneCount() const { return panes_.size(); }
	const SplitPane &pane(size_t index) const { return panes_[index]; }
	// Lays panes out horizontally inside `bounds` (device px in the parent).
	void layout(const RECT &bounds, double scale);
	HWND hwnd() const { return container_ ? container_->hwnd : nullptr; }
	// Fired after a divider drag or collapse changed the layout.
	std::function<void()> onLayoutChanged;

private:
	Widget *container_ = nullptr;
	std::vector<SplitPane> panes_;
	Color dividerColor_;
	int dividerWidth_ = 1;
	RECT lastBounds_{};
	double lastScale_ = 1.0;
};

// The `<glass-split>` shell: when an app's mounted root holds a
// <glass-split>, its <glass-pane> children become split panes rendered by the
// gea renderer into resizable containers and its <toolbar> becomes a real
// toolbar strip. Mirrors the macOS GeaSplitShell.
class GlassSplitShell {
public:
	// nullptr when `mountedRoot` holds no <glass-split>.
	static std::unique_ptr<GlassSplitShell> create(int mountedRoot, HWND window);
	~GlassSplitShell();

	// Per frame: size each pane node to its container, run the flex layout
	// for that pane, and sync the gea subtrees into the pane views.
	void layoutAndSync(const RECT &clientArea, double scale);

private:
	GlassSplitShell() = default;
	struct PaneInfo {
		int contentNodeId = -1;
		Widget *container = nullptr;
		RECT lastFrame{};
	};
	std::unique_ptr<SplitView> split_;
	std::unique_ptr<Toolbar> toolbar_;
	std::vector<PaneInfo> panes_;
	std::uint64_t lastSerial_ = 0;
	bool dirty_ = true;
};

}  // namespace gea::win32
