// SPDX-License-Identifier: Apache-2.0
// The target's side of gea_win32_host.h: the extensions registered so far, in
// registration order.
#pragma once

#include "gea_win32_host.h"

#include <vector>

namespace gea::win32 {

std::vector<const gea_win32_host_extension *> &hostExtensions();
// GEA_WIN32_FRAME_PACING_TIMER or GEA_WIN32_FRAME_PACING_PRESENT.
int framePacing();

}  // namespace gea::win32
