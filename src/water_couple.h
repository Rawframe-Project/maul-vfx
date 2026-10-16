// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Particles meeting a water surface (mvfxEmitterDef.water): its pull on
// those in the water, and their crossings into it, which its next step
// takes.

#ifndef MAUL_VFX_SRC_WATER_COUPLE_H
#define MAUL_VFX_SRC_WATER_COUPLE_H

#include "force.h"
#include "instance.h"

// Resolves an emitter's water surface for the step, given the map from
// the world into its particles' space; none when it meets none.
void mvfxResolveWaterCoupling(mvfxInstance* instance, int32_t emitter,
                              const mvfxSpaceMap* fromWorld);

// Adds a resolved surface's pull on a particle at x (in the particles'
// space) to its acceleration a and drag: in the water, buoyancy times its
// depth below the surface up the surface's Y, and drag toward the water's
// velocity.
void mvfxWaterPull(const mvfxResolvedWater* water, const float x[3], float a[3], float* drag);

// Meets particle i's move from start to end (in the particles' space) over
// seconds from tick with the emitter's surface: a first crossing into the
// water this step is kept for the surface's next step, kills the particle
// when it pours, and writes its event.
void mvfxMeetWater(mvfxInstance* instance, int32_t emitter, int32_t i, const float start[3],
                   const float end[3], int64_t tick, double seconds);

#endif
