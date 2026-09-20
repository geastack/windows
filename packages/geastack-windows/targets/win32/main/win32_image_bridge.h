// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "win32_widgets.h"

namespace gea::win32 {

// A 32bpp premultiplied BGRA bitmap for the framework's ImageStore slot, cached
// per image id and rebuilt when the slot's pixels change.
HBITMAP bitmapForImageId(int imageId, int *outWidth, int *outHeight);
void dropImageBitmaps();

}  // namespace gea::win32
