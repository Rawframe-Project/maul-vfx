// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A water surface as the library keeps it: its def checked and copied,
// and its fields' places in an instance's block.

#ifndef MAUL_VFX_SRC_WATER_H
#define MAUL_VFX_SRC_WATER_H

#include "allocator.h"
#include "force.h"

#include "maul-vfx/effect.h"
#include "maul-vfx/plan.h"
#include "maul-vfx/water.h"

#include <stdbool.h>
#include <stddef.h>

struct mvfxGrid;

// A water def as compiled: its cells, its sources copied, its terrain
// compiled and its grid (the effect's, held).
typedef struct mvfxCompiledWater
{
    mvfxWaterDef def;
    int64_t cells;
    mvfxWaterSource sources[MVFX_MAX_WATER_SOURCES];
    mvfxCollider terrain;
    struct mvfxGrid* terrainGrid;
} mvfxCompiledWater;

// Where a water surface's buffers lie in an instance's block.
typedef struct mvfxWaterAt
{
    size_t heights;
    size_t rates;
    size_t pushes;
    size_t faces;
    size_t giving;
    size_t ground;
    size_t heldRows;
    size_t crossings;
} mvfxWaterAt;

// A particle's crossing into a water surface for its next step, in the
// surface's frame: where (X and Z), its push's radius and depth, and the
// volume it pours (0 for none).
typedef struct mvfxWaterCrossing
{
    float at[2];
    float radius;
    float depth;
    float volume;
} mvfxWaterCrossing;

// A water surface's state in an instance: each cell's displacement from
// the rest level (shallow water's depth) in two buffers (current the one
// now; a substep gathers the neighbors' from it into the other) and its
// rate; shallow water's velocities on its faces, along X ((n0 + 1) by n1)
// and Z (n0 by (n1 + 1)) in two buffers each (faceCurrent the one now),
// each cell's outflows' scale (giving), its ground's height, each row's
// faces held in a substep, and whether it has been filled; the pushes and
// the particles' crossings (room for one a particle of the emitters that
// meet it, each crossing once a step) for the next step; where a world
// surface's grid was put (the instance's place at its start); whether it
// moves at all (a rate, or a slope), and its largest displacement's size
// (the surface's height); shallow water's fastest speed as the last
// context step left it, which each of this one's steps counts its
// substeps from; the counts mvfxWaterInfo reads. A planned instance's:
// the speed its simulation reported last and the tick it is of (-1 for
// none), and its plan's pushes and terrain as resolved.
typedef struct mvfxWaterState
{
    float* heights[2];
    int32_t current;
    float* rates;
    float* faces[2][2];
    int32_t faceCurrent;
    float* giving;
    float* ground;
    int32_t* heldRows;
    bool filled;
    mvfxWaterPush* pushes;
    int32_t pushCount;
    mvfxWaterCrossing* crossings;
    int64_t crossingCount;
    int64_t crossingCapacity;
    double origin[3];
    float rotation[4];
    float scale;
    bool holds;
    float reach;
    float stepSpeed;
    float reportedSpeed;
    int64_t reportedTick;
    // A planned surface's latest step counted (mvfxInstance_SetWaterCounts),
    // -1 for none.
    int64_t countedTick;
    int32_t planPushCount;
    mvfxPlannedWaterPush planPushes[MVFX_MAX_WATER_PUSHES];
    bool planHasTerrain;
    mvfxPlannedCollider planTerrain;
    uint64_t stepsSlowed;
    uint64_t facesHeld;
    double volumePoured;
} mvfxWaterState;

// Shallow water's faces along X and along Z for a grid of n cells.
static inline int64_t mvfxWaterFaces(const int32_t n[2], int32_t axis)
{
    return axis == 0 ? (int64_t)(n[0] + 1) * n[1] : (int64_t)n[0] * (n[1] + 1);
}

// A rate, or a difference of displacements, at most this is still.
#define MVFX_WATER_STILL 1e-6f

// Whether a water def is valid: mvfx_errorLimit for a value past one of
// the library's named bounds, mvfx_errorInvalid for anything else wrong.
mvfxResult mvfxCheckWaterDef(const mvfxWaterDef* def);

// Whether an emitter's coupling is valid against its effect's surfaces:
// off, or naming one, its values in range, a pour only on shallow water;
// mvfx_errorLimit for a value past its named bound.
mvfxResult mvfxCheckWaterCoupling(const mvfxWaterCoupling* coupling, const mvfxWaterDef* waters,
                                  int32_t count);

// An emitter's water surface for a step (mvfxEmitterDef.water): its
// coupling, the surface and its state as the last water step left them,
// the map from the surface's frame into the particles' space, and in that
// frame the particles' radius and their gravity's strength; the frame's
// Y in the particles' space; coupling NULL for none.
typedef struct mvfxResolvedWater
{
    const mvfxWaterCoupling* coupling;
    const mvfxCompiledWater* water;
    mvfxWaterState* state;
    mvfxSpaceMap map;
    float radius;
    float strength;
    float up[3];
} mvfxResolvedWater;

// A valid water def compiled.
mvfxCompiledWater mvfxCompileWater(const mvfxWaterDef* def);

// Places a water surface's buffers in a layout, with room for crossings.
void mvfxAddWaterLayout(mvfxLayout* layout, const mvfxCompiledWater* water, int64_t crossings,
                        mvfxWaterAt* at);

// Points a water surface's state at its buffers in block, still, with
// room for crossings.
void mvfxStartWater(mvfxWaterState* state, const mvfxCompiledWater* water, int64_t crossings,
                    unsigned char* block, const mvfxWaterAt* at);

// Makes a water surface still at its rest level, its pushes and crossings
// dropped.
void mvfxClearWater(mvfxWaterState* state, const mvfxCompiledWater* water);

// The cells along an axis of n whose centers lie within reach of center
// (both in cells, center's 0 at the first cell's center): span[0] to
// span[1], empty (span[1] below span[0]) when none, held within the grid
// however far the center.
void mvfxWaterSpan(float center, float reach, int32_t n, int32_t span[2]);

#endif
