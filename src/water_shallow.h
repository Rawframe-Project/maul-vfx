// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Shallow water's step: its ground, its sources and pushes, then the
// virtual pipe model's substeps.

#ifndef MAUL_VFX_SRC_WATER_SHALLOW_H
#define MAUL_VFX_SRC_WATER_SHALLOW_H

#include "instance.h"
#include "tasks.h"

// Steps shallow water surface w of an instance over its last step: its
// substeps counted from the speed of the state the last context step
// left (mvfxWaterState.stepSpeed), its ground read,
// filled at its first step, its sources and pushes, then the substeps,
// each pass split over tasks; then whether it moves and how far its
// surface reaches.
void mvfxStepShallow(mvfxInstance* instance, int32_t w, const mvfxTasks* tasks);

// The fastest speed of a shallow water's state: its largest face speed
// plus sqrt(g deepest), a wave's on still water.
float mvfxShallowFastest(const mvfxCompiledWater* water, const mvfxWaterState* state);

// The substeps a shallow water's step of seconds takes from the fastest
// speed of the state the last context step left (on the GPU path, the
// one its simulation reported), and their length in *dt: equal, each at most
// half a cell at that speed, at least one, none for no time; past
// MVFX_MAX_WATER_SUBSTEPS that many of the longest length, the step
// slowed (*slowed); for an infinite speed (not known) that many, equal,
// not slowed.
int32_t mvfxShallowSubsteps(const mvfxCompiledWater* water, float fastest, double seconds,
                            float* dt, bool* slowed);

#endif
