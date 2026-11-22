// SPDX-License-Identifier: Apache-2.0
#pragma once

namespace gea::win32 {

bool preparePrerenderDisplay(int width, int height);
void setPrerenderTime(int timeMs);
void usePrerenderFonts();
int runPrerender();

}  // namespace gea::win32
