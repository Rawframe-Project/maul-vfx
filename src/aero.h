// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Sheets' aerodynamics: each whole triangle's drag along its normal and
// across it against the air.

#ifndef MAUL_VFX_SRC_AERO_H
#define MAUL_VFX_SRC_AERO_H

#include "instance.h"

#include <stdbool.h>

// A triangle's edges' cross product into n, both edges scaled by 2^-64
// when a component of either reaches 2^63 (and true then), so no product
// overflows: its length is twice the triangle's area, times 2^128 when
// scaled. The GPU part's crossEdges (rhi/kernels/shared/air.wgsl) gives
// the same bits.
bool mvfxCrossEdges(const float e1[3], const float e2[3], float n[3]);

// The share of a relative speed a triangle takes over a substep of h
// seconds at a coefficient k: k A speed h over its three points, at most a
// sixth, A half the triangle's doubled area (times 2^128 when scaled). None
// for a zero coefficient, area or speed. Decided by its factors' exponents where
// the product would pass binary32's largest, so it forms no infinity: the
// GPU part's airShare (rhi/kernels/shared/air.wgsl) gives the same bits.
float mvfxAirShare(float k, float half, bool scaled, float speed, float h);

// Changes an emitter of sheets' velocities by their triangles' drag over
// a substep of h seconds from a tick, each change found from the
// velocities at its start; room holds three arrays of the capacity.
void mvfxBlowSheets(mvfxInstance* instance, int32_t emitter, int64_t tick, float h, float* room);

#endif
