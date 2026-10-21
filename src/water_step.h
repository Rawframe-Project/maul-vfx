// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A water surface's step: its pushes, then substeps of its model (waves
// here, shallow water in water_shallow.c).

#ifndef MAUL_VFX_SRC_WATER_STEP_H
#define MAUL_VFX_SRC_WATER_STEP_H

#include "instance.h"
#include "tasks.h"

// Steps water surface w of an instance over its last step: the pushes
// given for it, then the substeps the step's length takes, each pass
// split over tasks; then whether it moves and how far it reaches.
void mvfxStepWater(mvfxInstance* instance, int32_t w, const mvfxTasks* tasks);

// Lowers the cells within radius of a point (in the surface's frame, X
// and Z) of heights u by depth times (1 - r^2)^2; returns the sum of what
// they sank, in binary64 cell by cell. With u NULL it only sums.
double mvfxSinkWaves(const mvfxCompiledWater* water, float* u, const float at[2], float radius,
                     float depth);

// The substeps a waves surface's step of seconds takes and their length
// in *dt: equal, each at most 0.9 h / (c sqrt 2), none for no time; past
// MVFX_MAX_WATER_SUBSTEPS that many of the longest, the step slowed.
int32_t mvfxWaveSubsteps(const mvfxCompiledWater* water, double seconds, float* dt, bool* slowed);

#endif
