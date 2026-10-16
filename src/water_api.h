// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Water surfaces as the host meets them: their grids in the world.

#ifndef MAUL_VFX_SRC_WATER_API_H
#define MAUL_VFX_SRC_WATER_API_H

#include "box.h"
#include "instance.h"

// Where water surface w's grid is now (its center at the rest level).
mvfxBoxPlace mvfxWaterPlace(const mvfxInstance* instance, int32_t w);

// The box in the world around water surface w now: its grid, and along
// its frame's Y its largest displacement.
mvfxBounds mvfxWaterBounds(const mvfxInstance* instance, int32_t w);

#endif
