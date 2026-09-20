// SPDX-License-Identifier: Apache-2.0
#include "win32_press_bridge.h"

#include "events.h"
#include "ui/tree_internal.h"

#include <cstring>

extern "C" void gea_win32_fire_press_for_node(int nodeId)
{
	using gea::framework::events::PointerEvent;
	using gea::framework::events::PointerEventType;
	if (nodeId < 0) return;
	auto &tree = gea::embedded::ui::Tree::instance();
	if (nodeId >= tree.nodeCount()) return;
	auto fire = [&](PointerEventType type) {
		PointerEvent event;
		event.type = type;
		event.targetId = nodeId;
		tree.dispatchEvent(event);
	};
	fire(PointerEventType::TouchStart);
	fire(PointerEventType::TouchEnd);
	fire(PointerEventType::Click);
}

extern "C" int gea_win32_fire_input_for_node(int nodeId, const char *text)
{
	using gea::framework::events::PointerEvent;
	using gea::framework::events::PointerEventType;
	auto &tree = gea::embedded::ui::Tree::instance();
	int target = nodeId;
	if (target < 0) {
		for (int i = 0; i < tree.nodeCount(); i++) {
			const char *tag = tree.tagName(i);
			if (!tag) continue;
			if (std::strcmp(tag, "input") == 0 || std::strcmp(tag, "textarea") == 0) {
				target = i;
				break;
			}
		}
	}
	if (target < 0 || target >= tree.nodeCount()) return -1;
	tree.setAttribute(target, "value", text ? text : "");
	PointerEvent event;
	event.type = PointerEventType::Input;
	event.targetId = target;
	event.keyCode = 0;
	tree.dispatchEvent(event);
	return target;
}
