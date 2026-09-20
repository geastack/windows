// SPDX-License-Identifier: Apache-2.0
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Dispatches touchstart -> touchend -> click at a node, the way a mouse click
// on the web target does, so apps written for the touchscreen and JSX onClick
// handlers both fire.
void gea_win32_fire_press_for_node(int nodeId);
// Writes `text` into the first <input>/<textarea> (or `nodeId`) and dispatches
// an `input` event; the headless text-entry driver for GEA_WINDOWS_SYNTH_INPUT.
int gea_win32_fire_input_for_node(int nodeId, const char *text);
// Mouse state cache read by gea::platform::touch::Touchscreen (canvas apps).
void gea_win32_touch_set_state(int touching, int x, int y);

#ifdef __cplusplus
}
#endif
