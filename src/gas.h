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

#include <stdbool.h>
#include <stddef.h>

// The most levels of a gas's multigrid: from 512 cells along an axis
// halved to 2.
#define MVFX_GAS_LEVELS 10

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
    size_t solid;
    size_t work;
} mvfxGasAt;

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
} mvfxGasState;

// A scalar at most this is nothing.
#define MVFX_GAS_EMPTY 1e-6f

// Whether a gas def is valid.
bool mvfxIsGasDefValid(const mvfxGasDef* def);

// A valid gas def compiled.
mvfxCompiledGas mvfxCompileGas(const mvfxGasDef* def);

// Places a gas's buffers in a layout: its velocity's components' and its
// scalars' (each two buffers in a row), its levels' (three in a row).
void mvfxAddGasLayout(mvfxLayout* layout, const mvfxCompiledGas* gas, mvfxGasAt* at);

// Points a gas's state at its buffers in block, all zero.
void mvfxStartGas(mvfxGasState* state, const mvfxCompiledGas* gas, unsigned char* block,
                  const mvfxGasAt* at);

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
