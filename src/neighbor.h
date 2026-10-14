// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Neighbor grids: cell keys hashed into a table, a stable counting sort,
// queries over the 27 cells around a place.

#ifndef MAUL_VFX_SRC_NEIGHBOR_H
#define MAUL_VFX_SRC_NEIGHBOR_H

#include "instance.h"
#include "life.h"
#include "random.h"

#include <math.h>

// The salt that sets grid keys apart from any other hash.
#define MVFX_GRID_SALT 0x4E474244u

// The greatest cell coordinate in size: places farther out share the
// outermost cells.
#define MVFX_MAX_CELL 0x1p30

// A coordinate's cell for cells of one over inverseCell.
static inline int32_t mvfxGridCell(float x, float inverseCell)
{
    double c = floor((double)x * (double)inverseCell);
    c = !(c >= -MVFX_MAX_CELL) ? -MVFX_MAX_CELL : c > MVFX_MAX_CELL ? MVFX_MAX_CELL : c;
    return (int32_t)c;
}

// A cell's key: pcg4d of its coordinates masked to a table.
static inline uint32_t mvfxCellKey(const int32_t cell[3], uint32_t mask)
{
    mvfxWords4 h = mvfxPcg4d(
        (mvfxWords4){(uint32_t)cell[0], (uint32_t)cell[1], (uint32_t)cell[2], MVFX_GRID_SALT});
    return h.x & mask;
}

// Builds a grid of n places in cells of a size, copying the places and
// velocities when the grid keeps values (v may be NULL when it does not).
void mvfxBuildNeighborGrid(mvfxNeighborGrid* grid, const float* const p[3], const float* const v[3],
                           int32_t n, float cell);

// A query of an emitter's source grid from places in the emitter's space,
// which is the source's: emitters of one space in an instance share their
// anchor, which starts at the instance and moves with it at the same
// steps. Flocking's rules each have a squared radius (0 for a rule
// unused) and share the view's cosine.
typedef struct mvfxNeighborQuery
{
    const mvfxNeighborGrid* grid;
    float inverseCell;
    float radiusSquared;
    int32_t most;
    // Whether the grid is the emitter's own, so a particle leaves itself
    // out.
    bool own;
    bool flock;
    float separationSquared;
    float alignmentSquared;
    float cohesionSquared;
    float viewCosine;
    // The pair force's: its squared radius and its inverse (0 for none),
    // and its strength over the distance.
    float pairSquared;
    float pairInverse;
    const mvfxCompiledLife* pairStrength;
} mvfxNeighborQuery;

// What a query found, in the grid's order: its neighbors' count and sums,
// whether more were left out; for flocking, the separation's sum and the
// counts and sums of those within alignment's and cohesion's radii; and
// the pair force's pull.
typedef struct mvfxNeighborSums
{
    int32_t count;
    float place[3];
    float velocity[3];
    bool capped;
    float apart[3];
    int32_t aligned;
    float alignedVelocity[3];
    int32_t cohered;
    float coheredPlace[3];
    // The pair force's sum of accelerations.
    float pull[3];
} mvfxNeighborSums;

// The query of a valid emitter that finds neighbors.
mvfxNeighborQuery mvfxOpenNeighborQuery(const mvfxInstance* instance, int32_t emitter);

// The neighbors of a particle at a place, heading along its velocity.
mvfxNeighborSums mvfxSearchNeighbors(const mvfxNeighborQuery* query, int32_t particle,
                                     const float place[3], const float velocity[3]);

// Stores a particle's neighbor attributes from its sums, counting a cap.
void mvfxStoreNeighbors(mvfxInstance* instance, int32_t emitter, int32_t particle,
                        const mvfxNeighborSums* sums, const float place[3],
                        const float velocity[3]);

// At an emitter's step, once its ended particles are gone: builds its grid
// from its particles (for an emitter queried), then gives each particle
// its neighbors' count and the offsets to their mean place and velocity
// (for an emitter that queries and does not steer), from its source's
// grid.
void mvfxFindNeighbors(mvfxInstance* instance, int32_t emitter);

#endif
