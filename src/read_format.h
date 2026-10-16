// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The formats a field is read out in (a gas's, a water surface's).

#ifndef MAUL_VFX_SRC_READ_FORMAT_H
#define MAUL_VFX_SRC_READ_FORMAT_H

#include "maul-vfx/output.h"

#include <stdbool.h>
#include <stdint.h>

// A format's bytes, 0 for one a field is not read in.
uint32_t mvfxReadFormatBytes(mvfxFormat format);

// Whether a format is normalized (its range mapped from low to high).
bool mvfxIsReadNormalized(mvfxFormat format);

// A value mapped to what its format writes: a normalized format's range
// from low to high to 0 to 1, or -1 to 1.
float mvfxReadMapped(mvfxFormat format, float low, float high, float value);

#endif
