// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// What shallow water stands on and what is put into it: its ground read
// from a heightfield, its first fill, its sources and the pushes a step
// takes.

#ifndef MAUL_VFX_SRC_WATER_GROUND_H
#define MAUL_VFX_SRC_WATER_GROUND_H

#include "collide.h"
#include "instance.h"
#include "tasks.h"

// Resolves shallow water surface w's terrain into its frame where it
// stands now, and its grid: the def's, or the instance's first
// heightfield collider when the def takes the host's; false for the
// floor.
bool mvfxResolveWaterTerrain(const mvfxInstance* instance, int32_t w,
                             mvfxResolvedCollider* terrainOut, mvfxGridId* gridOut);

// Reads shallow water surface w's ground at its cells' centers, in its
// frame: its terrain (the def's, or the instance's first heightfield
// collider when the def takes the host's) where the vertical through the
// center meets it, else the floor (0); rows split over tasks.
void mvfxReadWaterGround(mvfxInstance* instance, int32_t w, const mvfxTasks* tasks);

// Fills each cell to the def's level where its ground is below it.
void mvfxFillWater(mvfxInstance* instance, int32_t w);

// Adds the sources' depth over seconds to the cells their footprints
// cover.
void mvfxPourWaterSources(mvfxInstance* instance, int32_t w, double seconds);

// Takes the pushes given for the step, then the particles' crossings:
// each push moves water between the cells within its radius and the ring
// out to twice it, the volume kept; a crossing pushes so and adds what it
// pours to the four cells about it. Then holds every depth within
// MVFX_MAX_WATER_DEPTH.
void mvfxPushShallow(mvfxInstance* instance, int32_t w);

#endif
