// SPDX-License-Identifier: Apache-2.0
// Fallback __gea_top_level when no app was compiled: exercises View / Text /
// Button / event dispatch so the target itself can be verified.
#include "events.h"
#include "ui/document.h"

#include <cstdio>

namespace {
gea::embedded::ui::TextElement g_counterLabel;
int g_counterValue = 0;

void refreshCounterLabel()
{
	char buffer[32];
	std::snprintf(buffer, sizeof(buffer), "Clicked %d", g_counterValue);
	g_counterLabel.setText(buffer);
}
}  // namespace

void __gea_top_level()
{
	using namespace gea::embedded::ui;
	using gea::framework::events::PointerEvent;

	auto &document = Document::instance();
	auto root = document.createView();
	root.style().backgroundColor(0x33DE);  // RGB565 ~ #3478F6
	root.style().set(Property::FlexDirection, 0);
	root.style().set(Property::JustifyContent, 1);
	root.style().set(Property::AlignItems, 1);
	root.style().set(Property::Gap, 16);

	auto title = document.createText("Hello, native Windows!");
	title.style().color(0xFFFF);
	title.style().set(Property::FontSize, 24);
	root.appendChild(title);

	g_counterLabel = document.createText("Clicked 0");
	g_counterLabel.style().color(0xFFFF);
	g_counterLabel.style().set(Property::FontSize, 16);
	root.appendChild(g_counterLabel);
	g_counterValue = 0;

	auto button = document.createButton();
	auto buttonLabel = document.createText("Click me");
	button.appendChild(buttonLabel);
	button.addEventListener("click", [](PointerEvent &) {
		g_counterValue++;
		refreshCounterLabel();
	});
	root.appendChild(button);

	document.mount(root, Document::preferredMountWidth(), Document::preferredMountHeight());
}

namespace gea::framework::app::generated {
void drainMicrotasks() {}
}
void gea_cpp_clear_microtasks() {}
