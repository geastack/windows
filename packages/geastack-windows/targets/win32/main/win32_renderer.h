// SPDX-License-Identifier: Apache-2.0
// The gea node tree -> Win32 renderer.
//
// One painted surface per window (and per scroll container), with real child
// windows only for what needs them: native controls and scroll containers.
// Each frame the renderer walks the laid-out tree, positions those windows,
// and repaints a surface when the paint signature of its subtree changed.
// Presses are hit-tested against the painted tree. Node geometry is layout
// px; the renderer scales to device px.
#pragma once

#include "win32_widgets.h"

#include <vector>

namespace gea::win32 {

class Renderer {
public:
	static Renderer &instance();

	// Layout px -> device px factor (the window's DPI / 96).
	void setScale(double scale);
	double scale() const;

	// Syncs the subtree rooted at `rootNodeId` into `parent`, whose client
	// origin corresponds to the root node's layout origin.
	void sync(HWND parent, int rootNodeId);

	// Multi-root variant for the split shell: pane i hosts the subtree rooted
	// at rootNodeIds[i]. One combined sweep across all panes so a pane's sync
	// never evicts another pane's widgets.
	void syncPanes(const std::vector<HWND> &panes, const std::vector<int> &rootNodeIds);

	// Drops every widget (app teardown / resident switch).
	void teardown();

	// The widget currently rendering `nodeId`, or nullptr.
	Widget *widgetForNode(int nodeId) const;

	// Every frame, before sync: cheap change detection input from the tree.
	void noteFrame();

private:
	Renderer() = default;
};

}  // namespace gea::win32
