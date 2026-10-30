// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A gas as the library keeps it: its def checked and copied, its fields'
// sizes and their places in an instance's block.

#ifndef MAUL_VFX_SRC_GAS_H
#define MAUL_VFX_SRC_GAS_H

#include "allocator.h"
#include "collide.h"
#include "force.h"
#include "gas_lattice.h"

#include "maul-vfx/gas.h"
#include "maul-vfx/plan.h"

#include <stdbool.h>
#include <stddef.h>

// The most levels of a gas's multigrid (maul-vfx/plan.h).
#define MVFX_GAS_LEVELS MVFX_MAX_GAS_LEVELS

// A gas's def as compiled: its sources copied in.
typedef struct mvfxCompiledGas
{
    mvfxGasDef def;
    mvfxGasSource sources[MVFX_MAX_GAS_SOURCES];
    // Its cells, and along each axis its faces across that axis.
    int64_t cells;
    int64_t faces[3];
    // Its multigrid's levels, the first its own cells: each level's cells
    // along each axis (an axis halved, rounding up, while every axis of
    // more than one cell keeps at least 2).
    int32_t levelCount;
    int32_t levels[MVFX_GAS_LEVELS][3];
    // The floats of its work buffer: the largest of its curl's (when it
    // confines vorticity), a faces array (with MacCormack) and its cells
    // (when burning expands it); 0 for none.
    int64_t work;
    // Its obstacles, compiled, and their grids (NULL for a primitive),
    // which the effect holds; whether it has any (its own or the host's).
    mvfxCollider obstacles[MVFX_MAX_COLLIDERS];
    struct mvfxGrid* obstacleGrids[MVFX_MAX_COLLIDERS];
    bool hasObstacles;
    // Its forces, compiled, and each vector field's grid (NULL for
    // others), which the effect holds.
    mvfxForce forces[MVFX_MAX_FORCES];
    struct mvfxGrid* forceGrids[MVFX_MAX_FORCES];
    // For a liquid gas the emitter whose particles move on it, for a
    // surface gas the one whose particles it wraps; -1 for smoke.
    int32_t emitter;
    // A surface gas's buckets: their cells along a side (enough to hold
    // its reach), their count along each axis and in all.
    int32_t bucket;
    int32_t buckets[3];
    int64_t bucketCount;
} mvfxCompiledGas;

// The most obstacles a gas meets: its own and the host's.
#define MVFX_GAS_OBSTACLES (2 * MVFX_MAX_COLLIDERS)

// Where a gas's buffers lie in an instance's block.
typedef struct mvfxGasAt
{
    size_t velocity[3];
    size_t scalars[3];
    size_t levels[MVFX_GAS_LEVELS];
    size_t areas[MVFX_GAS_LEVELS];
    size_t coefficients[MVFX_GAS_LEVELS];
    size_t weights[MVFX_GAS_LEVELS];
    size_t solid;
    size_t work;
    size_t cellStart;
    size_t order;
    size_t local;
    size_t masks;
    size_t valid;
} mvfxGasAt;

// A liquid or surface gas's room for its particles: each cell's (a
// surface's bucket's) first in the order (and one past the last's last);
// the particles' indices by cell, those outside the box left out; each
// particle's place in cells, and a liquid's velocity and affine matrix,
// in the box's frame (fifteen arrays of the capacity, a surface's three);
// a liquid's levels' liquid cells (1 liquid, 0 air), and its faces'
// validity in two buffers, across X, then Y, then Z.
typedef struct mvfxLiquidGasRoom
{
    int32_t* cellStart;
    int32_t* order;
    float* local;
    uint8_t* liquid[MVFX_GAS_LEVELS];
    uint8_t* valid[2];
    int32_t capacity;
} mvfxLiquidGasRoom;

// A level of a gas's multigrid: its pressure, the right-hand side it
// solves for and a buffer the smoothing writes, each over its cells.
typedef struct mvfxGasLevel
{
    float* pressure;
    float* rhs;
    float* scratch;
} mvfxGasLevel;

// A gas's state in an instance: its velocity's components at the faces
// across their axes and its density, temperature and fuel at the cells'
// centers, each in two buffers (current the one now); where its box was
// put (world space: the instance's place at its start); whether it holds
// anything (a value of a scalar above MVFX_GAS_EMPTY).
typedef struct mvfxGasState
{
    float* velocity[3][2];
    float* scalars[3][2];
    int32_t current;
    // Its pressure solve's levels; the first's pressure is kept from one
    // substep to the next, the solve's start.
    mvfxGasLevel levels[MVFX_GAS_LEVELS];
    // Its work buffer (NULL for none), written before it is read in
    // each use.
    float* work;
    // Each level's faces' open areas across each axis (in its finest
    // cells' faces: a face between two whole cells of level l has 4^l in
    // 3D, 0 for a closed wall or a solid), NULL across an axis of one
    // cell.
    float* areas[MVFX_GAS_LEVELS][3];
    // Each level's faces' coefficients in the solve (open area over the
    // distance between the centers) and each cell's sum of its faces'.
    float* coefficients[MVFX_GAS_LEVELS][3];
    float* weights[MVFX_GAS_LEVELS];
    // With obstacles: each cell's solid, 0 for none, else 1 more than the
    // index of the obstacle it is within; the obstacles resolved in the
    // box's frame at the end of this step and of the one before
    // (obstaclesNow the index of this step's), and whether the one before
    // is known (not at the first step or after a teleport).
    uint8_t* solid;
    mvfxResolvedCollider obstacles[2][MVFX_GAS_OBSTACLES];
    int32_t obstacleCount[2];
    int32_t obstaclesNow;
    bool obstaclesKnown;
    // Whether its obstacles were resolved since its start or a teleport.
    bool obstaclesResolved;
    double origin[3];
    float rotation[4];
    float scale;
    bool holds;
    // In a planned instance, a liquid gas's fastest particle speed at the
    // end of the latest step its simulation reported, and that step's
    // tick (mvfxInstance_SetLiquidSpeed); none reported yet when
    // reportedTick is below 0.
    float reportedSpeed;
    int64_t reportedTick;
    // A liquid or surface gas's room (all NULL for smoke).
    mvfxLiquidGasRoom room;
    // In a planned instance, what its step would take, resolved at the
    // end of the context's step (mvfxPlanGases): its gravity and forces
    // in the box's frame (its obstacles resolved into obstacles above),
    // and the emitters pouring into it.
    float planGravity[3];
    mvfxResolvedForce planForces[MVFX_MAX_FORCES];
    int32_t planPourCount;
    mvfxPlannedPour planPours[MVFX_MAX_EMITTERS];
    mvfxPlannedPour planLiquid;
} mvfxGasState;

// Whether a gas has a field: a surface gas only its distance, held as its
// first scalar; another, every field but the distance.
static inline bool mvfxIsGasFieldOf(const mvfxCompiledGas* gas, mvfxGasField field)
{
    return gas->def.kind == mvfx_gasSurface ? field == mvfx_gasDistance
                                            : field <= mvfx_gasVelocityZ;
}

// A surface gas's reach, R: the distance it holds where no particle is
// near, in the box's lengths.
static inline float mvfxSurfaceReach(const mvfxCompiledGas* gas)
{
    return gas->def.surface.reach * gas->def.surface.radius;
}

// Whether a gas def is valid: mvfx_errorLimit for a value past one of the
// library's named bounds, mvfx_errorInvalid for anything else wrong.
// Names the field it refuses below at.
mvfxResult mvfxCheckGasDef(const mvfxFieldPath* at, const mvfxGasDef* def);

// A valid gas def compiled.
mvfxCompiledGas mvfxCompileGas(const mvfxGasDef* def);

// Places a gas's buffers in a layout: its velocity's components' and its
// scalars' (each two buffers in a row), its levels' (three in a row); for
// a liquid gas, its room for particles of its emitter.
void mvfxAddGasLayout(mvfxLayout* layout, const mvfxCompiledGas* gas, int32_t particles,
                      mvfxGasAt* at);

// Points a gas's state at its buffers in block, all zero.
void mvfxStartGas(mvfxGasState* state, const mvfxCompiledGas* gas, int32_t particles,
                  unsigned char* block, const mvfxGasAt* at);

// A level's cells.
static inline int64_t mvfxGasLevelCells(const mvfxCompiledGas* gas, int32_t level)
{
    return (int64_t)gas->levels[level][0] * gas->levels[level][1] * gas->levels[level][2];
}

// A level's faces across its axes below k (0 to 3), counting none across
// an axis of one cell.
static inline int64_t mvfxGasLevelFaces(const mvfxCompiledGas* gas, int32_t level, int32_t k)
{
    int64_t cells = mvfxGasLevelCells(gas, level);
    int64_t faces = 0;
    for (int32_t a = 0; a < k; ++a)
    {
        int32_t n = gas->levels[level][a];
        faces += n > 1 ? cells / n * (n + 1) : 0;
    }
    return faces;
}

// Zeroes a gas's fields and forgets its obstacles.
void mvfxClearGas(mvfxGasState* state, const mvfxCompiledGas* gas);

#endif
