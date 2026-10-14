// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Bounds: each emitter's box of its live particles, measured at its
// step's end, or the fixed box its def gives, in the world.

#ifndef MAUL_VFX_SRC_BOUNDS_H
#define MAUL_VFX_SRC_BOUNDS_H

#include "instance.h"

#include "maul-vfx/output.h"

// Measures an emitter's box of its live particles in their space, grown
// by their largest size component.
void mvfxMeasureBounds(mvfxInstance* instance, int32_t emitter);

// An emitter's bounds in the world: its fixed box through the instance's
// transform, or its measured box through the map to the world; empty
// without either.
mvfxBounds mvfxBoundsOf(const mvfxInstance* instance, int32_t emitter);

#endif
