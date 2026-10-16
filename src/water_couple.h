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
// the world into its particles' space and their gravity there; none when
// it meets none.
void mvfxResolveWaterCoupling(mvfxInstance* instance, int32_t emitter,
                              const mvfxSpaceMap* fromWorld, const float gravity[3]);

// A particle at a step's start against a surface: whether it is over the
// water (over the grid, on shallow water its center above the ground),
// its place in the surface's frame and the surface's height over it.
typedef struct mvfxWaterTouch
{
    bool over;
    float at[3];
    float height;
} mvfxWaterTouch;

// Adds a resolved surface's drag on a particle at x (in the particles'
// space) to its acceleration a and drag: drag times its share under (of
// its height, its center from its radius r above the surface, 0, to r
// below, 1) toward the water's velocity. Returns what it touches.
mvfxWaterTouch mvfxWaterDrag(const mvfxResolvedWater* water, const float x[3], float a[3],
                             float* drag);

// Adds a surface's lift to a particle's acceleration a (all the rest of
// it, gravity's included), its velocity v and its drag's factors over the
// step known (it moves by v carry + a push): buoyancy times gravity's
// strength times its share under where the step ends, solved with the
// move over the lift's three pieces (backward Euler), so a stiff one
// neither rings nor leaps past where it floats.
void mvfxWaterLift(const mvfxResolvedWater* water, const mvfxWaterTouch* touch, const float v[3],
                   float carry, float push, float a[3]);

// Meets particle i's move from start (as it touched the surface there) to
// end (in the particles' space) over seconds from tick with the emitter's
// surface: a first crossing into the water this step is kept for the
// surface's next step, kills the particle when it pours, and writes its
// event.
void mvfxMeetWater(mvfxInstance* instance, int32_t emitter, int32_t i, const mvfxWaterTouch* touch,
                   const float start[3], const float end[3], int64_t tick, double seconds);

#endif
